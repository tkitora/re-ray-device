# firmware/core/

ハードに触らないロジック。ヘッダだけの C++ で、ESP32 でも PC でも同じものを使う。**PC でテストしてから実機に載せる。**

| ヘッダ | 内容 |
|---|---|
| `reray/sha256.h` | SHA-256、HMAC-SHA256 |
| `reray/frame.h` | LoRa のフレームの組み立て・読み取り、署名(中継で変わる ttl は署名から外す) |
| `reray/dedup.h` | 重複の判定、番号の回り込み |
| `reray/airtime.h` | LoRa の送信時間の計算、送信時間の予算(**既定値は仮**。`docs/COMPLIANCE.md`) |
| `reray/tx_guard.h` | 電波法のガード。`RERAY_TX_ENABLE`(既定 0)、出力の上限(12dBm を超えるとビルド失敗) |
| `reray/node_id.h` | 端末 ID(`dev-001` ↔ 番号) |
| `reray/report_tracker.h` | 送ったものの状態(送信済み → 届いた / 送り直し / 届いていない) |
| `reray/relay_scheduler.h` | 中継の予約(優先度つきの待ち時間、同じものを聞いたら取り消し) |
| `reray/gateway.h` | 出口の処理。JSON に直す部分は、サーバーの方式(生のまま渡す)に合わせて作り直す予定 |
| `reray/radio_config.h` | LoRa の周波数・帯域幅・SF の固定(決めた値以外はビルド失敗)、送信前の電波確認の判定 |
| `reray/input.h` | 操作の種類。ボタン 3 つの読み方(チャタリング、離したときに出す、3 つ同時押し、決定の長押し)、タッチの読み方 |
| `reray/text_layout.h` | 文字の折り返し(UTF-8。半角 = 1、全角 = 2 で数える) |
| `reray/codes.h` | モードと、送れるものの一覧(定型信号、数量、方角) |
| `reray/channels.h` | チャンネルの一覧と、届いたメッセージの置き場 |
| `reray/ui.h` | 画面の状態遷移。操作を受けて、グラスに出す 4 行と、端末にしてほしい動作を決める |

これから足すもの(`docs/DESIGN.md` §7.2): MQTT の JSON の組み立てと読み取り。

## テスト

```bash
make -C firmware/core test        # 1,732 項目(ASan + UBSan)
make -C firmware/core json-check  # JSON の形を Python で確かめる
make -C firmware/core guard-test  # 電波法のガード: 上限を超える出力や、決めていない周波数では、ビルドが失敗することを確かめる
```

**注意:** ヘッダを書き換えた直後(同じ秒の中)に `make test` を続けて実行すると、`make` が「変わっていない」と判断して、前のプログラムを使い回すことがある。スクリプトから続けて実行するときは `make -B test`(必ず作り直す)を使う。
