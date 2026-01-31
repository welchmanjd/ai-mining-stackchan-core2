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
  - Cross-cutting policies (logging, feature flags)
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
  - Small utilities that do not own lifecycle

## Dependency Direction (Rule of Thumb)
- core -> ai/audio/ui/behavior/config/utils
- ai -> audio/config/utils
- ui -> config/utils
- behavior -> config/utils
- config/utils -> (no project-level deps)

Avoid reverse dependencies (e.g., ui -> ai) unless explicitly justified.

## Current File Mapping (Draft)
- core
  - main.cpp
  - orchestrator.cpp / orchestrator.h
  - runtime_features.cpp / runtime_features.h
  - logging.h
  - mc_log_limiter.cpp / mc_log_limiter.h
- ai
  - ai_interface.h
  - ai_talk_controller.cpp / ai_talk_controller.h
  - openai_llm.cpp / openai_llm.h
  - azure_stt.cpp / azure_stt.h
  - azure_tts.cpp / azure_tts.h
  - mining_task.cpp / mining_task.h
  - ai_sandbox_main.cpp
- audio
  - audio_recorder.cpp / audio_recorder.h
  - i2s_manager.cpp / i2s_manager.h
- ui
  - ui_mining_core2.cpp / ui_mining_core2.h
  - ui_mining_core2_text.cpp
  - ui_mining_core2_ticker_avatar.cpp
  - app_presenter.cpp / app_presenter.h
- behavior
  - stackchan_behavior.cpp / stackchan_behavior.h
- config
  - config.h
  - config_private.h
  - config_private.sample.h
  - user_config.h
  - mc_config_store.cpp / mc_config_store.h
- utils
  - mc_text_utils.cpp / mc_text_utils.h

## Configuration and Secrets
- `config_private.h` is not committed and must be sourced from
  `config_private.sample.h`.
- Runtime and persisted settings should be centralized in `config`.

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
