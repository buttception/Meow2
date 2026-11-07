/// @file
/// @brief Which backend the GPU tests run on. The same test sources are built once per backend (tests/CMakeLists.txt):
///        `RhiGpu.*` on Vulkan, `RhiGpuD3D12.*` on D3D12, so every RHI behaviour is checked on both.

#pragma once

#include <Meow2/Graphics/Rhi/RhiTypes.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>

namespace Meow2::Rhi
{
#if MEOW2_TEST_D3D12
/// @brief The backend under test.
inline constexpr RhiBackend c_testBackend = RhiBackend::D3D12;
/// @brief The bytecode its pipelines take.
inline constexpr Shader::ShaderTarget c_testTarget = Shader::ShaderTarget::Dxil;
#else
/// @brief The backend under test.
inline constexpr RhiBackend c_testBackend = RhiBackend::Vulkan;
/// @brief The bytecode its pipelines take.
inline constexpr Shader::ShaderTarget c_testTarget = Shader::ShaderTarget::Spirv;
#endif
} // namespace Meow2::Rhi
