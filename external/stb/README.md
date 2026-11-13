# stb_image and stb_image_write

`stb_image.h` (v2.30) and `stb_image_write.h` (v1.16) are copied unchanged from [nothings/stb](https://github.com/nothings/stb) at commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`. Their licence (MIT or public domain, your choice) is at the end of each file.

One header each, so they're copied rather than added as a submodule, like RenderDoc's header (owner-approved; stb_image_write on 2026-11-13). Only `Meow2Engine/src/Assets/Texture/StbImage.cpp` and `StbImageWrite.cpp` compile their implementations, in their own library (`meow2_stb_image`) outside the engine's warnings.

To update them, copy the files from a newer commit and change the commit above.
