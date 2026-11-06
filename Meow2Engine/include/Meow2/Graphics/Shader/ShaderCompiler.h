#pragma once

/// @file
/// @brief ShaderCompiler: HLSL to DXIL (D3D12) and SPIR-V (Vulkan), through DXC.
///
/// DXC is loaded when the compiler is created, not linked, so an app without it (Meow2Player) still starts: it loads
/// cooked shaders and never creates a ShaderCompiler.
/// @code
/// Meow2::Result<Meow2::Shader::ShaderCompiler> compiler = Meow2::Shader::ShaderCompiler::create(files);
/// const Meow2::Shader::ShaderCompileDesc desc{
///     .path = "assets/shaders/Sky.hlsl",
///     .entryPoint = "psMain",
///     .stage = Meow2::Shader::ShaderStage::Pixel,
///     .target = Meow2::Shader::ShaderTarget::Spirv,
/// };
/// Meow2::Result<Meow2::Shader::ShaderCompileOutput> output = compiler->compile(desc);
/// @endcode
/// **Conventions:** matrices are row-major in memory (`-Zpr`) and multiplied as `mul(vector, matrix)`, the same as
/// `Meow2/Math`, so a `Mat4` is copied to the GPU as it is. SPIR-V uses D3D's constant-buffer layout
/// (`-fvk-use-dx-layout`), so one struct has one layout on both backends.
/// @see techstack.md §12, docs/wiki/Shaders.md

#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Meow2::FileSystem
{
class FileSystem;
}

namespace Meow2::Shader
{
/// @brief A programmable stage of the GPU.
enum class ShaderStage : std::uint8_t
{
    Vertex,        ///< Vertex shader.
    Pixel,         ///< Pixel (fragment) shader.
    Compute,       ///< Compute shader.
    Amplification, ///< Amplification (task) shader: decides how many mesh shaders run.
    Mesh,          ///< Mesh shader.
};

/// @brief What a shader compiles to.
enum class ShaderTarget : std::uint8_t
{
    Dxil,  ///< DXIL, for D3D12.
    Spirv, ///< SPIR-V, for Vulkan 1.3.
};

/// @brief Debuggability against speed.
enum class ShaderOptimization : std::uint8_t
{
    Debug,   ///< No optimisation, debug info embedded: for graphics debuggers.
    Release, ///< Full optimisation, no debug info.
};

/// @brief One preprocessor macro. Plain data.
struct ShaderDefine
{
    std::string name;  ///< Macro name.
    std::string value; ///< Macro value.

    /// @brief Defines are equal when name and value are equal.
    friend bool operator==(const ShaderDefine&, const ShaderDefine&) = default;
};

/// @brief One compilation. Plain data.
struct ShaderCompileDesc
{
    std::string path;                                              ///< Virtual path of the source file (FileSystem).
    std::string entryPoint;                                        ///< Function to compile.
    ShaderStage stage = ShaderStage::Pixel;                        ///< Which stage it is.
    ShaderTarget target = ShaderTarget::Spirv;                     ///< What to compile to.
    ShaderOptimization optimization = ShaderOptimization::Release; ///< Debuggability against speed.
    std::span<const ShaderDefine> defines{};                       ///< Macros; they must outlive the compile() call.
};

/// @brief The result of a successful compilation. Plain data.
struct ShaderCompileOutput
{
    std::vector<std::byte> bytecode;   ///< The DXIL container or SPIR-V module.
    std::vector<std::string> includes; ///< Virtual paths of every file it included, for dependency tracking.
    std::string warnings;              ///< Compiler warnings; empty if there were none.
};

/// @brief Compiles HLSL with DXC.
///
/// Sources and `#include`s are read through the FileSystem. An include is looked up relative to the including file.
/// @par Thread safety
/// Thread-safe: compile() may be called from several threads at once, with one DXC compiler instance per call. On
/// Windows the calls run in parallel; elsewhere a mutex runs them one at a time, because DXC changes the process-wide C
/// locale there.
/// @par Lifetime
/// The FileSystem must outlive it.
class ShaderCompiler
{
public:
    /// @brief Loads DXC from the folder of the running executable.
    /// @param files Reads the sources. Must outlive the compiler.
    /// @return The compiler, or ErrorCode::NotFound if DXC isn't there, or ErrorCode::Unsupported in a build with
    ///         `MEOW2_SHADER_TOOLS=OFF`.
    [[nodiscard]] static Result<ShaderCompiler> create(const FileSystem::FileSystem& files);

    /// @brief Takes over the loaded compiler.
    /// @param other Left empty.
    ShaderCompiler(ShaderCompiler&& other) noexcept;

    /// @brief Unloads the current compiler and takes over another.
    /// @param other Left empty.
    /// @return This object.
    ShaderCompiler& operator=(ShaderCompiler&& other) noexcept;

    ShaderCompiler(const ShaderCompiler&) = delete;            ///< Not copyable: owns the loaded library.
    ShaderCompiler& operator=(const ShaderCompiler&) = delete; ///< Not copyable: owns the loaded library.

    /// @brief Unloads DXC.
    ~ShaderCompiler();

    /// @brief Compiles one entry point.
    /// @param desc What to compile.
    /// @return The bytecode; ErrorCode::NotFound if the source is missing; ErrorCode::ParseError with DXC's messages,
    ///         `path:line:column: error: …`, if the shader doesn't compile.
    [[nodiscard]] Result<ShaderCompileOutput> compile(const ShaderCompileDesc& desc) const;

    /// @brief The compiler's version: part of a cooked shader's cache key.
    /// @return The pinned DXC release, e.g. `v1.9.2609`.
    [[nodiscard]] std::string version() const;

    /// @brief Backend state, defined in `src/Graphics/Shader/Dxc/`.
    struct Impl;

private:
    explicit ShaderCompiler(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Shader
