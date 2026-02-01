# Config

This project uses compile-time macros for defaults plus a small runtime config store.

- Compile-time defaults live in `src/config/config.h`.
- Secrets live in `src/config/config_private.h` (not committed).
- User overrides live in `src/config/user_config.h` (optional).
- Persisted runtime values are stored by `src/config/mc_config_store.*`.

Related docs:
- `docs/serial_setup.md` (runtime setup protocol)

## Private config (required)
Copy the sample and fill in your secrets:

- `src/config/config_private.sample.h` -> `src/config/config_private.h`

Fields:
- Wi-Fi: `MC_WIFI_SSID`, `MC_WIFI_PASS`
- Duino-coin: `MC_DUCO_USER`, `MC_DUCO_MINER_KEY`
- Azure Speech: `MC_AZ_SPEECH_REGION`, `MC_AZ_SPEECH_KEY`, `MC_AZ_CUSTOM_SUBDOMAIN`
- OpenAI: `MC_OPENAI_API_KEY`

## Compile-time defaults (full list)
Override any of these in `src/config/user_config.h`.

General:
- `MC_DISPLAY_SLEEP_SECONDS` (default 60)
- `MC_TTS_ACTIVE_THREADS_DURING_TTS` (default 0)
- `MC_ATTENTION_TEXT` (default "Hi")
- `MC_SPK_VOLUME` (default 160)
- `MC_CPU_FREQ_MHZ` (default 240)
- `MC_AZ_TTS_VOICE` (default "ja-JP-AoiNeural")
- `MC_SPEECH_SHARE_ACCEPTED` (default: see `src/config/config.h`)
- `MC_SPEECH_HELLO` (default: see `src/config/config.h`)

AI talk (core):
- `MC_AI_TALK_ENABLED` (default 1)
- `MC_AI_IDLE_HINT_TEXT` (default "AI")
- `MC_AI_LISTENING_HINT_TEXT` (default `MC_AI_IDLE_HINT_TEXT`)
- `MC_AI_THINKING_HINT_TEXT` (default `MC_AI_IDLE_HINT_TEXT`)
- `MC_AI_SPEAKING_HINT_TEXT` (default `MC_AI_IDLE_HINT_TEXT`)
- `MC_AI_STT_DEBUG_SHOW_TEXT` (default 0)

AI talk (tap / touch):
- `MC_AI_TAP_AREA_TOP_HEIGHT_PX` (default 80)
- `MC_AI_TAP_DEBOUNCE_MS` (default 150)

AI talk (recording):
- `MC_AI_LISTEN_MAX_SECONDS` (default 10)
- `MC_AI_LISTEN_MIN_SECONDS` (default 3)
- `MC_AI_LISTEN_CANCEL_WINDOW_SEC` (default 3)
- `MC_AI_COUNTDOWN_UPDATE_MS` (default 250)

AI talk (recording time, derived):
- `MC_AI_LISTEN_TIMEOUT_MS` (default: `MC_AI_LISTEN_MAX_SECONDS` * 1000)
- `MC_AI_LISTEN_MIN_MS` (default: `MC_AI_LISTEN_MIN_SECONDS` * 1000)
- `MC_AI_LISTEN_CANCEL_WINDOW_MS` (default: `MC_AI_LISTEN_CANCEL_WINDOW_SEC` * 1000)

AI talk (PCM params):
- `MC_AI_REC_SAMPLE_RATE` (default 16000)
- `MC_AI_REC_MAX_SECONDS` (default `MC_AI_LISTEN_MAX_SECONDS`)
- `MC_AI_REC_SAVE_LAST_WAV` (default 0)

AI talk (cooldown):
- `MC_AI_COOLDOWN_MS` (default 2000)
- `MC_AI_COOLDOWN_ERROR_EXTRA_MS` (default 1000)

