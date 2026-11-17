// T-Beam V1.2 (AXP2101) 立ち上げ確認用ファーム(電波を出さない確認だけ)。
//
// ★ このファームは LoRa の送信をしない。ソースに transmit / startTransmit の呼び出しを書いていないうえ、
//   RERAY_TX_ENABLE=1 でビルドすると失敗する(下の static_assert)。受信と、電波の強さの観測だけを行う。
//   送信は docs/COMPLIANCE.md の確認欄(§4)が全て埋まるまで作らない(CLAUDE.md)。
//
// ピン・電源の根拠は docs/FINDINGS.md §4。電源の設定は公式 LoRaBoards.cpp(LilyGo-LoRa-Series 1991b1e)の
// AXP2101 + ESP32 の分岐に合わせた。2026-11-06 に実機で確認済み。結果は docs/FINDINGS.md に書く。
//
// シリアル(115200)から 1 文字で操作する:
//   x システム情報  i I2C スキャン  p 電源(PMU)  g GPS(30秒)  k ボタンと LED
//   r LoRa チップの確認  l 受信待ち(60秒。電波は出さない)  e 電波の強さの観測(30秒)
//   b BLE のパッシブスキャン(10秒。聞くだけで送信しない)  ? ヘルプ
//   GUIDE01(BLE): S 接続  B 電池  T 3行表示  N 通知  C 閉じる  U 100回更新  J 日本語  W 桁数  D 切断  K 画面の大きさ  Z サイズ別の桁数
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <RadioLib.h>
#include <TinyGPS++.h>

#include <NimBLEDevice.h>
#include <XPowersLib.h>

#include "guide01_test.h"
#include "reray/tx_guard.h"

static_assert(!reray::txEnabled(), "この確認用ファームは送信しない。RERAY_TX_ENABLE=1 ではビルドしない");

// ---- ピン(docs/FINDINGS.md §4。公式 utilities.h の T-Beam ブロック)----
static const int PIN_RADIO_SCK = 5;
static const int PIN_RADIO_MISO = 19;
static const int PIN_RADIO_MOSI = 27;
static const int PIN_RADIO_CS = 18;
static const int PIN_RADIO_DIO0 = 26;
static const int PIN_RADIO_RST = 23;
static const int PIN_RADIO_DIO1 = 33;
static const int PIN_GPS_RX = 34;  // ESP32 の RX ← GPS の TX
static const int PIN_GPS_TX = 12;
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const int PIN_BUTTON = 38;
static const int PIN_LED = 4;  // LOW で点灯

// 受信専用で使う周波数。電波は出さないので規制の対象外だが、のちの送信チャンネルの候補(ch37 = 923.2MHz)に合わせた。
static const float RX_FREQ_MHZ = 923.2f;

static XPowersLibInterface *pmu = nullptr;  // 公式と同じく基底クラスのポインタ経由で操作する
static bool pmuOk = false;
static SX1276 radio = new Module(PIN_RADIO_CS, PIN_RADIO_DIO0, PIN_RADIO_RST, PIN_RADIO_DIO1, SPI);
static bool radioOk = false;
static TinyGPSPlus gps;
static volatile bool rxFlag = false;

static void IRAM_ATTR onRx() { rxFlag = true; }

// ---- システム情報 ----
static void sysinfo() {
  Serial.println("--- システム情報");
  Serial.printf("チップ: %s rev%d コア数%d\n", ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getChipCores());
  Serial.printf("フラッシュ: %u バイト  PSRAM: %u バイト\n", (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize());
  Serial.printf("MAC(基板の識別): %012llX\n", (unsigned long long)ESP.getEfuseMac());
  Serial.printf("空きヒープ: %u\n", (unsigned)ESP.getFreeHeap());
}

// ---- I2C スキャン ----
static void i2cScan() {
  Serial.println("--- I2C スキャン(SDA=21, SCL=22)。期待: 0x34=AXP2101(PMU)、0x3C=OLED(Non-Soldered 版は無い)");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X に応答\n", a);
      n++;
    }
  }
  Serial.printf("応答 %d 件\n", n);
}

