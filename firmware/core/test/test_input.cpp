// ボタンとタッチの読み方、文字の折り返し、周波数の固定のテスト。
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "harness.h"
#include "reray/airtime.h"
#include "reray/input.h"
#include "reray/radio_config.h"
#include "reray/text_layout.h"

using namespace reray;

// ---- ボタン ----

struct Ops {
  Op v[32];
  size_t n = 0;
  bool is(std::initializer_list<Op> expect) const {
    if (expect.size() != n) return false;
    size_t i = 0;
    for (Op o : expect)
      if (v[i++] != o) return false;
    return true;
  }
};

// mask を fromMs から toMs まで(5 ミリ秒きざみで)押し続け、出た操作を out に足す
static void hold(ButtonDecoder& d, uint8_t mask, uint32_t fromMs, uint32_t toMs, Ops& out) {
  for (uint32_t t = fromMs; t != toMs; t += 5) {
    d.update(mask, t);
    for (Op o = d.next(); o != Op::None; o = d.next())
      if (out.n < 32) out.v[out.n++] = o;
  }
}

static void test_buttons_single() {
  puts("buttons: single");
  {  // 戻る: 押している間は何も出ず、離したときに 1 回
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnBack, 0, 100, o);
    CHECK(o.n == 0);
    hold(d, 0, 100, 200, o);
    CHECK(o.is({Op::Back}));
  }
  {
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnSelect, 0, 100, o);
    hold(d, 0, 100, 200, o);
    CHECK(o.is({Op::Select}));
  }
  {  // 決定を短く: 押し始めに OkDown、離したときに Ok と OkUp
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnOk, 0, 200, o);
    CHECK(o.is({Op::OkDown}));
    hold(d, 0, 200, 300, o);
    CHECK(o.is({Op::OkDown, Op::Ok, Op::OkUp}));
  }
  {  // 決定を長く: 0.5 秒で OkHold。離したときは OkUp だけ(Ok は出ない)
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnOk, 0, 500, o);
    CHECK(o.is({Op::OkDown}));
    hold(d, kBtnOk, 500, 900, o);
    CHECK(o.is({Op::OkDown, Op::OkHold}));
    hold(d, 0, 900, 1000, o);
    CHECK(o.is({Op::OkDown, Op::OkHold, Op::OkUp}));
  }
  {  // 続けて 2 回押すと 2 回出る
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnSelect, 0, 80, o);
    hold(d, 0, 80, 160, o);
    hold(d, kBtnSelect, 160, 240, o);
    hold(d, 0, 240, 320, o);
    CHECK(o.is({Op::Select, Op::Select}));
  }
}

static void test_buttons_debounce() {
  puts("buttons: debounce");
  ButtonDecoder d;
  Ops o;
  // 30 ミリ秒より短いちらつきは、押したことにならない
  for (uint32_t t = 0; t < 400; t += 5) {
    d.update((t / 10) % 2 ? kBtnBack : 0, t);
    for (Op x = d.next(); x != Op::None; x = d.next()) o.v[o.n++] = x;
  }
  CHECK(o.n == 0);
  // ちらついたあとで落ち着けば、1 回だけ出る
  hold(d, kBtnBack, 400, 500, o);
  hold(d, 0, 500, 600, o);
  CHECK(o.is({Op::Back}));
}

