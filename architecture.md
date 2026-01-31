# Architecture (Draft)

This document is a lightweight guide to keep structure, responsibilities, and
dependencies consistent as the project grows.

## Scope
- Covers: module boundaries, responsibilities, dependency direction, extension points.
- Out of scope: low-level implementation details and per-function specs.

## High-Level Layout (Proposed)
```
/src
  /core        // startup, orchestration, common policies
  /ai          // LLM, STT, TTS, dialogue control, mining task
  /audio       // recording and I2S
  /ui          // rendering and presenters
  /behavior    // Stackchan behavior control
  /config      // settings, secrets, persistence
  /utils       // small helpers (text, etc.)
```

## Components and Responsibilities (Draft)
- core
  - System startup and lifecycle
  - Orchestration between subsystems
  - Cross-cutting policies (feature flags, timeouts)
- ai
  - Dialogue control and AI integration
  - LLM/STT/TTS providers
  - Mining task flow
- audio
  - Input/output audio plumbing
  - I2S configuration and lifecycle
- ui
  - UI screens, presenters, avatars, tickers
- behavior
  - Stackchan motion/behavior rules
- config
  - Configuration schemas and persistence
  - Private keys and user overrides
- utils
  - Small utilities that do not own lifecycle (text, logging, shared DTOs)

## Dependency Direction (Rule of Thumb)
- core -> ai/audio/ui/behavior/config/utils
- ai -> audio/config/utils
- ui -> config/utils
- behavior -> config/utils
- config/utils -> (no project-level deps)

Avoid reverse dependencies (e.g., ui -> ai) unless explicitly justified.

## Dependency Exceptions (Allowed Cases)
- ui -> ai
  - OK for read-only state exposure via a narrow interface (e.g., view-model or DTO).
  - Not OK to call AI actions or business logic directly from UI.
- behavior -> ai
  - OK for consuming high-level intent/state (e.g., "speaking", "idle").
  - Not OK for invoking AI providers directly.
- config -> (none)
  - config remains dependency-free at the project layer.
- utils -> (none)
  - utils stays stateless and free of project-level deps.

## Current File Mapping (Draft)
Status: config, utils, audio, ai, core, ui, and behavior are already moved under `/src` subfolders.
- core
  - core/main.cpp
  - core/orchestrator.cpp / core/orchestrator.h
- ai
  - ai/ai_interface.h
  - ai/ai_talk_controller.cpp / ai/ai_talk_controller.h
  - ai/openai_llm.cpp / ai/openai_llm.h
  - ai/azure_stt.cpp / ai/azure_stt.h
  - ai/azure_tts.cpp / ai/azure_tts.h
  - ai/mining_task.cpp / ai/mining_task.h
- audio
  - audio/audio_recorder.cpp / audio/audio_recorder.h
  - audio/i2s_manager.cpp / audio/i2s_manager.h
- ui
  - ui/ui_mining_core2.cpp / ui/ui_mining_core2.h
  - ui/ui_mining_core2_text.cpp
  - ui/ui_mining_core2_ticker_avatar.cpp
  - ui/app_presenter.cpp / ui/app_presenter.h
- behavior
  - behavior/stackchan_behavior.cpp / behavior/stackchan_behavior.h
- config
  - config/config.h
  - config/config_private.h
  - config/config_private.sample.h
  - config/user_config.h
  - config/mc_config_store.cpp / config/mc_config_store.h
  - config/runtime_features.cpp / config/runtime_features.h
- utils
  - utils/logging.h
  - utils/mc_log_limiter.cpp / utils/mc_log_limiter.h
  - utils/mc_text_utils.cpp / utils/mc_text_utils.h
  - utils/mining_panel_data.h
  - utils/mining_status.h
  - utils/mining_summary.h
  - utils/orchestrator_api.h

## Configuration and Secrets
- `config_private.h` is not committed and must be sourced from
  `config_private.sample.h`.
- Runtime and persisted settings should be centralized in `config`.

## Naming Rules (Code Style)
### Constants
- File-scope constants use `k` prefix + UpperCamelCase: `kDisplaySleepMessageMs`.
- Compile-time config macros keep `MC_` prefix + ALL_CAPS: `MC_AI_TALK_ENABLED`.
- Enum constants follow the enum’s naming style; avoid mixing styles inside one enum.