// ---- 電源(PMU) ----
static bool pmuBegin() {
  if (pmuOk) return true;
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pmu = new XPowersAXP2101(Wire);
  if (!pmu->init()) {
    delete pmu;
    pmu = nullptr;
    Serial.println("[pmu] AXP2101 が見つからない。V1.1(AXP192)の可能性。基板の表記を確認(COMPLIANCE #1)");
    return false;
  }
  // 公式 LoRaBoards.cpp の AXP2101 + ESP32 分岐と同じ。DCDC1(ESP32 本体)は触らない。
  pmu->disablePowerOutput(XPOWERS_DCDC2);
  pmu->disablePowerOutput(XPOWERS_DCDC3);
  pmu->disablePowerOutput(XPOWERS_DCDC4);
  pmu->disablePowerOutput(XPOWERS_DCDC5);
  pmu->disablePowerOutput(XPOWERS_ALDO1);
  pmu->disablePowerOutput(XPOWERS_ALDO4);
  pmu->disablePowerOutput(XPOWERS_BLDO1);
  pmu->disablePowerOutput(XPOWERS_BLDO2);
  pmu->disablePowerOutput(XPOWERS_DLDO1);
  pmu->disablePowerOutput(XPOWERS_DLDO2);
  pmu->disablePowerOutput(XPOWERS_CPULDO);
  pmu->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);  // GNSS の RTC
  pmu->enablePowerOutput(XPOWERS_VBACKUP);
  pmu->setProtectedChannel(XPOWERS_DCDC1);
  pmu->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);  // LoRa
  pmu->enablePowerOutput(XPOWERS_ALDO2);
  pmu->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);  // GNSS
  pmu->enablePowerOutput(XPOWERS_ALDO3);
  pmu->enableSystemVoltageMeasure();
  pmu->enableVbusVoltageMeasure();
  pmu->enableBattVoltageMeasure();
  pmuOk = true;
  delay(100);  // 電源が立ち上がるのを待つ
  return true;
}

static void pmuReport() {
  Serial.println("--- 電源(PMU)");
  if (!pmuBegin()) return;
  Serial.printf("チップ: %s\n", pmu->getChipModel() == XPOWERS_AXP2101 ? "AXP2101" : "AXP2101 以外");
  Serial.printf("USB(VBUS): %s %u mV\n", pmu->isVbusIn() ? "接続" : "なし", (unsigned)pmu->getVbusVoltage());
  Serial.printf("システム電圧: %u mV\n", (unsigned)pmu->getSystemVoltage());
  if (pmu->isBatteryConnect()) {
    Serial.printf("電池: 接続  %u mV  %d%%  %s\n", (unsigned)pmu->getBattVoltage(), (int)pmu->getBatteryPercent(),
                  pmu->isCharging() ? "充電中" : "充電していない");
  } else {
    Serial.println("電池: 接続なし(18650 が入っていない、または向きが逆)");
  }
}

// ---- GPS ----
static void gpsRun(uint32_t seconds) {
  Serial.printf("--- GPS(%u 秒)。屋外でないと位置は取れない。NMEA が流れてくれば配線と電源は OK\n", (unsigned)seconds);
  if (!pmuBegin()) return;
  static bool begun = false;
  if (!begun) {
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    begun = true;
  }
  uint32_t start = millis(), lastPrint = 0;
  uint32_t shown = 0;
  while (millis() - start < seconds * 1000UL) {
    while (Serial1.available()) {
      char c = (char)Serial1.read();
      gps.encode(c);
      if (shown < 3 && c == '\n') shown++;  // 最初の数行の生の文字列も見せる
      if (shown < 3) Serial.write(c);
    }
    if (millis() - lastPrint >= 5000) {
      lastPrint = millis();
      Serial.printf("\n[gps] 受信文字 %lu  文法OK %lu  文法NG %lu  位置の文 %lu  衛星 %u  位置 %s\n",
                    (unsigned long)gps.charsProcessed(), (unsigned long)gps.passedChecksum(),
                    (unsigned long)gps.failedChecksum(), (unsigned long)gps.sentencesWithFix(),
                    (unsigned)(gps.satellites.isValid() ? gps.satellites.value() : 0),
                    gps.location.isValid() ? "あり" : "なし");
      if (gps.location.isValid())
        Serial.printf("      lat=%.6f lon=%.6f 時刻(UTC)=%02d:%02d:%02d\n", gps.location.lat(), gps.location.lng(),
                      gps.time.hour(), gps.time.minute(), gps.time.second());
    }
    delay(2);
  }
  if (gps.charsProcessed() == 0)
    Serial.println("[gps] 1 文字も来ない: GPS の電源(ALDO3)か配線、またはボーレートを疑う");
}