static void test_buttons_chord() {
  puts("buttons: chord");
  {  // 3 つ同時に 1.5 秒 → ModeSwitch を 1 回。押し続けても、離しても、ほかは出ない
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnAll, 0, 1500, o);
    CHECK(o.n == 0);
    hold(d, kBtnAll, 1500, 6000, o);
    CHECK(o.is({Op::ModeSwitch}));
    hold(d, 0, 6000, 6200, o);
    CHECK(o.is({Op::ModeSwitch}));
  }
  {  // ばらばらに押し始めて、ばらばらに離しても、1 つだけの操作は出ない
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnBack, 0, 60, o);
    hold(d, kBtnBack | kBtnSelect, 60, 120, o);
    hold(d, kBtnAll, 120, 1700, o);
    CHECK(o.is({Op::ModeSwitch}));
    hold(d, kBtnSelect | kBtnOk, 1700, 1760, o);
    hold(d, kBtnOk, 1760, 1820, o);
    hold(d, 0, 1820, 1900, o);
    CHECK(o.is({Op::ModeSwitch}));
  }
  {  // 1.5 秒に足りなければ、何も出ない
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnAll, 0, 1000, o);
    hold(d, 0, 1000, 1200, o);
    CHECK(o.n == 0);
  }
  {  // 途中で 1 つ離したら、数え直す
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnAll, 0, 1000, o);
    hold(d, kBtnBack | kBtnSelect, 1000, 1100, o);
    hold(d, kBtnAll, 1100, 2100, o);
    CHECK(o.n == 0);
    hold(d, kBtnAll, 2100, 2800, o);
    CHECK(o.is({Op::ModeSwitch}));
  }
  {  // 2 つだけ押して離しても、何も出ない
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnBack | kBtnSelect, 0, 300, o);
    hold(d, 0, 300, 400, o);
    CHECK(o.n == 0);
  }
  {  // 決定のあとに別のボタンが加わったら、OkAbort。離しても Ok / OkUp / Back は出ない
    ButtonDecoder d;
    Ops o;
    hold(d, kBtnOk, 0, 200, o);
    hold(d, kBtnOk | kBtnBack, 200, 400, o);
    CHECK(o.is({Op::OkDown, Op::OkAbort}));
    hold(d, kBtnOk, 400, 1200, o);  // 決定だけに戻って長く押しても、OkHold は出ない
    hold(d, 0, 1200, 1300, o);
    CHECK(o.is({Op::OkDown, Op::OkAbort}));
  }
  {  // 時刻が一周しても同じ動き
    ButtonDecoder d;
    Ops o;
    const uint32_t t0 = 0xFFFFFF00u;
    hold(d, kBtnAll, t0, t0 + 1600, o);
    CHECK(o.is({Op::ModeSwitch}));
    hold(d, 0, t0 + 1600, t0 + 1700, o);
    hold(d, kBtnBack, t0 + 1700, t0 + 1800, o);
    hold(d, 0, t0 + 1800, t0 + 1900, o);
    CHECK(o.is({Op::ModeSwitch, Op::Back}));
  }
}

static void test_touch() {
  puts("touch");
  TouchMapper m;
  CHECK(m.map(Touch::SingleClick, 1000) == Op::Select);
  CHECK(m.map(Touch::SingleClick, 1010) == Op::None);  // 同じ通知が続けて届く(実機で 3 回ずつ)
  CHECK(m.map(Touch::SingleClick, 1020) == Op::None);
  CHECK(m.map(Touch::SingleClick, 1400) == Op::Select);  // 0.3 秒たてば、次の操作
  CHECK(m.map(Touch::DoubleClick, 1410) == Op::Ok);      // 種類が違えば、すぐでも通す
  CHECK(m.map(Touch::DoubleClick, 1420) == Op::None);
  CHECK(m.map(Touch::SwipeBack, 2000) == Op::Back);
  CHECK(m.map(Touch::SwipeForward, 2500) == Op::Select);
  CHECK(m.map(Touch::LongPress, 3000) == Op::None);      // 割り当てない
  TouchMapper w;
  CHECK(w.map(Touch::SwipeBack, 0xFFFFFFF0u) == Op::Back);
  CHECK(w.map(Touch::SwipeBack, 0x00000010u) == Op::None);  // 一周をまたいでも 32 ミリ秒しかたっていない
}

// ---- 折り返し ----

static bool lineIs(const char* s, Span sp, const char* expect) {
  return strlen(expect) == sp.len && memcmp(s + sp.off, expect, sp.len) == 0;
}

