# Metal FSR 1 on macOS

The existing Upscaling setting and U key select Off / FSR 1 on macOS. FSR
is off by default, as on the other platforms. FSR sharpness has a 0-100% slider
below the mode selector (Left/Right: 5%, Enter/Backspace: reset to 70%).
F9 selects RCAS strength
40 / 70 / 100 percent and enables FSR; the choice is remembered as
`fsr_sharpness` in viewer.ini. The full stats overlay reports the actual
kernel and strength. NIS remains unavailable on macOS.

The filter changes the displayed picture only. Recordings retain the incoming
encoded video. Picture controls run before the filter, as on the other
platforms. Existing hardware-decoder settings and defaults are preserved.

## Rendering

The Metal backend accepts both planar YUV420P and interleaved NV12 with padded
rows. Source YUV is converted to source-sized RGBA16Float once. EASU runs once
per output pixel into a private RGBA16Float texture, followed by RCAS in SDL's
Metal render encoder. BT.709 limited-range conversion matches the existing
OpenGL path. At native size or downscaling the normal SDL path is used.

SDL exposes its Metal render encoder but not its command buffer. Conversion
and EASU therefore use a separate command queue, with a CPU completion wait
before SDL reads the output. This is an explicit synchronization cost, not
zero-copy or asynchronous presentation. Four output textures prevent overwrites
while SDL has drawables in flight; they do not queue four video frames. Cached
redraws reuse the last filtered output; resize invalidates it.

If EASU setup or execution fails, a cubic reconstruction with adaptive RCAS
is available and identified as Cubic in stats. If Metal itself is unavailable,
the caller uses its normal SDL texture. Renderer teardown clears all resources.
The Metal shader carries the AMD FSR 1 license and attribution.

## Verification

```sh
make -C tools/raw-recv raw-view metal-fsr-test
/tmp/frame-tap-metal-fsr-test
/tmp/frame-tap-metal-fsr-test --benchmark
bash tools/run_pc_tests.sh
bash tools/raw-recv/build-macos.sh fsr1-review
```

The GPU fixture requires a macOS GUI session and Metal. It checks NV12/IYUV,
padded and odd-sized input, black/white/RGB fields, texture rotation, cached
redraws, resize/fullscreen, actual U/F9 dispatch, saved settings and software
renderer fallback. It writes a generated-image UI screenshot under /tmp.
The benchmark requires a 2560x1440 drawable and measures upload, filter and
present over 120 frames after warmup. It is throughput, not physical latency.

Use real SDL 2 for UI validation, as the packaged macOS build does. On the
development host SDL2-compat (SDL 3 underneath) doubled the Retina scale in
the existing HUD/drawer calls. The UI bounds check intentionally exposes that
incompatibility. A fixture linked against the packaged SDL 2 passed; the
Metal color/filter checks also passed under SDL2-compat.

After the portable build, select that same SDL 2 for the fixture:

```sh
make -C tools/raw-recv metal-fsr-test \
  METAL_SDL_CONFIG="$PWD/ref/macos-$(uname -m)/sdl2/bin/sdl2-config"
/tmp/frame-tap-metal-fsr-test
```

Before upstream submission, also validate the packaged app and a live stream,
including switching Off/FSR, fullscreen, the settings drawer and actual NV12
hardware decode. Report initial shader compilation separately from steady
state, and keep private gameplay captures out of the repository.

References:

- https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
- https://wiki.libsdl.org/SDL2/SDL_RenderGetMetalCommandEncoder
