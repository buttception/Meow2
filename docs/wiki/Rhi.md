# RHI

The render hardware interface: one set of C++ interfaces over Vulkan and Direct3D 12. Nothing above it sees an API type, so the backend can be chosen when the app starts and swapped while it runs ([techstack §11](../../techstack.md#11-rhi)).

**Status:** done in M5 (0.6.0). Vulkan and D3D12, with the same features and the same tests: swapchains, resources, the bindless heap, pipelines and their cache, a render graph and GPU-driven drawing, and switching between the two while the app runs.

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

`createBuffer()`, `createTexture()` and `createSampler()` return handles. Textures can have mip levels, each filled by `copyBufferToTexture(source, texture, level, offset)`; samplers set U and V addressing separately (repeat, clamp, mirror), and pipelines cull back faces, front faces (for mirrored meshes) or neither. Every buffer can be used for everything (storage, copies, indices, indirect arguments), so there are no usage flags to get wrong. `RhiMemory` picks where it lives: `GpuOnly`, `Upload` or `Readback`. The last two stay mapped: `mappedMemory()` returns their bytes.

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

Every buffer gets one slot, read as a `ByteAddressBuffer` or written as a `RWByteAddressBuffer`. A texture gets a sampled slot, plus a storage slot per mip level with `TextureDesc::storage` (`bindlessStorageIndex(texture, level)`): a storage view covers exactly one level, as Vulkan requires. Slot 0 of the resource heap is never handed out, so a forgotten index shows up. Slot 0 of the sampler heap is the default sampler.

### Barriers

`barrier(resource, from, to)` says what a resource was used for and what it's used for next, in `ResourceState` terms. The caller states both sides for now. PR 4's render graph will work them out.

### Pipelines

Pipelines are built from bytecode, so all of them share one layout: the heap and the push constants. Viewport and scissor are set by `beginRendering()` to the target's size. Depth compares with `GREATER_EQUAL`, for reversed Z. The viewport has a negative height, so both backends share one set of matrices.

Creating a pipeline is where the driver compiles shaders for the exact GPU, which is slow. So `pipelineCacheData()` returns its compiled results, and `RhiDeviceDesc::pipelineCache` hands them to the next run. Data from another GPU or driver is ignored.

`Renderer::PipelineLibrary` builds pipelines from shader assets and remembers how. On `Assets::ShaderReloaded` it rebuilds what uses that shader. If a rebuild fails, it logs the error and keeps the old pipeline. Keep the `PipelineId` and ask `get()` for the handle each frame, because rebuilds change it.

### Scissors, blending and the swapchain barrier

- `setScissor(ScissorRect)` clips the following draws to a rectangle in pixels from the target's top-left corner, the same on both backends (Vulkan's flipped viewport doesn't move scissors). `beginRendering()` resets it to the whole target.
- `GraphicsPipelineDesc::blend`: `BlendMode::None` overwrites, `BlendMode::Alpha` blends straight alpha (`src × a + dst × (1 − a)`), for UI.
- `barrier(IRhiSwapchain&)` orders colour writes to a swapchain image between command lists: two render passes in different lists that draw to the same swapchain (the scene, then the UI) aren't ordered against each other without it. Swapchain images stay `ColorTarget` for the whole frame.
- `draw()` has no first vertex: D3D12's `SV_VertexID` leaves it out and Vulkan's includes it, so a shader that needs an offset takes it from the push constants.

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

### Render graph

`Renderer::RenderGraph` holds one frame's passes. Each pass says what it touches and in what state, and `compile()` works out every barrier on one thread. `execute()` then records on jobs, one command list per pass, and returns the lists in pass order for `endFrame()`:

```cpp
RenderGraph graph;
const GraphResource buffer = graph.import(handle, ResourceState::Undefined);
graph.addPass({.name = "Cull", .accesses = {{buffer, ResourceState::ShaderWrite}}, .record = recordCull});
graph.compile();
device->endFrame(*graph.execute(*device, jobs));
```

Passes marked `small` share a list with the small passes next to them, because a job costs more than they do. A pass is never split across lists. Passes run in the order you add them. The graph doesn't reorder or remove them yet.

### GPU-driven drawing

`Renderer::GpuScene` keeps meshes and instances in buffers. A compute pass tests every instance against the frustum and writes an `IndirectDraw` for each survivor into one of three lists (back-culled, mirrored, double-sided), then one `drawIndirectCount()` per list draws them, with offsets into one draw buffer and one count buffer. So the CPU's cost doesn't grow with the instance count. Materials, shading and the lists are in [Rendering](Rendering.md).

There's no vertex input. The vertex shader takes `SV_VertexID`, reads the index from a heap buffer, then reads the `PackedVertex`: positions are 16-bit in the mesh's bounds, and normals are octahedral.

**Matrices in shared structs:** DXC reads a `float4x4` from a raw buffer by column, whatever `-Zpr` and `row_major` say. So `Meow2Shared.hlsli` stores matrices as four `float4` rows with `MEOW2_MATRIX`, and shaders rebuild them with `MEOW2_ROWS`.

### D3D12

`createRhiDevice({.backend = RhiBackend::D3D12})` gives the same `IRhiDevice` on Direct3D 12, Windows only. Code above the RHI doesn't change; `PipelineLibrary` picks the DXIL from the cooked shaders instead of the SPIR-V.

**The Agility SDK.** D3D12 itself comes with Windows, and so does the version of it a PC has. The bindless heap needs Shader Model 6.6, which the copy in Windows 10 doesn't have. So each app ships Microsoft's newer runtime, `D3D12Core.dll`, in a `D3D12/` folder next to the exe, and two exported values in the exe (`D3D12SDKVersion`, `D3D12SDKPath`) tell Windows to load it. `meow2_use_agility(<target>)` adds both to an exe and copies the runtime after each build. The release zip has it too. Configure downloads the pinned package and checks its hash, like DXC.

How the API maps across:

| Vulkan | D3D12 |
|---|---|
| Timeline semaphore | Fence, the same counter idea |
| One command pool per thread and frame | One command allocator per list: D3D12 allows one open list per allocator, and a job may hold several |
| Descriptor set: sampler heap, mutable resource heap | Shader-visible sampler heap and CBV/SRV/UAV heap, indexed directly (`ResourceDescriptorHeap`) |
| Push constants | Four root constants at `b0`, in the one root signature |
| synchronization2 barriers | Enhanced barriers: the same stages, accesses and layouts, so `ResourceState` maps one to one |
| `vkCmdDrawIndirectCount` | `ExecuteIndirect` with a command signature |
| VMA | D3D12MA |
| Negative viewport height | Nothing: D3D12's viewport Y already points down |

Things that work differently:

- **Writable buffers have their own heap slot.** Vulkan's storage buffer descriptor reads and writes. D3D12 has a read view and a write view, so a shader that writes a buffer uses `bindlessStorageIndex(buffer)`, as textures already did. Only `RhiMemory::GpuOnly` buffers can be written.
- **The instance of an indirect draw.** Vulkan's `SV_InstanceID` includes the draw's first instance; D3D12's starts at 0 for every draw. So `IndirectDraw` starts with an `instance` field, the command signature writes it into `PushConstants::instance` before each draw, and shaders call `indirectInstance(SV_InstanceID)`, which picks the right one per backend. After an indirect draw that root constant is undefined, so set push constants again before the next draw.
- **Filling a buffer** uses `WriteBufferImmediate`, one 32-bit word at a time. It's meant for small buffers such as draw counts.
- **Swapchains** use the flip model with three images, so the image a frame gets was last used three frames ago, which `beginFrame()` already waited for. Flip-model images can't be sRGB, so they're plain BGRA and drawn through sRGB views. DXGI stretches images to the window rather than reporting them out of date, so `needsResize()` is always false.
- **Pipeline cache.** `pipelineCacheData()` is an `ID3D12PipelineLibrary`, keyed by a hash of each pipeline's description and bytecode. Renderer keeps one cache file per backend. RenderDoc doesn't support pipeline libraries, so while it's in the process D3D12 has no cache and the file on disk is kept.
- **Markers** use `BeginEvent` with PIX's plain-string format, which PIX, RenderDoc and Nsight read with no PIX library.

### Switching backends while running

`Renderer::RenderDevice` owns the device and switches it with `swapTo(backend)`, between frames. Meow2Renderer does it on **F10**, or once at frame N with `--set rhi.swapAtFrame=N`; it takes about 0.3 to 0.6 s.

```cpp
std::unique_ptr<RenderDevice> render = *RenderDevice::create({.backend = RhiBackend::Vulkan}, rhiLog);
PipelineLibrary pipelines{{*render, assets, events, rhiLog}};                  // a device user
std::unique_ptr<GpuScene> scene = *GpuScene::create(*render, pipelines, desc); // another, registered after it
DeviceUser window = render->addUser(
    "Window", [&](IRhiDevice& old) { swapchain.reset(); },                    // let go
    [&](IRhiDevice& fresh) { return makeSwapchain(fresh); });                // make again from what you kept
// ... frames, each fetching render->device() ...
Result<void> swapped = render->swapTo(RhiBackend::D3D12);
```

A switch:

1. waits for the GPU to finish;
2. has every **device user** release its GPU objects, last registered first;
3. keeps the old device's pipeline cache (each backend has its own) and destroys the device;
4. creates the new device with that backend's cache, on the same window;
5. has every user restore its objects, first registered first.

**The users are the resource registry.** Each one keeps the CPU-side description of what it made, and makes it again: `PipelineLibrary` from its pipeline requests (with the new backend's bytecode, since every shader is cooked to DXIL and SPIR-V), `GpuScene` from its description plus a CPU copy of its meshes and instances (about 8 MB for 100,000 instances), the app from its window. Nothing else changes: `PipelineId`s and the scene object survive, but every handle, heap index and `IRhiDevice&` behind them is new, so nothing above the RHI keeps one across frames. Fetch them each frame.

**Why owners restore themselves** rather than a central list of every buffer: data derived from handles can't be replayed. A mesh record holds its vertex buffer's heap index, which is different on the new device, so the owner has to write the record again. Only the owner knows that.

**A switch that fails goes back.** If the new device can't start (no GPU for that API, a missing runtime) or a user can't restore, the users that did restore are released again, the old backend starts again and everything is restored there; `swapTo` returns the error and the app keeps running. A restore that fails cleans up what it made itself, because it isn't released afterwards. Only if the way back fails too is there no device (`hasDevice()` is false), and Meow2Renderer stops.

Pipelines are built for one colour format, so Meow2Renderer refuses a backend whose swapchain has another one (both give `Bgra8Srgb` today).

### When the GPU is lost

A GPU crash, a hang or a driver reset loses the device: every later call fails. The next `beginFrame()` (or a failed submit or present) logs what the driver knows, once, and returns an error.

- **D3D12:** DRED (Device Removed Extended Data) is always on. It reports how far each command list got, its last marker, and on a page fault the address and the resources near it.
- **Vulkan:** `VK_EXT_device_fault`, where the driver has it, gives a description, the GPU addresses involved and the vendor's fault codes.

Test it on D3D12 with `ID3D12Device5::RemoveDevice()`, which behaves exactly like a real loss (`RhiGpuD3D12.DeviceLossTest.*`).

### PIX and Nsight from the app

Looked into for M6's capture button; nothing added yet, because each needs an SDK approved first.

- **PIX** (D3D12): `pix3.h` from the WinPixEventRuntime package can load PIX's capture library into the app and capture the next frames to a `.wpix` file, then open it in PIX, much like RenderDoc's API. It needs PIX installed. Markers already work without it.
- **Nsight Graphics** (NVIDIA, both backends): its injection API, in the Nsight Graphics SDK headers, can inject Nsight into a running app and trigger a capture. NVIDIA's GPU crash dumps (Nsight Aftermath) are a separate SDK, which would add shader-level detail to the reports above.

## Minimum GPU

- Vulkan 1.3
- `VK_EXT_mutable_descriptor_type`, plus `VK_KHR_swapchain` to show a window
- One queue family with graphics and compute
- Dynamic rendering, synchronization2, timeline semaphores, buffer device address, demote to helper invocation (`discard`)
- Descriptor indexing for bindless: runtime arrays, partially bound, update after bind, dynamic and non-uniform indexing
- Storage writes in vertex and pixel shaders; storage images written without a declared format
- Multi-draw indirect, indirect draw count, 64-bit integers in shaders
- BC texture compression (BC7, BC5)
- Wireframe fill (`fillModeNonSolid`), for the debug views

On D3D12:

- Feature level 12_0, Shader Model 6.6, resource binding tier 3
- Enhanced barriers, and buffer ↔ texture copies with unrestricted row pitch (both need a driver from the last few years)
- The Agility SDK runtime in `D3D12/` next to the exe; without it the requirements check says so

## Settings

| Key | Default | Meaning |
|---|---|---|
| `rhi.backend` | `vulkan` | `vulkan` or `d3d12` (Windows), at startup; F10 switches while running |
| `rhi.swapAtFrame` | 0 | Switch backend once at this frame, for checks with no keyboard; 0 never |
| `rhi.validation` | on, off in Release | The Vulkan validation layer (Vulkan SDK) or the D3D12 debug layer (Agility SDK) |
| `rhi.vsync` | on | Wait for the display |

Renderer saves the pipeline cache of each backend it used to `cache/vulkan-pipelines.bin` and `cache/d3d12-pipelines.bin` next to the exe, and reads cooked shaders from `cooked/` next to it. Without them it just clears the window.

With SDL's `offscreen` driver there's no window to draw to, so Renderer creates no device. That's how smoke tests run on machines with no GPU.

### Pipeline states for debugging

`GraphicsPipelineDesc` has `fill` (`FillMode::Wireframe` draws only edges), `blend = BlendMode::Additive` (counting, light accumulation) and `depthTest` (false passes every pixel; depth stays bound, the compare becomes ALWAYS, so the pipeline still matches a pass with a depth target). D3D12's pipeline library is keyed by these states too: before, two pipelines with the same shaders but different fill or blending could load each other from the cache. `IRhiDevice::textureInfo()` reports a texture's size, mip levels and format.

## Tests

`Rhi.*` need no GPU. `RhiGpu.*` (Vulkan) and `RhiGpuD3D12.*` need a GPU and a desktop, so they're built only with `-DMEOW2_GPU_TESTS=ON`. CI doesn't run them yet. Both are built from the same sources (`tests/Graphics/Rhi/RhiTestBackend.h` picks the backend), so every behaviour is checked on both APIs.

```bash
cmake --preset windows-msvc-debug -DMEOW2_GPU_TESTS=ON
```

`RenderDeviceTest.*` draws a scene, switches, and requires the same image after; in the D3D12 suite it also goes to Vulkan and back, allowing 1 % of pixels to differ by 2 levels between the two APIs.

Each GPU test requires validation to report no errors, and `RhiGpu.RhiGpuTest.ValidationIsActive` fails if the layer isn't running, so a clean result can't come from a missing layer.