static void test_text() {
  puts("text layout");
  uint32_t cp;
  CHECK(utf8Next("A", 1, 0, cp) == 1 && cp == 'A');
  CHECK(utf8Next("あ", 3, 0, cp) == 3 && cp == 0x3042);
  CHECK(utf8Next("😀", 4, 0, cp) == 4 && cp == 0x1F600);
  CHECK(utf8Next("\x80", 1, 0, cp) == 1 && cp == 0xFFFD);      // 続きのバイトから始まる
  CHECK(utf8Next("\xE3\x81", 2, 0, cp) == 1 && cp == 0xFFFD);  // 途中で終わる
  CHECK(utf8Next("\xE3" "A", 2, 0, cp) == 1 && cp == 0xFFFD);  // 続きが違う

  CHECK(cpWidth('a') == 1 && cpWidth(0x3042) == 2 && cpWidth(0xFF71) == 1 && cpWidth('\n') == 0);
  CHECK(textWidth("abc") == 3 && textWidth("あい") == 4 && textWidth("aあb") == 4 && textWidth("") == 0);

  Span sp[8];
  const char* a = "0123456789";
  CHECK(wrapText(a, 10, 4, sp, 8) == 3);
  CHECK(lineIs(a, sp[0], "0123") && lineIs(a, sp[1], "4567") && lineIs(a, sp[2], "89"));

  const char* j = "あいうえお";
  CHECK(wrapText(j, strlen(j), 4, sp, 8) == 3);
  CHECK(lineIs(j, sp[0], "あい") && lineIs(j, sp[1], "うえ") && lineIs(j, sp[2], "お"));

  const char* m = "abあcい";  // 幅 1,1,2,1,2。幅 3 に入れる
  CHECK(wrapText(m, strlen(m), 3, sp, 8) == 3);
  CHECK(lineIs(m, sp[0], "ab") && lineIs(m, sp[1], "あc") && lineIs(m, sp[2], "い"));

  const char* nl = "ab\n\ncd\n";  // 改行で必ず分ける。最後の改行では、空の行を足さない
  CHECK(wrapText(nl, strlen(nl), 10, sp, 8) == 3);
  CHECK(lineIs(nl, sp[0], "ab") && lineIs(nl, sp[1], "") && lineIs(nl, sp[2], "cd"));

  CHECK(wrapText("", 0, 10, sp, 8) == 0);
  CHECK(wrapText("あ", 3, 1, sp, 8) == 1 && lineIs("あ", sp[0], "あ"));  // 幅より広い字でも止まらない

  // 入れ物より行が多いとき: 数は全部を返し、入れ物は壊さない
  Span two[2] = {{9, 9}, {9, 9}};
  CHECK(wrapText(a, 10, 2, two, 1) == 5);
  CHECK(two[0].off == 0 && two[0].len == 2 && two[1].off == 9);
  CHECK(wrapText(a, 10, 2, nullptr, 0) == 5);

  // どの幅でも、行の先頭が文字の途中にならない。全部の行をつなぐと元に戻る
  const char* mix = "区域5へ物資搬送 abc 助けてください😀xyz";
  for (uint16_t cols = 1; cols <= 30; cols++) {
    Span s2[64];
    const size_t n = wrapText(mix, strlen(mix), cols, s2, 64);
    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
      CHECK(((uint8_t)mix[s2[i].off] & 0xC0) != 0x80);
      CHECK(textWidth(mix + s2[i].off, s2[i].len) <= (cols < 2 ? 2 : cols));
      CHECK(s2[i].off == total);
      total += s2[i].len;
    }
    CHECK(total == strlen(mix));
  }

  char out[16];
  CHECK(fitText("abcdef", 4, out, sizeof out) == 4 && strcmp(out, "abcd") == 0);
  CHECK(fitText("あいう", 5, out, sizeof out) == 6 && strcmp(out, "あい") == 0);   // 幅 5 には 2 字まで
  CHECK(fitText("あいうえおか", 99, out, 8) == 6 && strcmp(out, "あい") == 0);     // 入れ物が 8 バイト
  CHECK(fitText("ab\ncd", 99, out, sizeof out) == 2 && strcmp(out, "ab") == 0);    // 改行で止まる
  CHECK(fitText("", 10, out, sizeof out) == 0 && out[0] == '\0');
}

// ---- 周波数の固定 ----

static void test_radio_config() {
  puts("radio config");
  CHECK(inCertifiedBand(920600) && inCertifiedBand(923200) && inCertifiedBand(928000));
  CHECK(!inCertifiedBand(920000));  // 公式サンプルの 920.0MHz は範囲の外
  CHECK(!inCertifiedBand(920400) && !inCertifiedBand(928200) && !inCertifiedBand(923300));
  CHECK(isConfirmedChannel(923200));
  CHECK(!isConfirmedChannel(923400) && !isConfirmedChannel(922400) && !isConfirmedChannel(920600));
  CHECK(loraFreqKhz() == 923200 && loraFreqMhz() > 923.19f && loraFreqMhz() < 923.21f);
  CHECK(loraSf() >= 7 && loraSf() <= 9 && loraBwKhz() == 125.0f);

  // 送信前の電波確認: -80dBm 以上なら送らない。測れなかったときも送らない
  CHECK(carrierSenseAllowsTx(true, -120.0f));
  CHECK(carrierSenseAllowsTx(true, -80.5f));
  CHECK(!carrierSenseAllowsTx(true, -80.0f));
  CHECK(!carrierSenseAllowsTx(true, -60.0f));
  CHECK(!carrierSenseAllowsTx(false, -120.0f));

  // 決めた設定で、いちばん長いフレーム(19 バイト)が 1 回 0.2 秒に収まる
  CHECK(loraAirtimeUs(19, loraSf(), RERAY_LORA_BW_HZ) <= 200000);
  CHECK(loraAirtimeUs(19, 9, RERAY_LORA_BW_HZ) <= 200000);   // 使ってよい上限の SF9 でも収まる
  CHECK(loraAirtimeUs(19, 10, RERAY_LORA_BW_HZ) > 200000);   // SF10 は収まらない(だから使わない)
}

void run_input_tests() {
  test_buttons_single();
  test_buttons_debounce();
  test_buttons_chord();
  test_touch();
  test_text();
  test_radio_config();
}
