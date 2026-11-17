// LoRa の周波数と設定の固定(電波法のガード。docs/COMPLIANCE.md §4.1)。
//
// 使ってよい値は、docs/COMPLIANCE.md の確認欄で決めたものだけ。違う値にするとビルドが失敗する。
// 値を増やすときは、先に確認欄(#5)を書き直してから、ここの一覧に足す。
#pragma once
#include <stdint.h>

#ifndef RERAY_LORA_FREQ_KHZ
#define RERAY_LORA_FREQ_KHZ 923200  // 923.2MHz(ch37)。2026-11-17 に決定
#endif

#ifndef RERAY_LORA_SF
#define RERAY_LORA_SF 7
#endif

#ifndef RERAY_LORA_BW_HZ
#define RERAY_LORA_BW_HZ 125000
#endif

namespace reray {

// 証明書の範囲(920.6〜928.0MHz、200kHz きざみ)の中か
constexpr bool inCertifiedBand(uint32_t khz) {
  return khz >= 920600 && khz <= 928000 && (khz - 920600) % 200 == 0;
}

// 確認欄で決めたチャンネルか。今は 923.2MHz だけ
constexpr bool isConfirmedChannel(uint32_t khz) { return khz == 923200; }

static_assert(inCertifiedBand(RERAY_LORA_FREQ_KHZ), "LoRa frequency is outside the certified band (920.6-928.0 MHz, 200 kHz grid)");
static_assert(isConfirmedChannel(RERAY_LORA_FREQ_KHZ), "LoRa channel is not the one confirmed in docs/COMPLIANCE.md");
static_assert(RERAY_LORA_BW_HZ == 125000, "LoRa bandwidth must be 125 kHz (docs/COMPLIANCE.md)");
// SF10 以上は、1 回の送信が 0.2 秒を超える(17 バイトで約 0.33 秒)。確認が済むまで使わない
static_assert(RERAY_LORA_SF >= 7 && RERAY_LORA_SF <= 9, "LoRa SF must be 7..9 (single transmission <= 200 ms)");

constexpr uint32_t loraFreqKhz() { return RERAY_LORA_FREQ_KHZ; }
constexpr float loraFreqMhz() { return RERAY_LORA_FREQ_KHZ / 1000.0f; }
constexpr uint8_t loraSf() { return RERAY_LORA_SF; }
constexpr float loraBwKhz() { return RERAY_LORA_BW_HZ / 1000.0f; }

// 送信前の電波確認(キャリアセンス)のしきい値。これ以上の強さを受けていたら、送らない。
constexpr int kCarrierSenseThresholdDbm = -80;

// 測った電波の強さから、送ってよいかを決める。測れなかったとき(rssiValid = false)は、送らない。
constexpr bool carrierSenseAllowsTx(bool rssiValid, float rssiDbm) {
  return rssiValid && rssiDbm < (float)kCarrierSenseThresholdDbm;
}

}  // namespace reray
