# support/

PlatformIO pre/post build scripts used only by the `simulator` environment (see `extra_scripts` in `platformio.ini`), plus a reference screenshot.

## Files

- **`sdl2_paths.py`** (pre-script) — locates SDL2's include/lib paths on the host and adds them to the build environment: Homebrew's Cellar on macOS (checks both Apple Silicon's `/opt/homebrew` and Intel's `/usr/local`), or an MSYS2 install (`C:\msys64\mingw64`) on Windows. Without this, the `simulator` env's `-lSDL2` in `platformio.ini` has nothing to link against on a fresh machine.
- **`sdl2_build_extra.py`** (post-script) — two things: propagates a `-m32` compile flag to the linker if present (SCons doesn't do this automatically for `-Wl,-m32`), and overrides PlatformIO's `upload` target (meaningless for a native build) to just execute the compiled simulator binary — this is what `pio run -e simulator -t exec` runs, and what shows up as the "Execute" custom target in PlatformIO's project explorer sidebar (see `pio_explorer.jpg`).
- **`pio_explorer.jpg`** — reference screenshot of the "Execute" custom target in the PlatformIO IDE's project explorer, for anyone looking for how to launch the simulator without the command line.

## Usage

These only run for `[env:simulator]` — `teensymm` doesn't reference `support/` at all. If SDL2 isn't found on a new machine, extend `sdl2_paths.py`'s platform-specific search rather than hardcoding a path in `platformio.ini`.
