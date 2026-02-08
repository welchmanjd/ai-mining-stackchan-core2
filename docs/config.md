# Config

This project uses compile-time defaults plus a small runtime config store.

- Defaults: `src/config/config.h`
- User overrides: `src/config/user_config.h`
- Secrets: `src/config/config_private.h` (not committed)
- Runtime store: `src/config/mc_config_store.*`

Related docs:
- `docs/serial_setup.md`

## Private config (required)
Copy the sample and fill in your secrets:

- `src/config/config_private.sample.h` -> `src/config/config_private.h`

Secrets:
- Wi-Fi: `MC_WIFI_SSID`, `MC_WIFI_PASS`
- Duino-coin: `MC_DUCO_USER`, `MC_DUCO_MINER_KEY`
- Azure Speech: `MC_AZ_SPEECH_REGION`, `MC_AZ_SPEECH_KEY`, `MC_AZ_CUSTOM_SUBDOMAIN`
- OpenAI: `MC_OPENAI_API_KEY`

## User-tunable config (recommended)
Edit `src/config/user_config.h` for things users often change:

- Display sleep: `MC_DISPLAY_SLEEP_SECONDS`
- Speaker volume: `MC_SPK_VOLUME`
- Attention text: `MC_ATTENTION_TEXT`
- Speech lines: `MC_SPEECH_SHARE_ACCEPTED`, `MC_SPEECH_HELLO`
- AI overlay hints: `MC_AI_IDLE_HINT_TEXT`, `MC_AI_LISTENING_HINT_TEXT`, `MC_AI_THINKING_HINT_TEXT`, `MC_AI_SPEAKING_HINT_TEXT`
- AI overlay text: `MC_AI_TEXT_THINKING`, `MC_AI_TEXT_COOLDOWN`, `MC_AI_TEXT_FALLBACK`
- Azure voice: `MC_AZ_TTS_VOICE`
- OpenAI instructions: `MC_OPENAI_INSTRUCTIONS`

See `src/config/config.h` for the full, authoritative list of defaults.

## Runtime config store
Some settings can be modified at runtime and persisted via the serial setup protocol.

Typical keys:
- `wifi_enabled`
- `mining_enabled`
- `ai_enabled`
- `openai_instructions`
- `display_sleep_s`
- `openai_model`
- `attention_text`
- `spk_volume`
- `share_accepted_text`
- `hello_text`
- `cpu_mhz`

Some keys apply immediately (`display_sleep_s`, `attention_text`, `spk_volume`, `cpu_mhz`).
Others are used on subsequent feature checks and/or after reboot. See `docs/serial_setup.md`.
