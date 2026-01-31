# AGENTS.md

## Project context
- PlatformIO + Arduino (ESP32, M5Stack Core2).
- Default build env: `m5stack-core2` (see `platformio.ini`).

## Build / flash
- Build: `pio run -e m5stack-core2`
- Upload: `pio run -t upload -e m5stack-core2`
- Monitor: `pio device monitor -b 115200`

## Coding conventions
- Keep headers minimal: prefer forward declarations and move heavy includes to `.cpp`.
- Use existing log macros (`MC_LOG*`, `LOG_EVT_*`) instead of raw `Serial`.
- Avoid dynamic allocation in hot paths; prefer fixed buffers where practical.
- Include order:
  - `.cpp`: corresponding header first, then C/C++ standard headers, then external/platform headers, then project headers.
  - `.h`: C/C++ standard headers first, then external/platform headers, then project headers.
  - Separate groups with a single blank line.

## Config / secrets
- Private values live in `src/config/config_private.h` (local, not committed).
- Update `src/config/config_private.sample.h` only when adding new fields.
