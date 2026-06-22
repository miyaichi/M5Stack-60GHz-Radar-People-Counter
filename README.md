# M5Stack 60GHz Radar People Counter

M5Stack Core ESP32 に 60GHz ミリ波レーダー（MS60-1211S80M）を接続し、センサー前を通過した人数をリアルタイムに表示するカウンターです。ESP-IDF (PlatformIO) で実装し、LCD ドライバも独自実装しています。

## ハードウェア構成

| 部品 | 型番 |
|------|------|
| マイコン | M5Stack Core ESP32 |
| レーダーセンサー | MoreSense MS60-1211S80M-Parking |
| 接続 | M5Stack 左側ピンソケット + 上部電源ピン |

### 配線

```
レーダー (Interface 1)        M5Stack (上部ピンソケット)
  VCC  ─────────────────────  5V
  GND  ─────────────────────  G

レーダー (Interface 2)        M5Stack (左側ピンソケット)
  TX (PB2)  ───────────────→  GPIO16  (UART2 RX)
  RX (PB3)  ←───────────────  GPIO17  (UART2 TX)  ← 省略可
  GND  ─────────────────────  GND
```

> **注意:** UART はクロス接続が必要です。レーダーの TX → M5Stack の RX (GPIO16)。

## センサー仕様（MS60-1211S80M）

| 項目 | 値 |
|------|----|
| 周波数 | 60GHz FMCW |
| 最大検出距離 | 3m（水平設置時） |
| ブラインドゾーン | 0.15m |
| 水平検出角度 | ±40°（80°） |
| 通信 | UART 115200bps |
| 推奨設置高さ | 0.5〜1.0m |

## UART プロトコル（解析結果）

センサーは以下の 14バイト固定長フレームを約 10Hz で出力します。

```
[AA][55][06][A2][06][00][DL][DH][AL][AH][EL][EH][SUM][00]
 header       固定ヘッダ   dist(cm)  az(0.01°) el(0.01°)  chk  tail
```

| フィールド | バイト | 型 | 単位 |
|-----------|--------|----|------|
| ヘッダ | [0][1] | - | `0xAA 0x55` |
| 距離 | [6][7] | uint16 LE | cm（0 = 未検出） |
| 方位角 | [8][9] | int16 LE | 0.01度（正=右、負=左） |
| 仰角 | [10][11] | int16 LE | 0.01度 |
| チェックサム | [12] | uint8 | `sum(byte[0..11]) & 0xFF` |

## ソフトウェア構成

```
src/
├── main.c        人数カウンター UI・ロジック
├── radar.c/h     UART フレームパーサー
├── ili9342c.c/h  SPI LCD ドライバ（独自実装）
└── font8x8.h     8×8 ビットマップフォント（LSB-first）
```

### LCD ドライバ (ILI9342C)

M5Stack Core の LCD（ILI9342C, 320×240, SPI）向けに C で独自実装しました。

- **SPI クロック:** 20MHz（ESP-IDF 6.0.1 の上限制約に合わせて設定）
- **カラー:** RGB565、SPI 転送時にバイトスワップ
- **MADCTL:** `0x08`（BGR モード）
- **フォント:** `font8x8.h` の LSB-first ビット順に合わせてレンダリング
- **スケール描画:** `lcd_draw_string_scaled()` で 1〜4× の拡大描画に対応

#### フォントのビット順

`font8x8.h` は **LSB-first**（bit0 = 左端ピクセル）のため、ビット抽出は `(glyph[row] & (1 << col))` で行います。

```c
// LSB-first: bit0 が左端
pixels[row * 8 + col] = (glyph[row] & (1 << col)) ? fg_be : bg_be;
```

### 人数カウンター

#### 検出ゾーン

| パラメータ | 値 | 説明 |
|-----------|-----|------|
| MIN_DIST | 0.15m | センサーブラインドゾーン限界 |
| MAX_DIST | 0.5〜3.0m | ボタンで調整可能（デフォルト 3.0m） |

#### ステートマシン

```
IDLE ──(3フレーム連続検出)──→ ENTERING ──→ TRACKING
                                               │
                              ←──(5フレーム連続未検出)──┘
                              │
                           カウント +1
                              │
                           EXITING ──(10フレーム後)──→ IDLE
```

#### 通過方向の判定

追跡中の方位角変化（開始時 vs 終了時）で左右方向を判定します。

```
az_delta = last_az - start_az
az_delta > +15°  → 左から右（← → 右）
az_delta < -15°  → 右から左（← 左）
```

## 画面レイアウト（320×240）

```
┌────────────────────────────────┐
│        PEOPLE COUNTER          │  白 / scale=2
│                                │
│              5                 │  緑 / scale=6（大型カウント）
│                                │
│   <- 2              3 ->       │  水色 / scale=2（方向別）
│                                │
│   0.32m  az:-15.3deg           │  灰 / scale=2（レーダー値）
│   >>> Detecting>>>             │  緑 / scale=2（状態）
│                                │
│   Max:3.0m                     │  黄 / scale=2（現在の最大距離）
│   [A]Reset [B]-0.5m [C]+0.5m  │  灰 / scale=1（ボタン説明）
└────────────────────────────────┘
```

## ボタン操作

| ボタン | GPIO | 機能 |
|--------|------|------|
| A（左） | 39 | カウントリセット |
| B（中央） | 38 | 最大検出距離 −0.5m |
| C（右） | 37 | 最大検出距離 +0.5m |

## ビルドと書き込み

### 前提

- PlatformIO インストール済み
- ESP-IDF 6.0.1

### 手順

```bash
# ビルド
/Users/miyaichi/.platformio/penv/bin/pio run

# 書き込み（M5Stack が認識されない場合は左前面ボタンA を押しながらリセット）
/Users/miyaichi/.platformio/penv/bin/pio run --target upload

# シリアルモニター
/Users/miyaichi/.platformio/penv/bin/pio device monitor
```

`platformio.ini` の設定：

```ini
[env:m5stack-core-esp32]
platform = espressif32
board = m5stack-core-esp32
framework = espidf
monitor_speed = 115200
upload_port = /dev/cu.usbserial-01CDD202
monitor_port = /dev/cu.usbserial-01CDD202
upload_speed = 921600
```

## 開発で判明した注意点

### SPI クロック制限
ESP-IDF 6.0.1 では SPI クロックの上限が 26.6MHz です。40MHz を指定しても LCD が初期化されないため、20MHz に設定しています。

### フォントのビット順
`font8x8.h` は LSB-first（bit0 = 左端）です。MSB-first（`0x80 >> col`）で描画すると文字が左右反転します。

### UART クロス接続
レーダーの TX ピンは M5Stack の RX ピン（GPIO16）に接続します。TX→TX の直結では受信できません。

### センサーの検出距離
このセンサーは車両検出用途で設計されています。人体（小さい RCS）の検出距離は設置環境により 30〜50cm 程度になることがあります。3m の検出距離を活かすには、センサーを壁面に固定し、通路を横切る方向（胴体の高さ）に向けることが重要です。

### M5Stack 書き込みモード
自動リセットが不安定な場合は、左前面ボタン（A）を押しながらサイドのリセットボタンを押すと書き込みモードに入れます。
