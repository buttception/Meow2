# RHI

The render hardware interface: one set of C++ interfaces over Vulkan and Direct3D 12. Nothing above it sees an API type, so the backend can be chosen when the app starts and swapped while it runs ([techstack §11](../../techstack.md#11-rhi)).

**Status:** Vulkan device, swapchains, resources, the bindless heap and pipelines. Meow2Renderer draws a triangle through the heap. Indirect drawing and D3D12 come later in M5.

## Using it

```cpp
Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice({.appName = "Game", .validation = true}, rhiLog);
Result<std::unique_ptr<IRhiSwapchain>> swapchain = (*device)->createSwapchain(
    {.window = window.nativeHandle(), .width = 1280, .height = 720, .vsync = true});

IRhiCommandList* list = *(*device)->beginFrame();  // waits until a frame slot is free
if ((*device)->acquire(**swapchain))               // false while minimised or waiting for a resize
{
    list->beginRendering(**swapchain, Color{.r = 0.1F, .g = 0.2F, .b = 0.4F});
    list->setPipeline(pipeline);
    list->setPushConstants({.userData = (*device)->bindlessIndex(vertices)});
    list->draw(3);
    list->endRendering();
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

### Resources

`createBuffer()`, `createTexture()` and `createSampler()` return handles. Every buffer can be used for everything (storage, copies, indices, indirect arguments), so there are no usage flags to get wrong. `RhiMemory` picks where it lives: `GpuOnly`, `Upload` or `Readback`. The last two stay mapped: `mappedMemory()` returns their bytes.

These functions are thread-safe; recording and the frame functions aren't.

### Deferred destruction

The GPU runs up to `c_maxFramesInFlight` frames behind the CPU, so an object can still be in use when the CPU is done with it. `destroy()` makes the handle stale at once, then queues the Vulkan objects and heap slots with the timeline value that will prove the GPU has finished. `beginFrame()` and `waitIdle()` destroy what's ready.

The heap slot waits too. If it were reused early, a frame in flight would read the wrong resource with no error at all.

### The bindless heap

There's one descriptor set for the whole engine, bound once per frame:

| Binding | Holds | HLSL |
|---|---|---|
| 0 | up to 256 samplers | `SamplerDescriptorHeap[i]` |
| 1 | up to 65,536 buffers and textures | `ResourceDescriptorHeap[i]` |

A shader gets its indices from the 16 bytes of push constants. Binding 1 is **mutable**: each slot can hold any of the resource types, which is what lets one array serve as both buffers and textures.

Every buffer gets one slot, read as a `ByteAddressBuffer` or written as a `RWByteAddressBuffer`. A texture gets a sampled slot, plus a storage slot with `TextureDesc::storage`. Slot 0 of the resource heap is never handed out, so a forgotten index shows up. Slot 0 of the sampler heap is the default sampler.

### Barriers

`barrier(resource, from, to)` says what a resource was used for and what it's used for next, in `ResourceState` terms. The caller states both sides for now. PR 4's render graph will work them out.

### Pipelines

Pipelines are built from bytecode, so all of them share one layout: the heap and the push constants. Viewport and scissor are set by `beginRendering()` to the target's size. Depth compares with `GREATER_EQUAL`, for reversed Z. The viewport has a negative height, so both backends share one set of matrices.

Creating a pipeline is where the driver compiles shaders for the exact GPU, which is slow. So `pipelineCacheData()` returns its compiled results, and `RhiDeviceDesc::pipelineCache` hands them to the next run. Data from another GPU or driver is ignored.

`Renderer::PipelineLibrary` builds pipelines from shader assets and remembers how. On `Assets::ShaderReloaded` it rebuilds what uses that shader. If a rebuild fails, it logs the error and keeps the old pipeline. Keep the `PipelineId` and ask `get()` for the handle each frame, because rebuilds change it.

### Recording on several jobs

Set `RhiDeviceDesc::recordingThreads` to `JobSystem::threadCount()`. Each job then asks for its own list:

```cpp
IRhiCommandList* list = *device->commandList(jobs.threadIndex());
```

Each thread has its own command pool per frame slot, because a Vulkan pool can only be used by one thread at a time. So jobs can record with no locks. Heap and resource lookups take a shared lock, so they don't block each other.

`endFrame(lists)` submits every list in one call, in the order you give. The order jobs finish in doesn't matter. The frame's first list runs before them, and a hidden last list holds the barriers that hand swapchain images to the display.

### Debugging

`beginMarker(name)`, `endMarker()` and `ScopedMarker` name a region. RenderDoc, PIX and Nsight show them, and in Debug and RelWithDebInfo they're also GPU zones in Tracy. To capture a frame, run Meow2Renderer with RenderDoc turned on, then press **F11**. The RenderDoc window opens on the capture:

```bash
Meow2Renderer.exe --set rhi.renderDoc=true
```

`Rhi::RenderDocCapture` loads RenderDoc's library into the app, the way Unreal's plugin does, so there's no need to start RenderDoc first. It needs RenderDoc installed; `rhi.renderDocPath` sets where. Launching the app from RenderDoc still works, and then F11 brings its window forward.

The setting is off by default, because RenderDoc hooks the graphics API. Without it, and without launching from RenderDoc, nothing is loaded and F11 does nothing.

| Key | Default | Meaning |
|---|---|---|
| `rhi.renderDoc` | off | Load RenderDoc at startup |
| `rhi.renderDocPath` | the default install | Where its library is |
| `rhi.renderDocOpenWindow` | on | Open the RenderDoc window on each capture |
| `rhi.captureFrame` | 0, never | Capture this frame with no keyboard, for scripts |

`memoryStats()` gives the driver's totals and the bytes of live buffers and textures per `MemTag`, set with `BufferDesc::tag` and `TextureDesc::tag`.

### Frame resources

These live in `Meow2/Graphics/Renderer/FrameResources.h`. Call each one's `beginFrame(device->frameSlot())` right after `IRhiDevice::beginFrame()`.

- **`Memory::FrameArena`**: CPU scratch memory per frame slot and thread. A slot resets only when it comes round again, after the GPU has finished with it.
- **`UploadRing`**: CPU-writable GPU memory for one frame. `allocate()` is lock-free, and a full ring returns an error.
- **`ReadbackQueue`**: `request()` copies data toward the CPU, and the callback runs when that frame slot comes round again, so nothing stalls.
- **`OffscreenTarget`**: draw with no window and read the pixels back, for golden images.

`FramePacket` is what the renderer gets from the simulation. `GameLoopCallbacks::extract` runs after `update` and before `render`, and `extractFrame()` copies entities with `WorldTransform` and `Renderable` into the frame arena, camera relative. After that, the scene can change freely.

## Minimum GPU

- Vulkan 1.3
- `VK_EXT_mutable_descriptor_type`, plus `VK_KHR_swapchain` to show a window
- One queue family with graphics and compute
- Dynamic rendering, synchronization2, timeline semaphores, buffer device address
- Descriptor indexing for bindless: runtime arrays, partially bound, update after bind, dynamic and non-uniform indexing
- Storage writes in vertex and pixel shaders
- Multi-draw indirect, indirect draw count, 64-bit integers in shaders

## Settings

| Key | Default | Meaning |
|---|---|---|
| `rhi.validation` | on, off in Release | The Vulkan validation layer, which comes with the Vulkan SDK |
| `rhi.vsync` | on | Wait for the display |

Renderer saves the pipeline cache to `cache/vulkan-pipelines.bin` next to the exe, and reads cooked shaders from `cooked/` next to it. Without them it just clears the window.

With SDL's `offscreen` driver there's no window to draw to, so Renderer creates no device. That's how smoke tests run on machines with no GPU.

## Tests

`Rhi.*` need no GPU. `RhiGpu.*` need a GPU and a desktop, so they're built only with `-DMEOW2_GPU_TESTS=ON`. CI doesn't run them yet.

```bash
cmake --preset windows-msvc-debug -DMEOW2_GPU_TESTS=ON
```

Each GPU test requires validation to report no errors, and `RhiGpu.RhiGpuTest.ValidationIsActive` fails if the layer isn't running, so a clean result can't come from a missing layer.
