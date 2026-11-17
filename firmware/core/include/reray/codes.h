// モードと、送れるものの一覧(docs/DESIGN.md §2、§6)。
//
// 定型信号のコード・名前は、サーバーの一覧(Re-Ray_Server の db/init/02_seed.sql。仮の 9 個)に合わせた。
// 数量と方角の決まりは、サーバーの SIGNAL_FORMAT_2026-10-23.md に合わせた:
//   数量 n   : 5 きざみの区分の「上限」を入れる(n = 20 は「16〜20」)。使わないときは 0(送るときは項目ごと省く)
//   方角 dir : 北から時計回りに 1〜8。使わないときは 0(同上)
// 一覧が変わったら、ここを同じに直す。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace reray {

enum class Mode : uint8_t { Normal = 0, Emergency = 1 };

enum class Need : uint8_t { No, Optional, Required };

constexpr uint8_t kCodeVoice = 0;  // 定型信号ではなく、音声を送る
constexpr uint8_t kCodeSos = 1;

struct SendType {
  uint8_t code;        // 定型信号のコード(1〜31)。0 は音声
  const char* name;
  bool direction;      // 方角を選ぶ(必須)
  Need count;          // 数量を選ぶか
  const char* unit;    // 数量の単位
  uint8_t counts[5];   // 端末で選べる数量(0 で終わり)
};

namespace detail {
// 平常時: トランシーバーとして使う
constexpr SendType kNormalTypes[] = {
    {kCodeVoice, "音声", false, Need::No, "", {0}},
    {3, "了解", false, Need::No, "", {0}},
    {4, "到着", false, Need::No, "", {0}},
    {9, "移動します", true, Need::Optional, "人", {5, 10, 20, 50, 0}},
};
// 緊急時: 決まった信号と SOS。音声は送らない
constexpr SendType kEmergencyTypes[] = {
    {kCodeSos, "SOS", false, Need::No, "", {0}},
    {2, "無事", false, Need::No, "", {0}},
    {5, "応援を頼む", false, Need::Optional, "人", {5, 10, 20, 0}},
    {6, "要救助者あり", false, Need::Required, "人", {5, 10, 20, 50, 100}},
    {7, "負傷者あり", false, Need::Required, "人", {5, 10, 20, 50, 0}},
    {8, "食料がほしい", false, Need::Optional, "食", {10, 30, 50, 100, 0}},
    {4, "到着", false, Need::No, "", {0}},
    {9, "移動します", true, Need::Optional, "人", {5, 10, 20, 50, 0}},
    {3, "了解", false, Need::No, "", {0}},
};
constexpr const char* kDirNames[9] = {"", "北", "北東", "東", "南東", "南", "南西", "西", "北西"};
}  // namespace detail

inline size_t sendTypeCount(Mode m) {
  return m == Mode::Normal ? sizeof(detail::kNormalTypes) / sizeof(SendType)
                           : sizeof(detail::kEmergencyTypes) / sizeof(SendType);
}
inline const SendType& sendType(Mode m, size_t i) {
  return m == Mode::Normal ? detail::kNormalTypes[i] : detail::kEmergencyTypes[i];
}

inline size_t countChoices(const SendType& t) {
  size_t n = 0;
  while (n < sizeof(t.counts) && t.counts[n] != 0) n++;
  return n;
}

constexpr size_t kDirCount = 8;
inline const char* dirName(uint8_t dir) { return (dir >= 1 && dir <= 8) ? detail::kDirNames[dir] : ""; }

// 数量の見せ方。サーバーと同じ規則: 「(n−4)〜n」+ 単位。n = 0 なら空。戻り値はバイト数。
inline size_t formatCount(uint8_t n, const char* unit, char* out, size_t cap) {
  if (cap == 0) return 0;
  if (n == 0) {
    out[0] = '\0';
    return 0;
  }
  const int w = snprintf(out, cap, "%u〜%u%s", (unsigned)(n >= 4 ? n - 4 : 1), (unsigned)n, unit);
  return (w > 0 && (size_t)w < cap) ? (size_t)w : 0;
}

// 送る内容を 1 行にする。例:「応援を頼む 6〜10人」「北東へ移動 6〜10人」「無事」。戻り値はバイト数。
inline size_t formatSignal(const SendType& t, uint8_t n, uint8_t dir, char* out, size_t cap) {
  char cnt[24];
  formatCount(n, t.unit, cnt, sizeof cnt);
  int w;
  if (t.direction && dir >= 1 && dir <= 8) {
    w = snprintf(out, cap, "%sへ移動%s%s", dirName(dir), cnt[0] ? " " : "", cnt);
  } else {
    w = snprintf(out, cap, "%s%s%s", t.name, cnt[0] ? " " : "", cnt);
  }
  return (w > 0 && (size_t)w < cap) ? (size_t)w : 0;
}

}  // namespace reray
