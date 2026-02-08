# Serial Setup Protocol

This project exposes a simple line-based serial protocol for setup and diagnostics.
All commands are plain text, one per line. Responses are prefixed with `@OK`, `@ERR`, or data tags.

Related docs:
- `docs/config.md` (compile-time and runtime config overview)

## Connection
- Baud: 115200
- Newline: `\n` (CR is ignored)

## Commands

### HELLO
- Request: `HELLO`
- Response: `@OK HELLO`

### PING
- Request: `PING`
- Response: `@OK PONG`

### HELP
- Request: `HELP`
- Response: `@OK CMDS=HELLO,PING,GET INFO,GET CFG,SET,SAVE,REBOOT,AZTEST,HELP`

### GET INFO
- Request: `GET INFO`
- Response: `@INFO {"app":"<name>","ver":"<version>","baud":115200}`

### GET CFG
- Request: `GET CFG`
- Response: `@CFG { ... }` (masked JSON with config values)

### SET
- Request: `SET <KEY> <VALUE>`
- Response (success): `@OK SET <KEY>`
- Response (error): `@ERR SET <KEY> <reason>`

Supported keys:
- `wifi_enabled` (`0|1`)
- `mining_enabled` (`0|1`)
- `ai_enabled` (`0|1`)
- `openai_instructions`
- `display_sleep_s`
- `openai_model`
- `spk_volume`
- `share_accepted_text`
- `attention_text`
- `hello_text`
- `wifi_ssid`
- `wifi_pass`
- `duco_user`
- `duco_miner_key`
- `az_speech_region`
- `az_speech_key`
- `az_tts_voice`
- `az_custom_subdomain`
- `openai_key`
- `cpu_mhz`

Keys with immediate runtime effect:
- `display_sleep_s`
- `attention_text`
- `spk_volume`
- `cpu_mhz`

Dependency rule:
- If `wifi_enabled=0`, effective mining/AI features are also disabled.

### SAVE
- Request: `SAVE`
- Response (success): `@OK SAVE`
- Response (error): `@ERR SAVE <reason>`

### REBOOT
- Request: `REBOOT`
- Response: `@OK REBOOT` (then device restarts)

### AZTEST
- Request: `AZTEST`
- Response: `@AZTEST OK`
- Response (error): `@AZTEST NG <reason>`

Notes:
- Requires Wi-Fi connection and Azure TTS credentials.
- Reloads runtime Azure config before testing.

## Error handling
Unknown commands return:
- `@ERR unknown_cmd: <original line>`

## Implementation reference
- `src/core/serial_setup.cpp` (search for `handleSetupLine_`)
- `src/core/serial_setup.h`
