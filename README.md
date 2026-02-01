# AI Mining Stackchan Core2

ESP32 (M5Stack Core2) mining dashboard + Stackchan avatar + AI speech.

ESP32 (M5Stack Core2) 向けのマイニングダッシュボード + スタックチャン表示 + AI 音声。

## Quick Start / クイックスタート
- Copy `src/config/config_private.sample.h` to `src/config/config_private.h` and fill secrets.
- Build: `pio run -e m5stack-core2`
- Upload: `pio run -t upload -e m5stack-core2`
- Monitor: `pio device monitor -b 115200`

- `src/config/config_private.sample.h` を `src/config/config_private.h` にコピーして秘密情報を記入します。
- ビルド: `pio run -e m5stack-core2`
- 書き込み: `pio run -t upload -e m5stack-core2`
- モニタ: `pio device monitor -b 115200`

## Docs / ドキュメント
- `architecture.md` : module layout + dependency direction / モジュール構成と依存方向
- `docs/config.md` : config macros and runtime settings / 設定マクロとランタイム設定
- `docs/serial_setup.md` : serial setup protocol / シリアル設定プロトコル

## Repository Layout / 構成
- `src/core` : startup + orchestration / 起動とオーケストレーション
- `src/ai` : LLM / STT / TTS / dialogue
- `src/audio` : I2S + recording / 音声入出力
- `src/ui` : UI + presenter
- `src/behavior` : Stackchan behavior / 振る舞い制御
- `src/config` : settings + secrets + persistence / 設定と永続化
- `src/utils` : small helpers / 小さなユーティリティ

## Configuration / 設定
- Secrets live in `src/config/config_private.h` (not committed).
- Optional overrides: `src/config/user_config.h`.
- Runtime key/value store: `src/config/mc_config_store.*`.

- 秘密情報は `src/config/config_private.h` に保存します（コミットしません）。
- 任意の上書き設定は `src/config/user_config.h` を使います。
- ランタイム設定の永続化は `src/config/mc_config_store.*` です。

## Build Environment / ビルド環境
- PlatformIO + Arduino
- Default env: `m5stack-core2`

- PlatformIO + Arduino
- 既定環境: `m5stack-core2`
