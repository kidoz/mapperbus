# Audio and video

An emulated NES produces timed hardware activity. A desktop host consumes
frames and audio buffers on its own schedule. MapperBus keeps these two kinds
of timing separate so presentation choices do not redefine the machine.

## From CPU cycles to sound

The CPU's progress clocks the PPU, APU, and relevant mapper hardware. The APU
combines the five base channels with expansion audio and FDS output. This
produces a signal at emulated timing, which must be converted into samples
for a host audio device.

The default BlipBuffer path uses band-limited step synthesis; a cubic Hermite
resampler is also available. Filtering applies either the hardware-oriented
chain, an enhanced Butterworth chain, or an unfiltered path. NES and Famicom
profiles describe different output characteristics. Dithering, pseudo-stereo,
and the expansion mixing mode are additional output choices.

```text
CPU / mapper / FDS activity
    → APU mixing
    → resampling and output processing
    → sample ring buffer
    → EmulationSession
    → host audio backend and device
```

The device's clock and the emulator's clock can drift. Dynamic rate control
uses buffer fill feedback to adjust the output rate within a small range.
The session also uses audio queue watermarks to avoid advancing indefinitely
when the host has enough queued sound. This is why sound buffering participates
in pacing rather than being an unrelated final write.

When a device negotiates a sample rate different from the requested rate, the
session reconciles the audio settings with the actual device rate. A requested
96 kHz setting therefore does not guarantee that a platform audio server uses
96 kHz internally.

## From PPU pixels to a window

The PPU produces the core framebuffer. Upscaling transforms that image for
presentation; it does not change the PPU's resolution or emulation rules.
CPU xBRZ and FSR run without a compute device. GPU implementations move that
processing to Metal or Vulkan when available.

The SDL3 video backend owns host presentation. GPU FSR uses EASU and RCAS
processing and can present a GPU result without a CPU readback. Vulkan shaders
can be compiled and embedded during the build, so shader tool availability is
both a build concern and a runtime capability concern.

Selecting a GPU mode does not prove the compute path is available. The
upscalers can fall back to nearest-neighbor scaling, and GPU tests can skip
unsupported cases. Explicit GPU validation is therefore useful when checking
the accelerated path.

## Region timing is a machine setting

NTSC, PAL, and Dendy select different machine timing. They affect frame timing
and audio generation. Upscaling, fullscreen, and vsync are host presentation
settings. This separation lets the same ROM timing feed different displays
without treating a display's refresh rate as the emulated console's region.

For exact settings, see [Configuration](../reference/configuration.md). For
commands, see [Configure SDL3](../how-to/configure-sdl3.md); for coverage, see
[GPU validation](../how-to/test-and-diagnose.md#validate-a-gpu-backend).

Implementation starting points: [APU](../../src/core/apu/apu.cpp),
[session](../../src/app/emulation_session.cpp), and
[SDL3 video](../../src/frontends/sdl3/sdl3_video.cpp).
