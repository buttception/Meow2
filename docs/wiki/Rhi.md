# RHI

The render hardware interface: one set of C++ interfaces over Vulkan and Direct3D 12. Nothing above it sees an API type, so the backend can be chosen when the app starts and swapped while it runs ([techstack §11](../../techstack.md#11-rhi)).

**Status:** Vulkan device, swapchains and clearing a window. Resources, bindless, pipelines and D3D12 come later in M5.

## Using it

```cpp
Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice({.appName = "Game", .validation = true}, rhiLog);
Result<std::unique_ptr<IRhiSwapchain>> swapchain = (*device)->createSwapchain(
    {.window = window.nativeHandle(), .width = 1280, .height = 720, .vsync = true});

IRhiCommandList* list = *(*device)->beginFrame();  // waits until a frame slot is free
if ((*device)->acquire(**swapchain))               // false while minimised or waiting for a resize
{
    list->clearSwapchain(**swapchain, Color{.r = 0.1F, .g = 0.2F, .b = 0.4F});
}
(*device)->endFrame();                             // submits, then shows every acquired swapchain
```

Destroy swapchains before their device, and windows after their swapchains. Call `resize()` between frames when the window's pixel size changes or `needsResize()` is true.

One device can drive several swapchains, which is what ImGui viewports and the editor's viewport need. All of them are submitted together and shown in one present call.

## How it works

### Frames in flight

The CPU may record `c_maxFramesInFlight` (2) frames ahead of the GPU. Each frame slot has its own command pool, reset as a whole when the slot comes round again.

Pacing uses one **timeline semaphore**, a counter on the GPU. Every submit signals the next value, and `beginFrame()` waits until the value that slot signalled has been reached.

### Swapchain semaphores

A binary semaphore links one GPU operation to the next, and can't be reused until something has waited on it:

- **Acquire semaphores** are per frame slot: signalled when the display hands an image over, and waited on by the submit.
- **Present semaphores** are per image: signalled by the submit and waited on by present. They can't be per frame, because the display gives images back in its own order.

### Resizing and minimising

Vulkan can't make a zero-sized swapchain, so a minimised window has no images: `width()` is 0 and `acquire()` returns false. `SUBOPTIMAL` still shows the frame and asks for a resize before the next one.

### Choosing a GPU

`missingVulkanRequirements()` compares a GPU with the requirements below. It's a plain function over a struct of facts, so it's tested with no GPU. Among GPUs that qualify, dedicated beats built-in, and anything beats software. If none qualifies, the error names each GPU and what it's missing, and Renderer shows that in a dialog.

### volk and VMA

**volk** loads Vulkan's functions directly from the driver at runtime, so the app doesn't link the Vulkan loader. Without Vulkan, the app gets a clear error rather than failing to start. volk keeps its pointers in globals, so only one Vulkan device can exist at a time.

**VMA** (VulkanMemoryAllocator) sub-allocates GPU memory. It's created with the device and is first used by buffers and textures. volk must be included before `vk_mem_alloc.h`, or VMA leaves out the function that takes volk's pointers.

## Minimum GPU

- Vulkan 1.3
- `VK_EXT_mutable_descriptor_type`, plus `VK_KHR_swapchain` to show a window
- One queue family with graphics and compute
- Dynamic rendering, synchronization2, timeline semaphores, buffer device address
- Descriptor indexing for bindless: runtime arrays, partially bound, update after bind, non-uniform indexing
- Multi-draw indirect, indirect draw count, 64-bit integers in shaders

## Settings

| Key | Default | Meaning |
|---|---|---|
| `rhi.validation` | on, off in Release | The Vulkan validation layer, which comes with the Vulkan SDK |
| `rhi.vsync` | on | Wait for the display |

With SDL's `offscreen` driver there's no window to draw to, so Renderer creates no device. That's how smoke tests run on machines with no GPU.

## Tests

`Rhi.*` need no GPU. `RhiGpu.*` need a GPU and a desktop, so they're built only with `-DMEOW2_GPU_TESTS=ON`. CI doesn't run them yet.

```bash
cmake --preset windows-msvc-debug -DMEOW2_GPU_TESTS=ON
```

Each GPU test requires validation to report no errors, and `RhiGpu.RhiGpuTest.ValidationIsActive` fails if the layer isn't running, so a clean result can't come from a missing layer.
