// 文字の折り返し(UTF-8)。グラスは長い行を折り返さずに切るので、こちらで行に分けてから渡す。
//
// 幅は「半角 = 1、全角 = 2」で数える。GUIDE01 の実際の字幅はまだ測っていない(docs/FINDINGS.md §3)。
// 測ったら、1 行の幅(cols)を直す。字の種類ごとの幅が違っていたら cpWidth() を直す。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace reray {

// 1 文字ぶん読む。戻り値は進むバイト数(1 以上)。壊れたバイトは U+FFFD として 1 バイトだけ進む。
inline size_t utf8Next(const char* s, size_t len, size_t i, uint32_t& cp) {
  const uint8_t c = (uint8_t)s[i];
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  size_t n = 0;
  uint32_t v = 0;
  if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
  else { cp = 0xFFFD; return 1; }
  if (i + n > len) { cp = 0xFFFD; return 1; }
  for (size_t k = 1; k < n; k++) {
    const uint8_t d = (uint8_t)s[i + k];
    if ((d & 0xC0) != 0x80) { cp = 0xFFFD; return 1; }
    v = (v << 6) | (d & 0x3F);
  }
  cp = v;
  return n;
}

inline uint8_t cpWidth(uint32_t cp) {
  if (cp < 0x20 || cp == 0x7F) return 0;        // 制御文字
  if (cp < 0x7F) return 1;                      // 半角の英数字・記号
  if (cp >= 0xFF61 && cp <= 0xFF9F) return 1;   // 半角カナ
  return 2;                                     // それ以外は全角として数える
}

inline uint16_t textWidth(const char* s, size_t len) {
  uint16_t w = 0;
  for (size_t i = 0; i < len;) {
    uint32_t cp;
    i += utf8Next(s, len, i, cp);
    w = (uint16_t)(w + cpWidth(cp));
  }
  return w;
}
inline uint16_t textWidth(const char* s) { return textWidth(s, strlen(s)); }

struct Span {
  uint16_t off;
  uint16_t len;
};

// 幅 cols に収まるように行に分ける。'\n' では必ず改行する。
// 戻り値は全体の行数(max を超えた分も数える)。out には先頭から max 行まで入る(out は nullptr でもよい)。
inline size_t wrapText(const char* s, size_t len, uint16_t cols, Span* out, size_t max) {
  size_t count = 0, lineStart = 0, i = 0;
  uint16_t width = 0;
  auto emit = [&](size_t end) {
    if (out && count < max) out[count] = Span{(uint16_t)lineStart, (uint16_t)(end - lineStart)};
    count++;
  };
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8Next(s, len, i, cp);
    if (cp == '\n') {
      emit(i);
      i += n;
      lineStart = i;
      width = 0;
      continue;
    }
    const uint8_t w = cpWidth(cp);
    if (width + w > cols && i > lineStart) {  // この字を入れるとはみ出す → ここで改行
      emit(i);
      lineStart = i;
      width = 0;
    }
    width = (uint16_t)(width + w);
    i += n;
  }
  if (i > lineStart) emit(i);
  return count;
}

// s の先頭から、幅 cols とバッファ cap に収まる分だけを out に写す(文字の途中では切らない)。戻り値はバイト数。
inline size_t fitText(const char* s, size_t len, uint16_t cols, char* out, size_t cap) {
  if (cap == 0) return 0;
  size_t i = 0;
  uint16_t width = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8Next(s, len, i, cp);
    if (cp == '\n') break;
    const uint8_t w = cpWidth(cp);
    if (width + w > cols || i + n >= cap) break;
    width = (uint16_t)(width + w);
    i += n;
  }
  memcpy(out, s, i);
  out[i] = '\0';
  return i;
}
inline size_t fitText(const char* s, uint16_t cols, char* out, size_t cap) {
  return fitText(s, strlen(s), cols, out, cap);
}

}  // namespace reray