### Statics
- File-scope mutable statics use `g_` prefix: `g_tts`, `g_displaySleepTimeoutMs`.
- Function-scope statics use `s_` prefix: `s_lastUiHbMs`.

### Variables and Functions
- Local variables: lowerCamelCase: `lastInputMs`, `wifiConnected`.
- Member variables: lowerCamelCase with trailing underscore: `last_input_ms_` is **not** used; use `lastInputMs_`.
- Functions and methods: lowerCamelCase verbs: `updateMiningSummary`, `setMiningPaused`.
- Private helper functions at file scope use trailing underscore: `handleSetupLine_`.

### Types (class/struct/enum/typedef/using)
- Types use UpperCamelCase: `AiTalkController`, `MiningSummary`.
- Type aliases (`using`) use UpperCamelCase: `using DeviceId = uint32_t;`.
- Enums use UpperCamelCase names: `enum class AppState`.

### Enum Values
- Prefer UpperCamelCase for `enum class` values: `Idle`, `Thinking`, `Speaking`.
- If a legacy enum uses ALL_CAPS, keep it consistent within that enum (no mixed styles).

### Namespaces
- Use lower_snake_case for namespaces: `namespace azure_stt { ... }`.
- Keep namespaces short and domain-scoped.

### Macros
- Macros are ALL_CAPS with project prefix: `MC_AI_TALK_ENABLED`.
- Avoid new macros when `constexpr` or `enum` can be used.

### Files and Folders
- Folders are lower_snake_case: `core`, `ai`, `audio`.
- Files are lower_snake_case with optional `mc_` prefix for shared utilities: `mc_text_utils.cpp`.
- Avoid mixed casing in file names.

### Config Keys (JSON / WebSerial / Stored)
- Keys are lower_snake_case: `display_sleep_s`, `attention_text`.
- Use stable keys; avoid renaming without a migration path.

### Log Tags / Event Names
- Tags are ALL_CAPS short tokens: `MAIN`, `AI`, `TTS`, `UI`.
- Event names are ALL_CAPS with module prefix: `EVT_AI_STATE`, `EVT_PRESENT_TTS_START`.

### Templates / Traits / Compile-Time Helpers
- Template type names follow UpperCamelCase; template parameters use `T`/`U`/`T*` style: `template <typename T>`.
- Traits names end with `Trait` or `Traits`: `AudioDeviceTraits`.

## Extension Points
- Add a new AI provider: place in `/src/ai` and expose a narrow interface.
- Add a new UI screen: place in `/src/ui`, keep presenters thin.
- Add a new behavior module: place in `/src/behavior`.

## Migration Plan (Draft)
- Phase 0: document-only
  - Agree on folder names and dependency direction.
  - Keep all files in `/src` while the plan is validated.
- Phase 1: non-invasive grouping
  - Create subfolders and move headers/sources that have fewest includes.
  - Update includes with minimal path changes.
  - Keep public headers stable where possible.
- Phase 2: core wiring
  - Move `main.cpp` and `orchestrator.*` after includes are stable.
  - Adjust include paths once, then fix compile errors in a single pass.
- Phase 3: UI and behavior
  - Move UI/behavior last to avoid breaking runtime flow while iterating.
- Phase 4: cleanup
  - Remove dead includes, fix relative paths, and normalize include style.

## Execution Plan (Draft)
1) Prep
   - Freeze feature changes during the move window.
   - Create a short-lived branch for directory migration.
2) Skeleton
   - Create folders under `/src` with empty `.keep` files if needed.
   - Add/update build include paths if required by the toolchain.
3) Low-risk moves
   - Move `utils`, `config`, and `audio` first.
   - Fix includes immediately after each move; compile if possible.
4) AI layer
   - Move `ai` components together to avoid partial dependency breaks.
   - Run a quick build to catch missing includes.
5) Core + wiring
   - Move `core` (`main.cpp`, `orchestrator.*`) last among core parts.
   - Resolve any dependency direction violations.
6) UI + behavior
   - Move `ui` and `behavior` with final include normalization.
7) Stabilize
   - Clean up include styles, remove unused headers.
   - Update docs and file mapping to match the final structure.

## Open Items
- Confirm final folder split and file moves.
- Decide naming rules (e.g., `mc_` prefix scope).
- Confirm logging policy and compile-time feature flags.