AI talk (timeouts):
- `MC_AI_STT_TIMEOUT_MS` (default 8000)
- `MC_AI_LLM_TIMEOUT_MS` (default 10000)
- `MC_AI_TTS_TIMEOUT_MS` (default 10000)
- `MC_AI_OVERALL_DEADLINE_MS` (default 20000)
- `MC_AI_OVERALL_MARGIN_MS` (default 250)
- `MC_AI_THINKING_MOCK_MS` (default 200)
- `MC_AI_POST_SPEAK_BLANK_MS` (default 500)
- `MC_AI_SIMULATED_SPEAK_MS` (default 2000)

AI talk (rate limits):
- `MC_AI_MAX_TALKS_PER_MIN` (default 6)
- `MC_AI_MAX_INPUT_CHARS` (default 200)
- `MC_AI_TTS_MAX_CHARS` (default 120)

AI talk (log head limits):
- `MC_AI_LOG_HEAD_BYTES_STT_TEXT` (default 30)
- `MC_AI_LOG_HEAD_BYTES_OVERLAY` (default 40)
- `MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT` (default 80)
- `MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG` (default 120)
- `MC_AI_LOG_HEAD_BYTES_LLM_DIAG` (default 180)

AI talk (TTS hard timeout):
- `MC_AI_TTS_HARD_TIMEOUT_BASE_MS` (default 25000)
- `MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS` (default 90)
- `MC_AI_TTS_HARD_TIMEOUT_MIN_MS` (default 20000)
- `MC_AI_TTS_HARD_TIMEOUT_MAX_MS` (default 60000)

AI talk (UI text):
- `MC_AI_TEXT_LISTENING` (default: see `src/config/config.h`)
- `MC_AI_TEXT_THINKING` (default: see `src/config/config.h`)
- `MC_AI_TEXT_CANCEL_HINT` (default: see `src/config/config.h`)
- `MC_AI_TEXT_COOLDOWN` (default: see `src/config/config.h`)
- `MC_AI_TEXT_FALLBACK` (default: see `src/config/config.h`)

AI talk (error text):
- `MC_AI_ERR_NET_UNSTABLE` (default: see `src/config/config.h`)
- `MC_AI_ERR_BUSY_TRY_LATER` (default: see `src/config/config.h`)
- `MC_AI_ERR_TEMP_FAIL_TRY_AGAIN` (default: see `src/config/config.h`)
- `MC_AI_ERR_SPEECH_KEY_CHECK` (default: see `src/config/config.h`)
- `MC_AI_ERR_SPEECH_REGION_CHECK` (default: see `src/config/config.h`)
- `MC_AI_ERR_SPEECH_QUOTA_MAYBE` (default: see `src/config/config.h`)
- `MC_AI_ERR_OPENAI_KEY_CHECK` (default: see `src/config/config.h`)
- `MC_AI_ERR_INPUT_TOO_LONG` (default: see `src/config/config.h`)
- `MC_AI_ERR_MIC_TOO_QUIET` (default: see `src/config/config.h`)
- `MC_AI_ERR_AUDIO_OUT_FAIL` (default: see `src/config/config.h`)

AI talk (sounds):
- `MC_AI_BEEP_FREQ_HZ` (default 880)
- `MC_AI_BEEP_DUR_MS` (default 80)
- `MC_AI_ERROR_BEEP_FREQ_HZ` (default 220)
- `MC_AI_ERROR_BEEP_DUR_MS` (default 200)

OpenAI:
- `MC_OPENAI_MODEL` (default "gpt-5-nano")
- `MC_OPENAI_ENDPOINT` (default "https://api.openai.com/v1/responses")
- `MC_OPENAI_MAX_OUTPUT_TOKENS` (default 1024)
- `MC_OPENAI_REASONING_EFFORT` (default "low")
- `MC_OPENAI_LOG_USAGE` (default 1)

See `src/config/config.h` for the authoritative source.

## Runtime config store
Some settings can be modified at runtime and persisted via the serial setup protocol.
The store is managed by `src/config/mc_config_store.*`.

Typical keys:
- `display_sleep_s`
- `attention_text`
- `spk_volume`
- `cpu_mhz`

These keys apply immediately when set via the serial protocol.

See `docs/serial_setup.md` for the setup protocol.
