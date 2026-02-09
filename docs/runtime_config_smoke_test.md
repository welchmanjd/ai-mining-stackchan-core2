# Runtime Config Smoke Test

Target firmware: `m5stack-core2`
Baud: `115200`

## 1. Basic protocol

Send:
- `HELLO`
- `PING`
- `GET INFO`
- `GET CFG`

Expect:
- `@OK HELLO`
- `@OK PONG`
- `@INFO {...}`
- `@CFG {...}`

## 2. Toggle keys

Send:
- `SET wifi_enabled 1`
- `SET mining_enabled 1`
- `SET ai_enabled 1`
- `SAVE`
- `REBOOT`

Expect:
- `@OK SET wifi_enabled`
- `@OK SET mining_enabled`
- `@OK SET ai_enabled`
- `@OK SAVE`
- `@OK REBOOT`

## 3. Runtime text/number keys

Send:
- `SET display_sleep_s 90`
- `SET spk_volume 120`
- `SET openai_model gpt-5-nano`
- `SET openai_instructions あなたは短く日本語で答えてください。`
- `SET share_accepted_text シェア獲得！`
- `SET attention_text こんにちは`
- `SET hello_text ボタンBだよ`
- `SAVE`
- `REBOOT`

Expect:
- each `SET` returns `@OK SET <key>`
- `SAVE/REBOOT` succeed

## 4. Dependency rule check

Send:
- `SET wifi_enabled 0`
- `SAVE`
- `REBOOT`

Expect:
- Wi-Fi communication is disabled after reboot
- mining and AI behavior are effectively disabled

## 5. Validation checks

Send:
- `SET spk_volume 999`
- `SET display_sleep_s -1`
- `SET wifi_enabled 2`

Expect:
- `@ERR SET spk_volume range(0-255)`
- `@ERR SET display_sleep_s invalid_number`
- `@ERR SET wifi_enabled invalid_bool`
