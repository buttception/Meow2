/// @file
/// @brief ShaderCompiler over DXC, loaded at runtime. The only file that includes DXC's headers.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Process.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Core/SharedLibrary.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

// dxcapi.h is COM on Windows and needs its base types first. Off Windows it brings its own (WinAdapter.h).
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <unknwn.h>
#include <windows.h>
#endif
#include <dxcapi.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <mutex>
#include <string_view>
#include <utility>

namespace Meow2::Shader
{
namespace
{
/// @brief The smallest COM smart pointer: DXC's own CComPtr exists only off Windows, ATL's needs ATL.
template <class T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ~ComPtr()
    {
        if (m_pointer != nullptr)
        {
            m_pointer->Release();
        }
    }

    T* operator->() const
    {
        return m_pointer;
    }

    T* get() const
    {
        return m_pointer;
    }

    /// @brief For functions that return an interface through a `void**`.
    void** out()
    {
        return reinterpret_cast<void**>(&m_pointer);
    }

private:
    T* m_pointer = nullptr;
};

/// @brief UTF-8 → the wide strings DXC's interface uses (16 bits on Windows, 32 elsewhere).
std::wstring toWide(std::string_view text)
{
    return std::filesystem::path{std::u8string{text.begin(), text.end()}}.wstring();
}

/// @brief The wide strings DXC hands back → UTF-8.
std::string toUtf8(const wchar_t* text)
{
    const std::u8string converted = std::filesystem::path{text}.u8string();
    return std::string{converted.begin(), converted.end()};
}

/// @brief The shader-model profile of a stage. SM 6.6 is the floor: bindless heaps (techstack.md §11).
const wchar_t* profileOf(ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex:
            return L"vs_6_6";
        case ShaderStage::Pixel:
            return L"ps_6_6";
        case ShaderStage::Compute:
            return L"cs_6_6";
        case ShaderStage::Amplification:
            return L"as_6_6";
        case ShaderStage::Mesh:
            return L"ms_6_6";
    }
    return L"";
}

// COM interfaces have no virtual destructor on purpose: objects end through Release(), never `delete` through a base.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
/// @brief Serves `#include`s from the FileSystem, and records them. Lives on the stack of one compile() call.
class IncludeHandler final : public IDxcIncludeHandler
{
public:
    IncludeHandler(const FileSystem::FileSystem& files, IDxcUtils& utils, std::vector<std::string>& includes)
        : m_files(files)
        , m_utils(utils)
        , m_includes(includes)
    {
    }

    virtual ~IncludeHandler() = default;

    HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** includeSource) override
    {
        *includeSource = nullptr;
        // DXC asks for `./assets/shaders/../Common.hlsli`; the FileSystem wants plain virtual paths.
        const std::string path = std::filesystem::path{toUtf8(fileName)}.lexically_normal().generic_string();
        Result<std::string> text = m_files.readText(path);
        if (!text)
        {
            return E_FAIL;
        }
        if (std::find(m_includes.begin(), m_includes.end(), path) == m_includes.end())
        {
            m_includes.push_back(path);
        }
        IDxcBlobEncoding* blob = nullptr;
        // CreateBlob copies, so the text can go out of scope.
        const HRESULT status = m_utils.CreateBlob(text->data(), static_cast<UINT32>(text->size()), CP_UTF8, &blob);
        *includeSource = blob;
        return status;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
    {
        if (!IsEqualIID(id, __uuidof(IDxcIncludeHandler)) && !IsEqualIID(id, __uuidof(IUnknown)))
        {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        *object = this;
        return S_OK;
    }

    // Not reference-counted: DXC only uses it during Compile(), and it lives on the caller's stack.
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return 1;
    }

private:
    const FileSystem::FileSystem& m_files;
    IDxcUtils& m_utils;
    std::vector<std::string>& m_includes;
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace

/// @brief The loaded DXC library.
struct ShaderCompiler::Impl
{
    Impl(const FileSystem::FileSystem& fileSystem, SharedLibrary dxc, DxcCreateInstanceProc create)
        : files(fileSystem)
        , library(std::move(dxc))
        , createInstance(create)
    {
    }

    const FileSystem::FileSystem& files;  ///< Reads the sources.
    SharedLibrary library;                ///< dxcompiler; keeps createInstance valid.
    DxcCreateInstanceProc createInstance; ///< DXC's factory function.
#if !defined(_WIN32)
    /// @brief One compile at a time: off Windows, DXC's string conversions change the process-wide C locale (found by
    ///        TSan). Windows compiles in parallel.
    std::mutex compileMutex;
#endif
};

ShaderCompiler::ShaderCompiler(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

ShaderCompiler::ShaderCompiler(ShaderCompiler&& other) noexcept = default;
ShaderCompiler& ShaderCompiler::operator=(ShaderCompiler&& other) noexcept = default;
ShaderCompiler::~ShaderCompiler() = default;

Result<ShaderCompiler> ShaderCompiler::create(const FileSystem::FileSystem& files)
{
#if defined(_WIN32)
    constexpr const char* c_libraryName = "dxcompiler.dll";
#else
    constexpr const char* c_libraryName = "libdxcompiler.so";
#endif
    Result<SharedLibrary> library = SharedLibrary::open(executablePath().parent_path() / c_libraryName);
    if (!library)
    {
        return Unexpected<Error>{library.error()};
    }
    auto* createInstance = reinterpret_cast<DxcCreateInstanceProc>(library->symbol("DxcCreateInstance"));
    if (createInstance == nullptr)
    {
        return fail(ErrorCode::Unsupported, "DxcCreateInstance isn't exported by the DXC library");
    }
    return ShaderCompiler{std::make_unique<Impl>(files, std::move(*library), createInstance)};
}

Result<ShaderCompileOutput> ShaderCompiler::compile(const ShaderCompileDesc& desc) const
{
    MEOW2_PROFILE_ZONE();
    Result<std::string> source = m_impl->files.readText(desc.path);
    if (!source)
    {
        return Unexpected<Error>{source.error()};
    }

#if !defined(_WIN32)
    // Held across Compile(), which reads includes through the FileSystem: that never calls back into the compiler.
    const std::scoped_lock lock{m_impl->compileMutex};
#endif
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(m_impl->createInstance(CLSID_DxcUtils, __uuidof(IDxcUtils), utils.out())) ||
        FAILED(m_impl->createInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), compiler.out())))
    {
        return fail(ErrorCode::Unknown, "DXC couldn't create its compiler");
    }

    const std::wstring path = toWide(desc.path);
    const std::wstring entryPoint = toWide(desc.entryPoint);
    std::vector<std::wstring> defines;
    defines.reserve(desc.defines.size());
    for (const ShaderDefine& define : desc.defines)
    {
        defines.push_back(toWide(std::format("{}={}", define.name, define.value)));
    }

    // `path` goes first without a switch: it's the name in error messages and the base of relative includes.
    std::vector<LPCWSTR> arguments{path.c_str(), L"-E",  entryPoint.c_str(), L"-T", profileOf(desc.stage), L"-HV",
                                   L"2021",      L"-Zpr"}; // row-major matrices, as in Meow2/Math
    for (const std::wstring& define : defines)
    {
        arguments.push_back(L"-D");
        arguments.push_back(define.c_str());
    }
    if (desc.target == ShaderTarget::Spirv)
    {
        arguments.push_back(L"-spirv");
        arguments.push_back(L"-fspv-target-env=vulkan1.3");
        arguments.push_back(L"-fvk-use-dx-layout"); // one struct, one layout on both backends
    }
    if (desc.optimization == ShaderOptimization::Debug)
    {
        arguments.push_back(L"-Od");
        arguments.push_back(L"-Zi");
        if (desc.target == ShaderTarget::Dxil)
        {
            arguments.push_back(L"-Qembed_debug");
        }
    }
    else
    {
        arguments.push_back(L"-O3");
    }

    ShaderCompileOutput output;
    IncludeHandler includeHandler{m_impl->files, *utils.get(), output.includes};
    const DxcBuffer buffer{source->data(), source->size(), DXC_CP_UTF8};
    ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&buffer, arguments.data(), static_cast<UINT32>(arguments.size()), &includeHandler,
                                 __uuidof(IDxcResult), result.out())))
    {
        return fail(ErrorCode::Unknown, std::format("{}: DXC failed to run", desc.path));
    }

    ComPtr<IDxcBlobUtf8> messages;
    result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), messages.out(), nullptr);
    std::string messageText;
    if (messages.get() != nullptr && messages->GetStringLength() > 0)
    {
        messageText.assign(messages->GetStringPointer(), messages->GetStringLength());
    }

    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status))
    {
        return fail(ErrorCode::ParseError, std::move(messageText));
    }

    ComPtr<IDxcBlob> object;
    result->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob), object.out(), nullptr);
    if (object.get() == nullptr || object->GetBufferSize() == 0)
    {
        return fail(ErrorCode::Unknown, std::format("{}: DXC produced no bytecode", desc.path));
    }
    const auto* bytes = static_cast<const std::byte*>(object->GetBufferPointer());
    output.bytecode.assign(bytes, bytes + object->GetBufferSize());
    output.warnings = std::move(messageText);
    return output;
}
} // namespace Meow2::Shader
