// GUIDE01 との BLE 確認用(T-Beam 単体で試す)。SDK の full_demo の BLE 部分を、M5Unified なしで取り出したもの。
// ★ BLE を使う。LoRa は送信しない(このファームは送信しない)。結果は docs/FINDINGS.md §3。
//   これは確認用。本番の画面は docs/DESIGN.md の構成で作り直す。
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>

extern "C" {
#include "guide01_protocol.h"
}

static const uint32_t SCAN_TIMEOUT_SEC = 8;
static const uint16_t REQUEST_MTU = 247;

static G01ScanFilter g_filter;
static G01TouchConsts g_touch;
static NimBLEAddress g_addr;
static bool g_found = false;
static NimBLEClient* g_client = nullptr;

// ---- 補助 ----
static std::string hexDump(const uint8_t* d, size_t n, size_t cap = 16) {
  std::string s;
  for (size_t i = 0; i < n && i < cap; i++) {
    char b[4];
    snprintf(b, sizeof b, "%02X ", d[i]);
    s += b;
  }
  if (n > cap) s += "...";
  return s;
}

static NimBLERemoteCharacteristic* findChar(const char* uuid) {
  if (!g_client || !g_client->isConnected()) return nullptr;
  for (auto svc : *g_client->getServices(false))
    for (auto chr : *svc->getCharacteristics(false))
      if (chr->getUUID() == NimBLEUUID(uuid)) return chr;
  return nullptr;
}

static bool writeTo(const char* label, const char* uuid, const uint8_t* data, int n) {
  NimBLERemoteCharacteristic* chr = findChar(uuid);
  if (!chr) { Serial.printf("[write] %s: characteristic not found (未接続?)\n", label); return false; }
  if (n <= 0) { Serial.printf("[write] %s: build error rc=%d\n", label, n); return false; }
  const bool ok = chr->writeValue(data, (size_t)n, !chr->canWriteNoResponse());
  Serial.printf("[write] %s: %s %dB %s\n", label, ok ? "OK" : "FAIL", n, hexDump(data, (size_t)n).c_str());
  return ok;
}

// ---- スキャン・接続 ----
static bool matchFilter(NimBLEAdvertisedDevice* dev) {
  if (!dev->haveManufacturerData()) return false;
  const std::string m = dev->getManufacturerData();
  if (m.size() < 2u + g_filter.data_size) return false;
  const uint16_t cid = (uint8_t)m[0] | ((uint16_t)(uint8_t)m[1] << 8);
  return cid == g_filter.id && memcmp(m.data() + 2, g_filter.data, g_filter.data_size) == 0;
}

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (g_found || !matchFilter(dev)) return;
    g_addr = dev->getAddress();
    g_found = true;
    Serial.printf("[scan] found %s rssi=%d name=%s\n", g_addr.toString().c_str(), dev->getRSSI(),
                  dev->getName().c_str());
    NimBLEDevice::getScan()->stop();
  }
};

static const char* touchName(uint8_t e) {
  if (e == g_touch.single_click) return "single_click";
  if (e == g_touch.double_click) return "double_click";
  if (e == g_touch.swipe_forward) return "swipe_forward";
  if (e == g_touch.swipe_back) return "swipe_back";
  if (e == g_touch.long_press) return "long_press";
  return "(unknown)";
}

static void onCmdNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  Serial.printf("[notify/cmd] len=%u raw=%s\n", (unsigned)len, hexDump(d, len).c_str());
  if (len < 1 || d[0] != g_touch.notify_type) return;
  G01TouchEventData ev{};
  const int rc = g01_parse_touch_event(d, len, &ev);
  if (rc == 0) Serial.printf("[touch] page=0x%02X event=%s\n", ev.page_id, touchName(ev.touch_event));
  else Serial.printf("[touch] parse error rc=%d\n", rc);
}

static void onFileNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  Serial.printf("[notify/file] %s\n", hexDump(d, len).c_str());
}

static bool scanAndConnect() {
  g_filter = g01_get_scan_filter();
  g_touch = g01_get_touch_consts();
  g_found = false;
  NimBLEDevice::init("reray-tbeam");
  NimBLEDevice::setMTU(REQUEST_MTU);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCb(), false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  Serial.printf("[scan] start (%us)\n", (unsigned)SCAN_TIMEOUT_SEC);
  scan->start(SCAN_TIMEOUT_SEC, false);
  scan->clearResults();
  if (!g_found) { Serial.println("[scan] GUIDE01 が見つからない"); return false; }

  if (!g_client) g_client = NimBLEDevice::createClient();
  g_client->setConnectionParams(12, 12, 0, 400);
  g_client->setConnectTimeout(10);
  if (!g_client->connect(g_addr)) { Serial.println("[connect] failed"); return false; }
  Serial.printf("[connect] OK mtu=%d\n", g_client->getMTU());
  if (!g_client->discoverAttributes()) { Serial.println("[discover] FAILED"); return false; }

  if (auto cmd = findChar(g01_get_ble_cmd_char())) Serial.printf("[subscribe] cmd %s\n", cmd->subscribe(true, onCmdNotify, true) ? "OK" : "FAIL");
  else Serial.println("[subscribe] cmd characteristic not found");
  if (auto file = findChar(g01_get_ble_file_char())) file->subscribe(true, onFileNotify, true);
  return true;
}

