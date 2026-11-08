# FH6 ARM64 integration probes

These probes use a temporary Wine prefix and leave the installed launcher, game, and bottle configuration alone. They do not start a game.

- Run `bash verify.sh metal` with `VULKAN_INCLUDE` set to the Vulkan-Headers include directory, `FFX_SDK_ROOT` set to the FidelityFX SDK's `Kits/FidelityFX` directory, and `DEVELOPER_DIR` set to the Xcode installation with the Metal compiler. The script builds the existing fsr4-native-macos source for ARM64. It allocates exportable KosmicKrisp images, converts a FidelityFX dispatch descriptor to a Metal frame, and verifies the FSR 4.1.2 pixels from a Vulkan readback at 3472 × 2266. The last readback submission waits on a Vulkan timeline value released by the Metal completion callback.
- Run `bash verify.sh steam` with `LLVM_MINGW_BIN` set to the llvm-mingw bin directory and the native Steam client signed in. The probe loads a Windows Steam API forwarder and opens a pipe to the existing macOS Steam session through Proton-ish. It releases the pipe and does not launch Windows Steam.

Highball r4's installed VKD3D provides `GetVulkanResourceInfo1`, `BeginVkCommandBufferInterop`, and `GetVulkanHeapInfo`. A separate FEX/D3D12 check resolved RGBA16F, D32F, and RG16F images. Exporting the D3D12 heap itself through `CreateSharedHandle` returned `E_NOTIMPL`. The Vulkan allocation in the Metal probe is created explicitly as exportable, so that result does not establish that arbitrary D3D12 textures are directly shareable.

The renderer still needs to hand off each game's FidelityFX dispatch at a command submission boundary and pass the real temporal inputs. The Steam probe verifies IPC and an existing login. App-specific startup, Steam Cloud, and online gameplay still require game validation.
