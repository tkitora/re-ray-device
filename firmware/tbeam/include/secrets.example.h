// ひな形。コピーして secrets.h を作り、値を入れる。
//   cp secrets.example.h secrets.h
// secrets.h は git に入らない(.gitignore)。このリポジトリは公開なので、ここ(ひな形)に本物の値を書かない。
#pragma once

// この端末の番号(1〜65534)。サーバーでは dev-001 の形になる。端末ごとに変える
#define RERAY_NODE_ID 1

// Wi-Fi
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// サーバー(MQTT と HTTP)
#define RERAY_USE_TLS 0                 // 0 = 暗号化なし(同じ LAN の中で試すとき)、1 = 暗号化あり
#define MQTT_HOST     "192.168.0.10"    // 試すときは PC の IP。自宅サーバーにつなぐときはそのホスト名
#define MQTT_PORT     1883
#define MQTT_USER     ""                // 認証なしのときは空
#define MQTT_PASSWORD ""
#define HTTP_BASE_URL "http://192.168.0.10:3000"   // 音声の送り先(/api/voice)

// LoRa のフレームの署名に使う鍵。開発用のダミー(本番では別の鍵にする)。16 バイト
#define RERAY_HMAC_KEY { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F }
