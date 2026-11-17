# Motorfest Metal address-space compilation repair

The 2026-11-17 game run passed the repaired DXGI Factory7 query and reached
an activity-card scene, but hundreds of shaders failed to compile. Metal
reported a conditional expression combining `device float4 *` (also `uint *`)
with `const constant float4 *`. Failed graphics pipelines left scene geometry
missing. The user's screenshot is evidence of reaching this scene, not proof
of protected online gameplay.

## Cause and scope

MetalSharp's SPIRV-Cross robust-load optimization treated SPIR-V `Uniform`
storage as Metal `constant` memory. SPIR-V 1.0 also represents storage buffers
with `Uniform` plus `BufferBlock`; their Metal pointers are `device` or
`const device`. Selecting those pointers against `spvZeroConstant` is invalid.

`constant-address-space.patch` additionally checks the backing variable's
actual Metal address space. Proven constant buffers retain the fast pointer
select. Device and unknown pointers use the existing guarded value select.
Bounds checks, null-descriptor behavior and zero on out-of-bounds reads remain.

The source is pinned to MetalSharp-Wine v7.0's graphics submodule
`6a1326c480419701097b4d53b2bed0d0dd3ad4f1`, upstream
https://github.com/metalsharp/VKD3D-Proton-MacOS . MoltenVK base is `5189f8de`;
SPIRV-Cross base is `c1af5739`, with that release's existing patches. Do not
substitute current main: its MoltenVK patch includes unrelated changes.

## Reproduce and build

Fetch the pinned sources with `scripts/fetch-sources.sh moltenvk`, then apply
`constant-address-space.patch` in `sources/MoltenVK/External/SPIRV-Cross`, and
`descriptor-set-capacity.patch` in `sources/MoltenVK`. Build with
`DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-moltenvk.sh --externals`.
This builds a separate library and does not modify any installed engine.

Compile `address-space.comp` using glslangValidator with `-V --target-env vulkan1.0`.
Compile `address-space.c` with the Vulkan headers and link the library under
test, then pass the SPIR-V path as its only argument. `vkcommon.h` is copied
from the pinned upstream tests/vk directory (AGPL-3.0); the SPIRV-Cross patch
retains SPIRV-Cross's upstream license.

The installed original reproduced exactly the device/constant type error and
failed pipeline creation. The candidate executed on the Apple GPU and read
back all 64 expected floats across valid and OOB storage/uniform accesses.
The upstream `tests/vk/robust.c` regression also passed (zero failures), covering
null descriptors, dynamic storage-buffer arrays, OOB writes and atomics.
These are component GPU tests, not proof of complete game stability.

## Deployment

`install.py --library <candidate>` verifies hashes and GPU proof, stages a
private ICD under Motorfest's existing compatdata, then stops only that prefix
and atomically updates its profile. Both `VK_DRIVER_FILES` and
`VK_ICD_FILENAMES` point to this ICD only for AppID 2698940. The signed engine,
other games, native Steam entry, resolution, quality and audio settings are
unchanged. Existing caches are not deleted; the driver uses its normal
source-derived cache key so incompatible shader binaries are not reused.
Backups and a hash receipt accompany the install.

Actual game rendering, BattlEye session establishment and the independent
Ubisoft/libcef 60-second wait remain separately tracked; none is inferred
from these GPU tests.

## Second failure: descriptor set 16 collides with buffer 1

The actual 11:36 run no longer had device/constant errors, but captured vertex
shader `de65eb80bd25a102` declared a uniform block at DescriptorSet 16, Binding 0.
The release supported only 16 descriptor sets (indices 0–15). SPIRV-Cross
therefore treated set 16 as a discrete resource while MoltenVK supplied an
argument-buffer-relative binding. Both that uniform and set 1 landed at Metal
buffer(1), causing pipeline compilation to fail. The later loading-screen run
also lost its GPU device with command-buffer timeout code 2. The timeout is an
additional observed failure, not proven to be caused solely by this collision.

The second candidate expands SPIRV-Cross's set masks to their 32-bit capacity,
MoltenVK's storage count to 32, and stage tracking to four machine words. The
advertised maximum is still capped at 19 by the 31-buffer Metal limit minus
12 reserved implicit buffers. Existing static assertions remain enabled.
This is an implemented binding capacity increase, not just a reported-limit edit.

`set16.c` and `set16.comp` reproduce the original buffer(1) collision. With the
candidate, the real Apple GPU reads both the high-set uniform and push constant
correctly while writing through low set 1. Changing the shader DescriptorSet
operand from 16 to 18 and passing `18` as the second test argument also passes,
exercising the highest advertised set. The 64-float address-space and upstream
robustness regressions pass again with this same library.

Candidate SHA-256: `9eea8bfa2ccc08161596b73ce4845f04fce6bdba50e222424c3e41492a854be7`.
Installed at 11:48 in Motorfest only; full shader dumping is now disabled.
The previous live process was already device-lost and was stopped only after
its log and native thread sample were preserved. Native Steam launched the new
run. Actual scene stability and protected session establishment remain pending.

## Third failure: implicit workgroup vec3 padding

The actual captured SSAO blur shader `b3af8f15508cdfc9` stores 968 workgroup
records containing float AO, float3 normal and float depth. Metal's ordinary
float3 struct alignment expands each record to 48 bytes, producing the exact
46,464-byte pipeline failure. This is conversion layout inflation, not a need
to disable SSAO or reduce game quality.

The candidate packs only vec3 members of simple, implicit-layout float workgroup
structs. Types used in host-visible buffer blocks, explicit member offsets,
pointers, arrays within the leaf record and matrix members are excluded. Scalar
precision is unchanged. The game's actual SPIR-V re-converts and creates a real
Metal compute pipeline with 19,360 bytes of static threadgroup memory (limit
32,768). An independent compute test reproduces the 46,464-byte failure before
the patch and reads back 4,840 correct values after a barrier and cross-thread
indexing with the patch. High descriptor set, constant/device access and upstream
robustness checks were rerun on this exact candidate and passed.

Final candidate hash: `9b31b7aae1fa883a28dd0f1a28d0c7356ea89a73b420feae35049c2f129c2e06`.
The 11:48 game run passed loading per the user, then crashed on CPU access to
address 0x28 in an executable private allocation. That first exception precedes
failed exception dispatch. No new GPU timeout or previous shader binding errors
were observed in that run. The CPU crash is not claimed fixed by this shader patch.