// ---- 機能 ----
static void readBattery() {
  NimBLERemoteCharacteristic* c = findChar(g01_get_ble_battery_level_char());
  if (!c) { Serial.println("[battery] characteristic not found"); return; }
  const std::string v = c->readValue();
  G01BatteryData b{};
  const int rc = g01_parse_battery_response((const uint8_t*)v.data(), v.size(), &b);
  if (rc == 0) Serial.printf("[battery] level=%u%% charging=%u\n", b.level, b.is_charging);
  else Serial.printf("[battery] parse error rc=%d raw=%s\n", rc, hexDump((const uint8_t*)v.data(), v.size()).c_str());
}

// 文字表示(座標・色・サイズ指定)。最大 4 行。show_status_bar=0。
// 画面の大きさは g01_get_gif_text_consts() の実行時の値を表示して確認する(実機で測る項目)。
static bool showLines(const char* l1, const char* l2 = nullptr, const char* l3 = nullptr, uint8_t fontSize = 32) {
  static uint8_t pkt[G01_BUF_GIF_TEXT];  // スタックに置くと loopTask(8KB)が足りない
  const G01GifTextConsts gc = g01_get_gif_text_consts();
  const char* lines[3] = {l1, l2, l3};
  G01DisplayItem items[3];
  memset(items, 0, sizeof items);
  size_t n = 0;
  for (int i = 0; i < 3; i++) {
    if (!lines[i]) continue;
    items[n].layer_id = (uint8_t)n;
    items[n].type = gc.element_type_text;
    items[n].x = 20;
    items[n].y = (uint16_t)(20 + n * (fontSize + 16));
    items[n].font_size = fontSize;
    items[n].color = {255, 255, 255};
    items[n].text = lines[i];
    n++;
  }
  const int len = g01_gif_text_display_elements(0, items, n, pkt, sizeof pkt);
  return writeTo("display_elements", g01_get_ble_gif_text_display_char(), pkt, len);
}

static bool showNotification(const char* title, const char* content) {
  uint8_t pkt[G01_BUF_TEXT];
  const int len = g01_notification("Re-Ray", title, content, "20260101T000000", pkt, sizeof pkt);
  return writeTo("notification", g01_get_ble_msg_notify_char(), pkt, len);
}

static void closePage() {
  uint8_t pkt[G01_BUF_COMMAND];
  const int len = g01_gif_text_close_page(pkt, sizeof pkt);
  writeTo("close_page", g01_get_ble_gif_text_display_char(), pkt, len);
}

static void stress100() {
  Serial.println("[stress] 100 回更新(500ms 間隔)。グラスの表示が崩れないか目視で確認する");
  uint32_t ok = 0;
  for (int i = 1; i <= 100; i++) {
    char a[48], b[48];
    snprintf(a, sizeof a, "update %03d/100", i);
    snprintf(b, sizeof b, "uptime %lus", (unsigned long)(millis() / 1000));
    if (showLines(a, b)) ok++;
    delay(500);
  }
  Serial.printf("[stress] 書き込み成功 %u/100\n", (unsigned)ok);
}

static void printConsts() {
  const G01GifTextConsts gc = g01_get_gif_text_consts();
  Serial.printf("[consts] screen=%ux%u content_h=%u pos_center=%u max_gif=%lu\n", gc.screen_width, gc.screen_height,
                gc.content_height, gc.pos_center, (unsigned long)gc.max_gif_size);
}


// 文字サイズごとに何文字見えるかを測る。1 文字ずつ違う 40 文字(0-9, A-Z, a-d)を 4 つのサイズで出す。
// グラスを見て、各行の「最後に見えた文字」を読めば、そのサイズの桁数が分かる(実機で測る項目)。
static void showWidthTest() {
  static uint8_t pkt[G01_BUF_GIF_TEXT];
  const G01GifTextConsts gc = g01_get_gif_text_consts();
  static const char* kRuler = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd";
  static const uint8_t sizes[4] = {16, 24, 32, 48};
  static const uint16_t ys[4] = {10, 50, 110, 180};
  G01DisplayItem items[4];
  memset(items, 0, sizeof items);
  for (int i = 0; i < 4; i++) {
    items[i].layer_id = (uint8_t)i;
    items[i].type = gc.element_type_text;
    items[i].x = 0;
    items[i].y = ys[i];
    items[i].font_size = sizes[i];
    items[i].color = {255, 255, 255};
    items[i].text = kRuler;
  }
  const int len = g01_gif_text_display_elements(0, items, 4, pkt, sizeof pkt);
  writeTo("width_test(16/24/32/48)", g01_get_ble_gif_text_display_char(), pkt, len);
}
