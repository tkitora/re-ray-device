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

これから足すもの(`docs/DESIGN.md` §7.2): 操作、画面、折り返し、チャンネル、送るものの組み立て、JSON。

## テスト

```bash
make -C firmware/core test        # 474 項目(ASan + UBSan)
make -C firmware/core json-check  # JSON の形を Python で確かめる
make -C firmware/core guard-test  # 電波法のガードが、ビルドで拒否されることを確かめる
```
