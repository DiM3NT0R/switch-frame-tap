# macOS clarity filter

The macOS Metal renderer defaults to FSR 1 EASU reconstruction and adaptive
RCAS sharpening, with noise attenuation and overshoot limiting. The previous
Catmull-Rom luma reconstruction remains available with `--upscaler cubic`.
EASU uses a compute pass followed by an RCAS render pass. The previous
cubic/bilinear path still uses one render pass.
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
different command-buffer integration.

## FSR 1 EASU + RCAS

The default macOS reconstruction is AMD FSR 1's 12-tap edge-adaptive
spatial upsampling (EASU), followed by RCAS at output-pixel spacing.
`--upscaler cubic` selects the previous Catmull-Rom implementation for A/B
comparison; `--upscaler easu` selects the new default. At native size or
downscaling, EASU is bypassed. The HUD reports the actual active kernel.

EASU is evaluated once per output pixel into a private RGBA16Float texture.
An initial compute dispatch converts source YUV to source-sized RGBA16Float
once per input pixel, avoiding repeated chroma filtering and color conversion
inside the EASU taps. Conversion and EASU share one command buffer, with
tracked texture hazards ordering their separate encoders. Thus there are
three GPU stages including color preparation, two for the FSR filter itself.
RCAS reads the upscaled texture at output-pixel spacing. Four output slots prevent
overwriting textures still used by SDL's in-flight drawables; a cached redraw
reuses the same result without dispatching EASU again. Size changes invalidate
the cache. This is resource rotation, not a queue of four video frames.

SDL exposes its render encoder but not its command buffer. EASU therefore
runs on a separate Metal queue, and the CPU waits for that single compute
command buffer to complete before encoding RCAS in SDL's encoder. This deliberate
wait establishes cross-queue producer/consumer ordering without private SDL
APIs. It costs compute/submission time; it is NOT zero-overhead or fully
asynchronous. The source RGB texture is safe to reuse because each producing
command buffer finishes before the next begins, and RCAS only reads the
rotated output textures. Compute failure permanently selects the existing cubic fallback
until renderer reinitialization. No temporal history or frame generation is used.
Sharpness retains the existing 0..1 strength control and noise attenuation.
Source reference: https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h

Initial fused-EASU validation: shader compiled on the host Metal device; HUD tests passed
windowed/fullscreen, neutral flat-field preservation, old-mode selection,
and waiting-menu clearing. USB mocks passed. Live gameplay was visually
checked at a 2880x1800 drawable with the EASU label and no visible corruption.
Short successive USB runs (not identical frozen input) gave median
header-to-present / draw+vsync of 36.95 / 4.30 ms for cubic and
36.50 / 4.20 ms for EASU, excluding startup seconds. These observations
were too short to expose the subsequent performance problem, and are not a
precise incremental GPU cost or physical input latency. A later live toggle
showed median header-to-present / draw+vsync of 86.45 / 25.90 ms before
disabling fused EASU, versus 36.95 / 3.70 ms after disabling it. This prompted
the two-pass implementation above. Focus changes during GUI tests caused isolated stalls;
occasional audio underruns remain unrelated to a proof of filter correctness.

### Two-pass Validation

`/tmp/frame-tap-hud-test` additionally verifies black/white updates across
more than four texture rotations, cached redraws, odd-sized viewport resizes,
strength 0/100, and BT.709 red/green/blue preparation. HUD, USB mocks and the
40-frame stream fixture passed after the renderer change.

`/tmp/frame-tap-hud-test --benchmark` uses a generated 1920x1080 detailed
grayscale pattern, a fixed 2560x1440 Retina window, strength 100%, 30 warmup
frames and 180 timed frames. It checks the size and actual EASU mode throughout
the run, so a focus/fullscreen transition cannot silently benchmark bilinear.
The same test source linked against the previous fused `clarity.o` measured
17.34 ms/frame for upload + filter + present; the final RGB-prepared two-pass
filter measured 7.05 ms/frame. This is a host synthetic throughput comparison,
not physical input latency. Other host GPU activity and power management can
affect the numbers. The new GPU timestamp covers color preparation + EASU,
not RCAS or vsync; the separate submit/wait measurement includes submission
and cross-queue CPU synchronization overhead.

The final live USB session ran for approximately five minutes, including
window/fullscreen transitions and user F8 comparisons. At 2880x1620 video
output, the late-session stable FSR-on samples had median header-to-present
46.30 ms / draw+vsync 12.80 ms; FSR-off samples had 36.65 / 4.10 ms.
The first two seconds after each toggle were excluded. Both modes showed
about 39 fps; no USB errors or compute failures appeared. FSR still costs
approximately 9-10 ms in this scene. Occasional audio underruns remain;
five minutes is bounded validation, not proof against every long-session
thermal/load condition. The session included FSR-off intervals, not five
uninterrupted minutes with the filter enabled.

## Low-delay Decode and Console Collection

On macOS the default hardware-decoding path now uses one decoder thread and
AV_CODEC_FLAG_LOW_DELAY. This stream uses IDR/P pictures, not B pictures.
An explicit `--threads N` preserves the original frame-threaded behavior
unless `--low-latency` is also specified. Other platforms and an explicit
`--software` keep their prior defaults. If hardware device initialization
fails, the automatic mode also keeps the software frame-threaded default.
If hardware negotiation falls back
to software after the macOS default was chosen, that session still uses the
single-thread setting; monitor throughput for CPU-bound software fallback.

Live A/B with unchanged EASU/RCAS 100% reduced header-to-present from roughly
46-50 ms to 18-22 ms at approximately 39 fps. These are successive live runs,
not controlled physical input-to-photon measurements. The low-delay decoder
policy, explicit frame-thread override and software settings are covered by
`usb-test`, along with the transport regressions.

The console early-collection change preserves the 1080p pipeline and GPU
fences. The existing encode-completion callback is also polled while waiting
for a new game present, before selecting its capture slot, and after a full
capture before VIC conversion. It is NOT called between framebuffer-read
chunks: collection can block on USB, which would extend the window in which
the game might reuse a selected slot and cause torn captures.
`idlework_test.cpp` tests pending/ready, once-only collection and cancellation.
The console log labels the modified path and counts early collections.
This scheduling change does not alter CPU/GPU clocks, FPSLocker, dmnt backend,
capture fences, encoder quality, resolution or protocol.

Hardware validation of early collection: the new module booted on Atmosphere
1.11.2 / firmware 22.5.0, with its NPDM byte-identical to the installed release.
The viewer resumed after the console reboot without a viewer restart. The
console log showed 1200 early collections per 1200-frame window, 38.8-39.2 fps,
and the same dmnt backend. Later read times of 12.3-12.7 ms were close to the
13.1-13.7 ms baseline, while console age remained roughly 20-21 ms rather
than 40 ms. These are bounded successive-gameplay observations, not a
controlled scene, clock-matched benchmark or physical input-to-photon test.
