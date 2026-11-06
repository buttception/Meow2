/// @file
/// @brief ShaderCompiler in a build without DXC (`MEOW2_SHADER_TOOLS=OFF`): create() reports that it isn't available.

#include <Meow2/Graphics/Shader/ShaderCompiler.h>

#include <utility>

namespace Meow2::Shader
{
/// @brief Never created: there's no compiler in this build.
struct ShaderCompiler::Impl
{
};

ShaderCompiler::ShaderCompiler(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

ShaderCompiler::ShaderCompiler(ShaderCompiler&& other) noexcept = default;
ShaderCompiler& ShaderCompiler::operator=(ShaderCompiler&& other) noexcept = default;
ShaderCompiler::~ShaderCompiler() = default;

Result<ShaderCompiler> ShaderCompiler::create(const FileSystem::FileSystem& /*files*/)
{
    return fail(ErrorCode::Unsupported, "this build has no shader compiler (MEOW2_SHADER_TOOLS=OFF)");
}

std::string ShaderCompiler::version() const
{
    return {};
}

Result<ShaderCompileOutput> ShaderCompiler::compile(const ShaderCompileDesc& /*desc*/) const
{
    return fail(ErrorCode::Unsupported, "this build has no shader compiler (MEOW2_SHADER_TOOLS=OFF)");
}
} // namespace Meow2::Shader
