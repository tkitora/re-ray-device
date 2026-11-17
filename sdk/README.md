# sdk/

GUIDE01 SDK をここに配置する。

## 取得元

https://git.happylifecreators.com/guide-series-sdk/guide01

(リポジトリは Apache License 2.0。SDK の README には、`libs/` のビルド済みバイナリも同じ Apache-2.0 の範囲と書かれている(2026-10-13 に確認)。ただし `sdk/guide01/` は `.gitignore` で git 管理外にしてあるので、このリポジトリには含めない。)

## 配置

```bash
cd re-ray-device/sdk
git clone https://git.happylifecreators.com/guide-series-sdk/guide01.git
```

## 確認すること

- `guide01/libs/esp32/xtensa/esp32/libguide01_protocol.a` がある(T-Beam は ESP32)
- `guide01/libs/include/guide01_protocol.h` がある

`firmware/tbeam/link_prebuilt.py` が、ビルドのときにこのライブラリをつなぐ。無いとビルドが止まり、この手順を案内する。

## 注意

- `sdk/` の中は書き換えない。
- SDK 本体はこのリポジトリに入れない(`.gitignore` 済み)。使う人が上の手順で取ってくる。
- 確かめた版: `783d69f`(2026-08-26、ライブラリ 2.0.0)。