// ---- ボタンと LED ----
static void buttonLed() {
  Serial.println("--- ボタン(GPIO38)と LED(GPIO4、LOW で点灯)。LED を 3 回点滅 → 10 秒間ボタンを見る");
  pinMode(PIN_LED, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(250);
    digitalWrite(PIN_LED, HIGH);
    delay(250);
  }
  pinMode(PIN_BUTTON, INPUT);
  int last = digitalRead(PIN_BUTTON);
  Serial.printf("ボタンの今の状態: %d(押していない時の値を確認)。10 秒間、変化を表示する\n", last);
  uint32_t start = millis();
  while (millis() - start < 10000) {
    int v = digitalRead(PIN_BUTTON);
    if (v != last) {
      Serial.printf("  ボタン %d → %d (%lu ms)\n", last, v, (unsigned long)(millis() - start));
      last = v;
    }
    delay(5);
  }
}

// ---- LoRa(受信専用) ----
static bool radioBegin() {
  if (radioOk) return true;
  if (!pmuBegin()) return false;  // LoRa の電源(ALDO2)を入れる
  SPI.begin(PIN_RADIO_SCK, PIN_RADIO_MISO, PIN_RADIO_MOSI, PIN_RADIO_CS);
  // begin の power 引数は、出力設定の値を渡すだけ(送信はしない)。tx_guard を通して上限を守る。
  const reray::TxPower pw = reray::clampTxDbm(reray::defaultTxDbm());
  int16_t st = radio.begin(RX_FREQ_MHZ, 125.0, 7, 5, RADIOLIB_SX127X_SYNC_WORD, pw.dbm, 8, 0);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] begin 失敗 code=%d(-2 は SX127x が見つからない。SX1262 版の可能性)\n", (int)st);
    return false;
  }
  radio.setPacketReceivedAction(onRx);
  radioOk = true;
  return true;
}

static void radioCheck() {
  Serial.println("--- LoRa チップの確認(電波は出さない)");
  if (!radioBegin()) return;
  Serial.printf("[radio] SX127x を検出。チップのバージョンレジスタ: 0x%02X(SX1276/77/78/79 は 0x12)\n",
                (unsigned)radio.getChipVersion());
  Serial.printf("[radio] 設定: %.1f MHz  BW125  SF7  CR4/5  (受信のみ)\n", RX_FREQ_MHZ);
  Serial.println("※ 920MHz 版が SX1276 か SX1278 かは、基板上のチップの印字でも確認する(docs/FINDINGS.md §6)");
}

static void listen(uint32_t seconds) {
  Serial.printf("--- 受信待ち(%u 秒)。電波は出さない。もう 1 台は電源を切っておく\n", (unsigned)seconds);
  if (!radioBegin()) return;
  rxFlag = false;
  radio.startReceive();
  uint32_t start = millis(), n = 0;
  while (millis() - start < seconds * 1000UL) {
    if (rxFlag) {
      rxFlag = false;
      uint8_t buf[64];
      size_t len = radio.getPacketLength();
      if (len > sizeof buf) len = sizeof buf;
      int16_t st = radio.readData(buf, len);
      Serial.printf("[rx] %d バイト  RSSI %.0f dBm  SNR %.1f dB  状態 %d\n  ", (int)len, radio.getRSSI(), radio.getSNR(),
                    (int)st);
      for (size_t i = 0; i < len; i++) Serial.printf("%02X ", buf[i]);
      Serial.println();
      n++;
      radio.startReceive();
    }
    delay(2);
  }
  radio.standby();
  Serial.printf("[rx] 受信 %lu 件\n", (unsigned long)n);
}

