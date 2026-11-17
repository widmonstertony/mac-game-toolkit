# Consecutive-frame recorder

This is an installed FH6-only observation layer, **not a new renderer or a
ghosting fix**. The existing FSR quality upscaler, inner FG provider and 2×
scheduler are unchanged. MetalFX remains a separate, inactive candidate.

## Five-image upgrade, November 17

The prior recorder could compare scene and generated output only after the FG
provider returned. It could not distinguish a scene resource changed during that
dispatch from corruption already present in the input. The upgraded recorder
adds `scene-before-fg.bin`, copied into a distinct readback footprint before the
inner dispatch. It also retains the original four images, matching frame metadata
and source references until GPU completion. A duplicate before-tap, changed
resource identity or incompatible frame metadata aborts the capture.

The real D3DMetal test deliberately overwrites the same scene texture on the GPU
between taps. The saved pre/post values remain different and correct across
eight frames, including reused output resources. Forwarding through the complete
existing provider remains byte-identical for twelve fixture frames. Serialization
checks 491,520 pixels across the five images. The installer's old four-image
count rejected this valid upgrade; it now requires the five-image result.

The outer DLL was installed and its hash rechecked against the tested artifact.
The exact existing inner DLL was retained. The previous outer/profile and the
verification receipt are backed up beside the FH6 profile. A running FH6 process
is rejected before replacement. No new gameplay capture or image-quality result
is claimed by this upgrade; normal recording still requires the bounded explicit
request. Native resolution, quality settings, audio, networking and caches were
not changed.

See [GPU validation](validation-2026-11-17.json). The fixture's existing provider
can produce nonzero analytic image errors; forwarding equality verifies that
this recorder does not change them, not that the underlying rendering is correct.
