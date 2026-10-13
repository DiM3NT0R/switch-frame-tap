# macOS clarity filter

The macOS Metal renderer defaults to FSR 1 EASU reconstruction and adaptive
RCAS sharpening, with noise attenuation and overshoot limiting. The previous
Catmull-Rom luma reconstruction remains available with `--upscaler cubic`.
Color conversion, reconstruction, and sharpening share one draw pass.
It cannot recover missing game detail or change the console's render resolution.

Retina drawables are enabled. Window sizing respects the display's usable
bounds while retaining the stream aspect ratio. VideoToolbox decoding remains
independent of this filter.

## Controls

- F8: toggle filtering (settings are saved).
- F9: cycle sharpening strength 40%, 70%, 100%.
- `--clarity on|off`: override the saved mode.
- `--sharpness 0..1`: override the saved strength; default is 0.45.

The default is enabled on macOS. Non-Metal renderers and other platforms
retain the SDL path. MP4 recordings contain the incoming encoded video,
not the filtered display. Existing SDL texture uploads are retained for
instant fallback when toggling the effect.

## Original Catmull-Rom Validation on 2026-10-13

Build: `make -C tools/raw-recv raw-view`.
Stream fixture: `tools/sft_stream_test.py` passed (40 decoded frames,
the deliberately injected sequence gap reported as one lost packet).

Controlled replay used the identical 1920x1080 H.264 frame with a
2574x1448 Retina drawable, filtering disabled/enabled in separate runs.
Stable seconds 3-6, excluding startup and screenshot readback:

| Metric | Off | On |
| --- | ---: | ---: |
| CPU draw + present timing | 2.55 ms | 3.40 ms |
| Header-to-present telemetry | 49.08 ms | 49.78 ms |
| Displayed frame rate | 50.85 fps | 50.58 fps |

These are host replay measurements, not GPU timestamps or physical
input-to-display latency. Live USB verified VideoToolbox NV12 transfer,
1080p decode, and the Metal filter. Regular telemetry reported roughly
35-40 ms header-to-present and queue depth one after startup. Occasional
frame gaps and audio underruns still occurred; this does not establish
that the entire stream is stutter-free. The session later disconnected.
After reconnecting, the new live session displayed a correctly colored
game/loading-screen image at 1920x1080. Stable intervals around seconds
79-82 reported 35.8-37.7 ms header-to-present, 3.0-3.9 ms draw/present,
and queue depth one. A same-session on/off comparison was not completed:
macOS denied synthetic keyboard input. The controlled replay comparison
above remains the evidence for incremental filter cost.
Invalid CLI values were rejected with exit status 2. A fresh generated
1080p color-chart replay passed both the software-renderer fallback and
Metal path; Metal output was visually checked at 2574x1448.
Private fixtures and screenshots are not included in the repository.

## References

- [AMD FSR 1 overview](https://gpuopen.com/fidelityfx-superresolution/)
- [AMD RCAS reference source](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h)
- [Apple MetalFX spatial/temporal inputs](https://developer.apple.com/videos/play/wwdc2022/10103/)
- [SDL Metal encoder API](https://wiki.libsdl.org/SDL2/SDL_RenderGetMetalCommandEncoder)

HUD regression checks: `make -C tools/raw-recv hud-test`, then
`/tmp/frame-tap-hud-test`. The macOS-only fixture checks FPS, graphs,
and full overlays in a letterboxed Retina window and fullscreen, plus
the physical O key with a non-Latin key symbol. HUD drawing rebuilds
its logical viewport instead of multiplying a potentially stale scale.

USB recovery regression checks: `make -C tools/raw-recv usb-test`, then
`/tmp/frame-tap-usb-test`. Mocked transport verifies whole-packet receive
buffering, residual-byte header resynchronization, one clear-halt attempt,
and exit to the reconnect loop on persistent endpoint errors. The host
now reads into a 256 KiB buffer rather than requesting a 32-byte USB
transfer at an arbitrary position inside a pending video payload.
Persistent failures trigger transport reopen with a 500 ms backoff.

Hardware validation: three consecutive close/reopen cycles (seven seconds
of playback per cycle) each resumed 1920x1080 streaming without touching
the cable or restarting the console. Each resynchronized residual bytes;
none logged a PIPE error. Sleep/wake was not physically tested in this run.

Review follow-up: partial USB reads now survive idle timeouts up to a five-second
deadline; individual USB waits are capped at 100 ms for responsive shutdown.
Transient IO/interrupted errors get three bounded retries before reopening.
Reconnect joins the decoder and clears stale frames, decoder references and
queued audio before starting a new reader/decoder session.
The tests cover partial-read/timeout/remaining data, transient IO, and a
VideoToolbox format negotiation falling back to software. HUD regression also
checks that the waiting menu clears the bottom letterbox after a video frame.

libswscale conversion is now macOS-only (needed for VideoToolbox NV12 output).
Other platforms retain the existing YUV420P decoder path and diagnose other
pixel formats explicitly. Windows build scripts therefore need no new swscale
dependency. Window fitting uses comparisons instead of fmin/fmax, avoiding a
new Linux libm dependency. Windows/Linux binaries were not built on this host.
The follow-up live USB run passed 80 seconds without USB error or reconnect
messages. This is bounded evidence, not a guarantee against device-side USB
enumeration failures; the preceding vanished device required physical cable
reconnection to become visible to macOS again.

Temporal game upscalers require inputs such as motion vectors and depth
that this H.264 stream does not expose. Spatial MetalFX would require a
different command-buffer integration. The current single-pass route was
chosen to keep the renderer change bounded and avoid extra frame queues.
# FSR 1 EASU + RCAS

The default macOS reconstruction is AMD FSR 1's 12-tap edge-adaptive
spatial upsampling (EASU), followed by RCAS at output-pixel spacing.
`--upscaler cubic` selects the previous Catmull-Rom implementation for A/B
comparison; `--upscaler easu` selects the new default. At native size or
downscaling, EASU is bypassed. The HUD reports the actual active kernel.

Both stages are fused into one Metal fragment pass: RCAS's five input
samples each evaluate EASU, rather than reading an intermediate texture.
This avoids a new frame queue or CPU/GPU wait, at the cost of redundant
sampling. No temporal history, motion estimation or frame generation is used.
Sharpness retains the existing 0..1 strength control and noise attenuation.
Source reference: https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h

EASU validation: shader compiled on the host Metal device; HUD tests passed
windowed/fullscreen, neutral flat-field preservation, old-mode selection,
and waiting-menu clearing. USB mocks passed. Live gameplay was visually
checked at a 2880x1800 drawable with the EASU label and no visible corruption.
Short successive USB runs (not identical frozen input) gave median
header-to-present / draw+vsync of 36.95 / 4.30 ms for cubic and
36.50 / 4.20 ms for EASU, excluding startup seconds. These observations
show no obvious regression, not a precise incremental GPU cost or physical
input latency. Focus changes during GUI tests caused isolated stalls;
occasional audio underruns remain unrelated to a proof of filter correctness.
