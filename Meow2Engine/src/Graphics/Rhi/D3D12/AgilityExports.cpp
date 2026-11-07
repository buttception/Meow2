/// @file
/// @brief The two values Windows looks for in an executable to load the DirectX 12 Agility SDK runtime
///        (`D3D12Core.dll`) from the app's own `D3D12/` folder instead of the one built into Windows.
///
/// Compiled into each executable by meow2_use_agility() (cmake/Meow2Agility.cmake), never into the engine library:
/// Windows reads them from the exe's export table, and a static library's object would be dropped by the linker since
/// nothing refers to it. When the OS's own runtime is newer than this one, Windows keeps its own.
/// @see https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/

#ifndef MEOW2_AGILITY_SDK_VERSION
#error "Compile this file through meow2_use_agility(), which sets MEOW2_AGILITY_SDK_VERSION"
#endif

extern "C"
{
    // GLOBAL: Windows' D3D12 loader reads these by name from the exe; they're constants and never change.
    /// @brief The Agility SDK version the app was built against (`D3D12_SDK_VERSION` of its headers).
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = MEOW2_AGILITY_SDK_VERSION;
    // GLOBAL: as above. The type is fixed by Windows: a pointer, not an array.
    /// @brief Where the runtime is, relative to the exe.
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