static void rssiWatch(uint32_t seconds) {
  Serial.printf("--- 電波の強さの観測(%u 秒、%.1f MHz)。周囲の雑音の目安(キャリアセンスのしきい値 -80dBm との比較用)\n",
                (unsigned)seconds, RX_FREQ_MHZ);
  if (!radioBegin()) return;
  radio.startReceive();
  delay(10);
  float mn = 0, mx = -200, sum = 0;
  uint32_t n = 0, over = 0, start = millis();
  mn = 0;
  while (millis() - start < seconds * 1000UL) {
    float r = radio.getRSSI(false, true);  // 今のチャンネルの瞬時値
    if (r < mn) mn = r;
    if (r > mx) mx = r;
    sum += r;
    if (r >= -80.0f) over++;
    n++;
    delay(5);
  }
  radio.standby();
  Serial.printf("[rssi] 回数 %lu  最小 %.0f  最大 %.0f  平均 %.1f dBm  -80dBm 以上: %lu 回(%.1f%%)\n", (unsigned long)n, mn,
                mx, n ? sum / n : 0, (unsigned long)over, n ? 100.0 * over / n : 0);
}

// ---- BLE(パッシブスキャン。聞くだけで、こちらからは何も送らない) ----
static void bleScan(uint32_t seconds) {
  Serial.printf("--- BLE パッシブスキャン(%u 秒)。周囲の BLE 機器の電波を聞くだけで、送信はしない\n", (unsigned)seconds);
  static bool inited = false;
  if (!inited) {
    NimBLEDevice::init("");
    inited = true;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(false);  // パッシブ: SCAN_REQ を送らない
  scan->setInterval(100);
  scan->setWindow(99);
  Serial.printf("空きヒープ(BLE 初期化後): %u\n", (unsigned)ESP.getFreeHeap());
  NimBLEScanResults res = scan->start(seconds, false);
  const int n = res.getCount();
  Serial.printf("[ble] 見つかった機器 %d 台\n", n);
  for (int i = 0; i < n && i < 12; i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    Serial.printf("  %s  RSSI %d dBm  名前: %s\n", d.getAddress().toString().c_str(), (int)d.getRSSI(),
                  d.getName().empty() ? "(なし)" : d.getName().c_str());
  }
  if (n > 12) Serial.printf("  ...ほか %d 台\n", n - 12);
  scan->clearResults();
  if (n == 0) Serial.println("[ble] 1 台も見つからない: 周囲に BLE 機器がない、またはアンテナ/初期化の問題を疑う");
}

static void help() {
  Serial.println("x:システム情報 i:I2C p:電源 g:GPS k:ボタンとLED r:LoRaチップ l:受信待ち e:電波の強さ b:BLE ?:ヘルプ  [GUIDE01] S:接続 B:電池 T:表示 N:通知 C:閉じる U:100回 J:日本語 W:桁数 D:切断");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n== Re-Ray T-Beam 立ち上げ確認(送信しない) ==");
  Serial.printf("送信: 無効(RERAY_TX_ENABLE=%d)  出力の上限: %d dBm\n", (int)RERAY_TX_ENABLE, reray::maxTxDbm());
  help();
}

void loop() {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'x': sysinfo(); break;
      case 'i': Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); i2cScan(); break;
      case 'p': pmuReport(); break;
      case 'g': gpsRun(30); break;
      case 'k': buttonLed(); break;
      case 'r': radioCheck(); break;
      case 'l': listen(60); break;
      case 'e': rssiWatch(30); break;
      case 'b': bleScan(10); break;
      case 'S': scanAndConnect(); break;
      case 'B': readBattery(); break;
      case 'T': showLines("Re-Ray", "Line 2 ABC xyz", "Line 3 0123456789"); break;
      case 'N': showNotification("Re-Ray", "SOS dev-001 zone 3"); break;
      case 'C': closePage(); break;
      case 'U': stress100(); break;
      case 'J': showLines("区域5へ物資搬送", "無事です", "助けてください"); break;
      case 'W': showLines("0123456789012345678901234567890123456789"); break;
      case 'D': if (g_client) g_client->disconnect(); break;
      case 'K': printConsts(); break;
      case 'Z': showWidthTest(); break;
      case '?': help(); break;
      default: break;
    }
  }
  delay(10);
}
