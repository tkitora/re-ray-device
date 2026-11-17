# re-ray-device

**Re-Ray(晩ご飯は type-C)の端末のファームウェア。** T-Beam 1 枚で動く。

Re-Ray は、自治体の職員が、ふだんの連絡にも災害のときにも同じ道具で使える連絡システム。
職員は、メガネに付ける小さな画面(GUIDE01)と、この端末を持つ。

```
              BLE                          LoRa 920MHz
 [GUIDE01] <------> [ T-Beam(この端末)] <----------------> ほかの端末
  表示・タッチ          |     ^
                Wi-Fi  |     |  ボタン 3 つ(戻る・選択・決定)、ピンマイク、乾電池
                       v     |
                  [サーバー]
```

| モード | 使い方 |
|---|---|
| 平常時 | トランシーバーのように使う。話した声をサーバーに送り、文字になって相手のグラスに出る |
| 緊急時 | 決まった信号と SOS を送る。Wi-Fi が使えないときは、端末どうしが LoRa で中継する |

サーバーと管理画面は、別のリポジトリ([Re-Ray_Server](https://github.com/yuki20060410/Re-Ray_Server))。

## いまの状態(2026-11-17)

**作り始めたところ。** 設計(`docs/DESIGN.md`)と、引き継いだ共通ロジック、実機の確認用ファームがある。

| できていること | まだのこと |
|---|---|
| LoRa のフレーム・署名・中継、ボタンの読み方、画面の進み方、チャンネル(PC のテスト 1,732 項目) | 画面を GUIDE01 に出すところ(実機) |
| T-Beam の電源・GPS・LoRa の受信・BLE の確認(実機) | Wi-Fi と MQTT、音声 |
| T-Beam から GUIDE01 への接続・表示・タッチ(実機) | ボタンとマイクの基板 |

**LoRa の電波は出さない設定になっている。** 送信前の電波確認などの実装(`docs/COMPLIANCE.md` §4.2)が済むまで、送信を有効にしない。

## 文書

| 知りたいこと | 読むところ |
|---|---|
| 何をどう作るか | `docs/DESIGN.md` |
| 実機で確かめた結果、ピン | `docs/FINDINGS.md` |
| 電波法について確かめること | `docs/COMPLIANCE.md` |
| サーバー担当に頼むこと | `docs/SERVER_REQUESTS.md` |
| 作業の決まり | `CLAUDE.md` |

## 動かし方

```bash
# PC のテスト(コンパイラがあれば動く)
make -C firmware/core test

# T-Beam のファーム(PlatformIO が要る)
#   先に sdk/README.md の手順で GUIDE01 の SDK を取ってくる
cd firmware/tbeam
pio run -e tbeam                 # ビルド
pio run -e tbeam -t upload       # 書き込み
pio device monitor -b 115200     # 画面を見る(? でできることの一覧)
```

Wi-Fi やサーバーの接続先は、`firmware/tbeam/include/secrets.example.h` をコピーした `secrets.h` に書く。
**`secrets.h` は git に入れない。このリポジトリは公開なので、パスワードをほかのファイルに書かない。**
