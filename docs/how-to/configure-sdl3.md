# Configure the SDL3 frontend

These examples assume the SDL3 executable is built in `buildDir`. Replace the
ROM path in each command. Options affect this process; they are not saved back
to the configuration file.

## Select or remap a gamepad

Choose the SDL gamepad index for player 1:

```sh
./buildDir/src/app/mapperbus-sdl3 --gamepad 0 "/path/to/game.nes"
```

To use the left stick for the D-pad while keeping A on the east face button
and B on the south face button:

```sh
./buildDir/src/app/mapperbus-sdl3 \
    --gamepad-map 'a=east,b=south,up=lefty-,down=lefty+,left=leftx-,right=leftx+' \
    "/path/to/game.nes"
```

Set `--gamepad-deadzone 16000`, for example, if an analog stick drifts. The accepted
range is 1–32767. Each player combines their keyboard and gamepad input.

Player 2 uses gamepad index 1 by default. To select its device explicitly:

```sh
./buildDir/src/app/mapperbus-sdl3 --gamepad 0 --gamepad2 1 "/path/to/game.nes"
```

Use `--gamepad2-map` with the same mapping format to remap player 2.

For persistent bindings, open GUI Settings → Input and select **Player 1** or
**Player 2**. Click a keyboard or gamepad binding to change it; the device,
deadzone, enable switch, and default buttons apply to that player. Player 2's
keyboard starts unbound. Both frontends use these saved profiles.

You can also edit `[input.keyboard]` / `[input.gamepad]` for player 1 and
`[input.player2.keyboard]` / `[input.player2.gamepad]` for player 2 in
[mapperbus.conf](../reference/configuration.md) while the GUI is closed. If gamepad input was disabled there, set `enabled=true` before
using these commands.

## Select an upscaler

Choose a factor from 2 to 6 and one mode:

```sh
# CPU xBRZ
./buildDir/src/app/mapperbus-sdl3 --scale 3 "/path/to/game.nes"
# CPU FSR 1
./buildDir/src/app/mapperbus-sdl3 --scale 4 --fsr "/path/to/game.nes"
# GPU xBRZ
./buildDir/src/app/mapperbus-sdl3 --scale 3 --gpu "/path/to/game.nes"
# GPU FSR 1
./buildDir/src/app/mapperbus-sdl3 --scale 4 --gpu-fsr "/path/to/game.nes"
```

An upscaler flag needs `--scale` to activate upscaling at launch. Without an
available GPU compute path, GPU upscalers fall back to nearest-neighbor scaling.
For Vulkan shader build requirements, see [Build and install](build-and-install.md).

During play, use `2`–`6` to change factor, `0` or `1` for native scale, and `F9`
to cycle modes. `F11` toggles fullscreen and `V` toggles vsync.

## Override the region

```sh
./buildDir/src/app/mapperbus-sdl3 --region pal "/path/to/game.nes"
```

Use `ntsc`, `pal`, or `hybrid`. Omit the option to use automatic cartridge region
detection. The SDL3 startup path does not use the GUI's saved `frontend.region`
preference.

## Change audio for a session

For a 48 kHz output request with enhanced Famicom filtering:

```sh
./buildDir/src/app/mapperbus-sdl3 --sample-rate 48000 \
    --filter-mode enhanced --filter-profile famicom "/path/to/game.nes"
```

Use `--resampling cubic` for the alternative resampler, `--stereo` for
pseudo-stereo, `--dither` for dithering, or `--expansion-mixing resistance` for
the resistance-based expansion mix. The device may negotiate a different
sample rate.

For persistent audio settings, edit the `[audio]` section in
[mapperbus.conf](../reference/configuration.md), or use GUI Settings. See
[Commands and controls](../reference/commands-and-controls.md) for the full
option list and [Audio and video](../explanation/audio-and-video.md) for context.
