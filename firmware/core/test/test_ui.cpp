// 送れるものの一覧、チャンネル、画面の状態遷移のテスト。
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "harness.h"
#include "reray/channels.h"
#include "reray/codes.h"
#include "reray/ui.h"

using namespace reray;

// ---- 道具 ----

struct Fx {
  Channels ch;
  Messages st;
  Ui ui;
  uint32_t t = 1000;
  Fx() : ui(ch, st) {}

  // ボタンを短く押したとき、実際に届く操作の並びで渡す
  void back() { ui.op(Op::Back, false, t += 100); }
  void sel() { ui.op(Op::Select, false, t += 100); }
  void ok() {
    ui.op(Op::OkDown, false, t += 100);
    ui.op(Op::Ok, false, t += 100);
    ui.op(Op::OkUp, false, t);
  }
  void sel(int n) { for (int i = 0; i < n; i++) sel(); }

  // たまっている動作を全部取り出す
  size_t drain(Action* out, size_t max) {
    size_t n = 0;
    Action a;
    while (ui.nextAction(a))
      if (n < max) out[n++] = a;
    return n;
  }
  size_t drain() { Action a[8]; return drain(a, 8); }

  View view() { View v; ui.render(v); return v; }
  bool line(size_t i, const char* expect) {
    View v = view();
    if (strcmp(v.line[i], expect) == 0) return true;
    printf("    line[%u] = \"%s\" (expected \"%s\")\n", (unsigned)i, v.line[i], expect);
    return false;
  }

  Message msg(uint8_t chNo, uint32_t mid, const char* from, const char* body) {
    Message m;
    m.ch = chNo;
    m.mid = mid;
    copyText(m.from, sizeof m.from, from);
    copyText(m.body, sizeof m.body, body);
    return m;
  }
  bool recv(uint8_t chNo, uint32_t mid, const char* from, const char* body) {
    return ui.onMessage(msg(chNo, mid, from, body), true, false, t += 10);
  }
};

// ---- 送れるものの一覧 ----

static void test_codes() {
  puts("codes");
  CHECK(sendTypeCount(Mode::Normal) == 4);
  CHECK(sendType(Mode::Normal, 0).code == kCodeVoice);
  CHECK(sendTypeCount(Mode::Emergency) == 9);
  CHECK(sendType(Mode::Emergency, 0).code == kCodeSos);
  for (size_t i = 0; i < sendTypeCount(Mode::Emergency); i++)
    CHECK(sendType(Mode::Emergency, i).code != kCodeVoice);  // 緊急時は音声を送らない
  for (Mode m : {Mode::Normal, Mode::Emergency})
    for (size_t i = 0; i < sendTypeCount(m); i++) {
      const SendType& t = sendType(m, i);
      CHECK(t.code <= 31);
      if (t.count != Need::No) CHECK(countChoices(t) >= 1);
      for (size_t k = 0; k < countChoices(t); k++) CHECK(t.counts[k] % 5 == 0 && t.counts[k] <= 100);  // 5 きざみ、最大 100
    }

  char b[64];
  CHECK(formatCount(5, "人", b, sizeof b) > 0 && strcmp(b, "1〜5人") == 0);
  CHECK(formatCount(20, "人", b, sizeof b) > 0 && strcmp(b, "16〜20人") == 0);
  CHECK(formatCount(100, "食", b, sizeof b) > 0 && strcmp(b, "96〜100食") == 0);
  CHECK(formatCount(0, "人", b, sizeof b) == 0 && b[0] == '\0');
  CHECK(strcmp(dirName(1), "北") == 0 && strcmp(dirName(2), "北東") == 0 && strcmp(dirName(8), "北西") == 0);
  CHECK(dirName(0)[0] == '\0' && dirName(9)[0] == '\0');

  const SendType& rescue = sendType(Mode::Emergency, 3);
  CHECK(rescue.code == 6 && rescue.count == Need::Required && countChoices(rescue) == 5);
  formatSignal(rescue, 20, 0, b, sizeof b);
  CHECK(strcmp(b, "要救助者あり 16〜20人") == 0);
  const SendType& move = sendType(Mode::Emergency, 7);
  CHECK(move.code == 9 && move.direction);
  formatSignal(move, 10, 2, b, sizeof b);
  CHECK(strcmp(b, "北東へ移動 6〜10人") == 0);
  formatSignal(move, 0, 5, b, sizeof b);
  CHECK(strcmp(b, "南へ移動") == 0);
  formatSignal(sendType(Mode::Emergency, 1), 0, 0, b, sizeof b);
  CHECK(strcmp(b, "無事") == 0);
}

// ---- チャンネル ----

static Channel mk(uint8_t ch, ChKind kind, const char* name) {
  Channel c;
  c.ch = ch;
  c.kind = kind;
  copyText(c.name, sizeof c.name, name);
  return c;
}

static void test_channels() {
  puts("channels");
  Channels c;
  // 最初(サーバーの一覧が届く前): 本部、全体、送った記録
  CHECK(c.count() == 3);
  CHECK(c.at(0).ch == 1 && c.at(0).kind == ChKind::Direct && strcmp(c.at(0).name, "本部") == 0);
  CHECK(c.at(1).ch == kChBroadcast && c.at(2).ch == kChLocalSent);
  CHECK(c.talkableCount() == 1);
  // 今のサーバー(ch が付かない)の文: 個別あて → 本部、全員あて → 全体
  CHECK(c.resolve(false, 0, false) == 1);
  CHECK(c.resolve(false, 0, true) == kChBroadcast);

  const Channel list[] = {mk(5, ChKind::Group, "北班"), mk(2, ChKind::Direct, "本部"), mk(0, ChKind::Group, "x"),
                          mk(255, ChKind::Group, "y"), mk(5, ChKind::Group, "重複")};
  c.at(0).unread = 3;
  CHECK(c.setFromConfig(list, 5, 10));
  CHECK(c.count() == 4);  // 北班、本部、全体、送った記録(0・255・重複は入れない)
  CHECK(c.at(0).ch == 5 && c.at(0).kind == ChKind::Group && strcmp(c.at(0).name, "北班") == 0);
  CHECK(c.at(1).ch == 2 && c.at(2).ch == kChBroadcast && c.at(3).ch == kChLocalSent);
  CHECK(c.talkableCount() == 2 && c.rev() == 10);
  CHECK(c.resolve(true, 5, false) == 5);
  CHECK(c.resolve(true, 77, false) == 2);      // 知らない番号 → 最初の 1 対 1
  CHECK(c.resolve(true, 77, true) == kChBroadcast);
  CHECK(c.resolve(true, 255, false) == 2);     // 送った記録には、外から入れられない
  CHECK(c.resolve(false, 0, false) == 2);

  // 古い一覧(rev が同じか小さい)は無視する
  const Channel old[] = {mk(9, ChKind::Group, "古い")};
  CHECK(!c.setFromConfig(old, 1, 10) && !c.setFromConfig(old, 1, 3));
  CHECK(c.count() == 4 && c.at(0).ch == 5);

  // 新しい一覧では、読んでいない数を引き継ぐ。上限(8)を超えた分は捨てる
  c.at(0).unread = 7;
  Channel many[12];
  for (int i = 0; i < 12; i++) many[i] = mk((uint8_t)(i + 5), ChKind::Group, "班");
  CHECK(c.setFromConfig(many, 12, 11));
  CHECK(c.talkableCount() == 8 && c.count() == 10 && c.at(0).unread == 7);

  // 1 対 1 が無ければ、個別あては全体に入れる
  const Channel groups[] = {mk(5, ChKind::Group, "北班")};
  CHECK(c.setFromConfig(groups, 1, 12));
  CHECK(c.resolve(false, 0, false) == kChBroadcast);

  // 名前が長すぎるときは、文字の途中で切らない
  const Channel longName[] = {mk(3, ChKind::Group, "あいうえおかきくけこ")};  // 30 バイト
  CHECK(c.setFromConfig(longName, 1, 13));
  CHECK(strcmp(c.at(0).name, "あいうえおかきく") == 0);  // 24 バイト = 8 文字
}

static void test_store() {
  puts("message store");
  MessageStore<4> s;
  Message m;
  m.ch = 1;
  for (uint32_t i = 1; i <= 3; i++) { m.mid = i; CHECK(s.add(m)); }
  CHECK(!s.add(m));  // 同じチャンネルに同じ mid は 2 回入れない
  m.ch = 2;
  CHECK(s.add(m));   // チャンネルが違えば別のもの
  CHECK(s.size() == 4 && s.countIn(1) == 3 && s.countIn(2) == 1);
  CHECK(s.nth(1, 0)->mid == 1 && s.nth(1, 2)->mid == 3 && s.nth(1, 3) == nullptr);
  m.ch = 1;
  m.mid = 9;
  CHECK(s.add(m));   // 満杯 → いちばん古いもの(ch1 の mid 1)が消える
  CHECK(s.size() == 4 && s.countIn(1) == 3 && s.nth(1, 0)->mid == 2 && s.nth(1, 2)->mid == 9);
  m.mid = 0;         // mid が 0(端末の中の記録)は、いくつでも入る
  CHECK(s.add(m) && s.add(m));
  Messages big;
  for (uint32_t i = 1; i <= 70; i++) { m.mid = i; big.add(m); }
  CHECK(big.size() == 64 && big.nth(1, 0)->mid == 7 && big.nth(1, 63)->mid == 70);
}

// ---- 画面 ----

static void test_ui_navigation() {
  puts("ui: navigation");
  Fx f;
  CHECK(f.ui.screen() == Screen::Home && f.ui.mode() == Mode::Normal);
  CHECK(f.line(0, "平常 ホーム [圏外]"));
  CHECK(f.line(1, ">チャンネルを見る") && f.line(2, " メッセージを送る") && f.line(3, ""));
  f.ui.setLink(true, false);
  CHECK(f.line(0, "平常 ホーム"));

  f.back();  // ホームで戻るを押しても、何も起きない
  CHECK(f.ui.screen() == Screen::Home);
  f.sel();
  CHECK(f.line(1, " チャンネルを見る") && f.line(2, ">メッセージを送る"));
  f.sel();
  CHECK(f.ui.cursor() == 0);

  f.ok();
  CHECK(f.ui.screen() == Screen::ChannelList);
  CHECK(f.line(0, "平常 チャンネル") && f.line(1, ">本部") && f.line(2, " 全体") && f.line(3, " 送った記録"));
  f.sel(3);  // 最後の次は最初
  CHECK(f.ui.cursor() == 0);
  f.sel();
  f.ok();
  CHECK(f.ui.screen() == Screen::Reader && f.ui.readerChannel() == kChBroadcast);
  CHECK(f.line(0, "平常 全体 1/1") && f.line(1, "(まだありません)"));
  f.sel();
  f.ok();  // 1 ページしかないので、上下しても変わらない
  CHECK(f.ui.readerPage() == 0);
  f.back();
  CHECK(f.ui.screen() == Screen::ChannelList && f.ui.cursor() == 1);  // 見ていたチャンネルに戻る
  f.back();
  CHECK(f.ui.screen() == Screen::Home);
  CHECK(f.drain() == 0);  // ここまで、端末への動作は出ていない
}

static void test_ui_reader() {
  puts("ui: reader");
  Fx f;
  f.ui.setLink(true, false);
  // 幅 24 の画面で、3 行 × 3 ページぶんの文を入れる
  f.recv(1, 1, "", "一二三四五六七八九十一二三四五六七八九十一二三四");            // 24 文字 = 2 行
  f.recv(1, 2, "山田", "北口の点検が終わりました。次は南橋に向かいます。");       // 3 行
  f.recv(1, 3, "", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKL");  // 74 字 = 4 行
  f.back();  // 知らせを閉じる
  f.ok();
  f.ok();    // 本部を開く
  CHECK(f.ui.screen() == Screen::Reader && f.ui.readerPages() == 3);
  CHECK(f.ui.readerPage() == 2);  // いちばん新しい所から
  CHECK(f.line(0, "平常 本部 3/3"));
  CHECK(f.line(1, "YZabcdefghijklmnopqrstuv") && f.line(2, "wxyz0123456789ABCDEFGHIJ") && f.line(3, "KL"));
  f.ok();  // 決定 = 上へ
  CHECK(f.ui.readerPage() == 1 && f.line(0, "平常 本部 2/3"));
  CHECK(f.line(1, "ました。次は南橋に向かい") && f.line(2, "ます。") && f.line(3, "ABCDEFGHIJKLMNOPQRSTUVWX"));
  f.ok();
  f.ok();  // いちばん上で止まる
  CHECK(f.ui.readerPage() == 0);
  CHECK(f.line(1, "一二三四五六七八九十一二") && f.line(2, "三四五六七八九十一二三四") && f.line(3, "山田: 北口の点検が終わり"));
  f.sel();  // 選択 = 下へ
  f.sel();
  f.sel();  // いちばん下で止まる
  CHECK(f.ui.readerPage() == 2);

  // 読んでいるチャンネルに新しい文が届いた: いちばん下にいれば付いていく。知らせは出ない
  f.recv(1, 4, "本部", "了解");
  CHECK(f.ui.overlay() == Overlay::None && f.ui.readerPages() == 4 && f.ui.readerPage() == 3);
  CHECK(f.line(1, "本部: 了解"));
  f.ok();   // 上を読んでいる最中は、動かさない
  f.recv(1, 5, "本部", "次");
  CHECK(f.ui.readerPage() == 2);
  CHECK(f.ch.at(0).unread == 0);
}

static void test_ui_incoming() {
  puts("ui: incoming");
  Fx f;
  CHECK(f.recv(1, 10, "本部", "北口に集合してください"));
  CHECK(f.ui.overlay() == Overlay::Notice && f.ch.at(0).unread == 1);
  CHECK(f.line(0, "平常 新着 本部 [圏外]") && f.line(1, "本部: 北口に集合してくだ") && f.line(2, "さい"));
  CHECK(!f.recv(1, 10, "本部", "北口に集合してください"));  // 同じものが 2 回届いても、1 件
  CHECK(f.ch.at(0).unread == 1);

  f.sel();  // どのボタンでも閉じる。画面は進まない
  CHECK(f.ui.overlay() == Overlay::None && f.ui.screen() == Screen::Home && f.ui.cursor() == 0);
  f.ok();
  CHECK(f.line(1, ">本部 (1)"));
  f.ok();   // 開くと、読んでいない数が消える
  CHECK(f.ch.at(0).unread == 0);

  // 知らせは、時間がたつと消える
  f.back();
  f.back();
  f.recv(1, 11, "", "二通目");
  CHECK(f.ui.overlay() == Overlay::Notice);
  f.ui.tick(f.t + 7000);
  CHECK(f.ui.overlay() == Overlay::Notice);
  f.ui.tick(f.t + 8100);
  CHECK(f.ui.overlay() == Overlay::None);

  // チャンネルが付かない文(今のサーバー): 全員あて → 全体、個別あて → 本部
  f.ui.onMessage(f.msg(0, 20, "", "全員へ"), false, true, f.t += 10);
  f.ui.onMessage(f.msg(0, 21, "", "あなたへ"), false, false, f.t += 10);
  CHECK(f.st.countIn(kChBroadcast) == 1 && f.st.countIn(1) == 3);

  // 自分が話した文は、チャンネルには入るが、知らせない
  f.back();
  Message own = f.msg(1, 30, "自分", "テスト");
  own.own = true;
  f.ui.onMessage(own, true, false, f.t += 10);
  CHECK(f.ui.overlay() == Overlay::None && f.st.countIn(1) == 4);
  CHECK(f.drain() == 0);
}

static void test_ui_instruction() {
  puts("ui: instruction");
  Action a[4];
  for (int choice = 0; choice < 3; choice++) {
    Fx f;
    Message m = f.msg(1, 13, "本部", "北口へ移動してください");
    m.instruction = true;
    f.ui.onMessage(m, true, false, f.t);
    CHECK(f.ui.overlay() == Overlay::Instruction);
    CHECK(f.line(0, "平常 指示 [圏外]") && f.line(1, "北口へ移動してください") && f.line(3, "決定=了解 選択=できない"));
    if (choice == 0) f.ok();
    else if (choice == 1) f.sel();
    else f.back();
    CHECK(f.ui.overlay() == Overlay::None && f.ui.screen() == Screen::Home);
    const size_t n = f.drain(a, 4);
    if (choice == 2) {
      CHECK(n == 0);  // あとで: 返事は出さない。文はチャンネルに残る
      CHECK(f.st.countIn(1) == 1);
    } else {
      CHECK(n == 1 && a[0].kind == Action::SendAck && a[0].mid == 13);
      CHECK(a[0].ackKind == (choice == 0 ? kAckAccept : kAckRefuse));
    }
  }
}

static void test_ui_send_signal() {
  puts("ui: send signal");
  Action a[4];
  Fx f;
  f.ui.setLink(true, false);
  f.sel();
  f.ok();
  CHECK(f.ui.screen() == Screen::SendType);
  CHECK(f.line(0, "平常 送る: タイプ") && f.line(1, ">音声") && f.line(2, " 了解") && f.line(3, " 到着"));

  // 了解: 詳細なし → 確認 → 送信
  f.sel();
  f.ok();
  CHECK(f.ui.screen() == Screen::SendConfirm);
  CHECK(f.line(1, "了解") && f.line(2, "決定 = 送信") && f.line(3, "戻る = やり直す"));
  CHECK(f.drain() == 0);  // 確認の画面に来ただけでは、まだ送らない
  f.ok();
  CHECK(f.ui.screen() == Screen::Result && f.ui.sendState() == SendState::Sending);
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::SendSignal);
  CHECK(a[0].code == 3 && a[0].n == 0 && a[0].dir == 0 && a[0].pos == 0);
  const uint16_t t1 = a[0].ticket;
  CHECK(f.line(1, "送信中"));
  f.ui.onSignalResult(t1, SendState::Delivered);
  CHECK(f.ui.sendState() == SendState::Delivered && f.line(1, "本部に届きました"));
  CHECK(f.st.countIn(kChLocalSent) == 1);
  CHECK(strcmp(f.st.nth(kChLocalSent, 0)->body, "了解 → 本部に届いた") == 0);
  f.ok();
  CHECK(f.ui.screen() == Screen::Home);

  // 移動します: 方角(必須)→ 数(任意。先頭は「指定なし」)→ 確認
  f.sel();
  f.ok();
  f.sel(3);
  f.ok();
  CHECK(f.ui.screen() == Screen::SendDir && f.line(1, ">北") && f.line(2, " 北東") && f.line(3, " 東"));
  f.sel();
  f.ok();
  CHECK(f.ui.screen() == Screen::SendCount);
  CHECK(f.line(1, ">指定なし") && f.line(2, " 1〜5人") && f.line(3, " 6〜10人"));
  f.sel(2);
  f.ok();
  CHECK(f.ui.screen() == Screen::SendConfirm && f.line(1, "北東へ移動 6〜10人"));
  // 戻るで、1 つずつ前へ。選んだ所に戻る
  f.back();
  CHECK(f.ui.screen() == Screen::SendCount);
  f.back();
  CHECK(f.ui.screen() == Screen::SendDir && f.ui.cursor() == 1);
  f.back();
  CHECK(f.ui.screen() == Screen::SendType && f.ui.cursor() == 3);
  f.ok();
  f.sel();
  f.ok();   // 北東
  f.ok();   // 指定なし
  CHECK(f.line(1, "北東へ移動"));
  f.ok();
  CHECK(f.drain(a, 4) == 1 && a[0].code == 9 && a[0].dir == 2 && a[0].n == 0);
  const uint16_t t2 = a[0].ticket;
  CHECK(t2 != t1);

  // 届かなかった → あとで届いた。「送った記録」に両方残る
  f.ui.onSignalResult(t2, SendState::Queued);
  CHECK(f.ui.sendState() == SendState::Queued && f.line(1, "届いていません") && f.line(2, "つながったら送ります"));
  f.ok();
  f.ui.onSignalResult(t2, SendState::Delivered);  // ほかの画面にいても、記録は残る
  CHECK(f.st.countIn(kChLocalSent) == 3);
  CHECK(strcmp(f.st.nth(kChLocalSent, 2)->body, "北東へ移動 → 本部に届いた") == 0);
  f.ui.onSignalResult(t2, SendState::Delivered);  // 済んだ番号の結果がもう一度来ても、増えない
  f.ui.onSignalResult(999, SendState::Delivered); // 知らない番号は無視
  CHECK(f.st.countIn(kChLocalSent) == 3);
}

static void test_ui_send_emergency() {
  puts("ui: send (emergency)");
  Action a[4];
  Fx f;
  f.ui.restoreMode(Mode::Emergency);
  f.ui.setLink(false, true);
  CHECK(f.line(0, "緊急 ホーム [LoRa]"));
  f.sel();
  f.ok();
  CHECK(f.line(1, ">SOS") && f.line(2, " 無事") && f.line(3, " 応援を頼む"));
  f.sel(3);  // 4 つ目からは、次のページ
  CHECK(f.line(1, ">要救助者あり") && f.line(2, " 負傷者あり") && f.line(3, " 食料がほしい"));
  f.ok();    // 要救助者あり: 数が必須なので「指定なし」は出ない
  CHECK(f.ui.screen() == Screen::SendCount && f.line(1, ">1〜5人") && f.line(2, " 6〜10人") && f.line(3, " 16〜20人"));
  f.sel(4);
  CHECK(f.line(1, " 46〜50人") && f.line(2, ">96〜100人"));
  f.sel();   // 最後の次は最初
  f.ok();
  CHECK(f.line(1, "要救助者あり 1〜5人"));
  f.ok();
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::SendSignal && a[0].code == 6 && a[0].n == 5 && a[0].dir == 0);

  // SOS: 詳細なし。確認して送る
  f.ok();
  f.sel();
  f.ok();
  f.ok();
  CHECK(f.ui.screen() == Screen::SendConfirm && f.line(1, "SOS"));
  f.ok();
  CHECK(f.drain(a, 4) == 1 && a[0].code == kCodeSos);

  // 9 個目(了解)まで回って、最初に戻る
  f.ok();
  f.sel();
  f.ok();
  f.sel(8);
  CHECK(f.line(1, " 到着") && f.line(2, " 移動します") && f.line(3, ">了解"));
  f.sel();
  CHECK(f.ui.cursor() == 0);
}

static void test_ui_mode() {
  puts("ui: mode");
  Action a[4];
  Fx f;
  f.ui.op(Op::ModeSwitch, false, f.t += 100);
  CHECK(f.ui.overlay() == Overlay::ModeConfirm && f.ui.mode() == Mode::Normal);
  CHECK(f.line(0, "平常 モード切替 [圏外]") && f.line(1, "緊急モードにしますか") && f.line(2, "決定 = はい") && f.line(3, "戻る = いいえ"));
  f.sel();   // 選択では何も起きない
  CHECK(f.ui.overlay() == Overlay::ModeConfirm);
  f.back();  // いいえ
  CHECK(f.ui.overlay() == Overlay::None && f.ui.mode() == Mode::Normal && f.drain() == 0);

  // 送る画面の途中からでも切り替えられる。切り替えたらホームへ
  f.sel();
  f.ok();
  f.ui.op(Op::ModeSwitch, false, f.t += 100);
  f.ok();
  CHECK(f.ui.mode() == Mode::Emergency && f.ui.screen() == Screen::Home && f.ui.overlay() == Overlay::None);
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::SetMode && a[0].mode == Mode::Emergency);
  CHECK(f.line(0, "緊急 ホーム [圏外]"));

  f.ui.op(Op::ModeSwitch, false, f.t += 100);
  CHECK(f.line(1, "平常モードに戻しますか"));
  f.ok();
  CHECK(f.ui.mode() == Mode::Normal);
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::SetMode && a[0].mode == Mode::Normal);

  // 本部からの一斉の切り替え
  f.ui.onServerMode(Mode::Normal, f.t);  // 同じモードなら、何も起きない
  CHECK(f.ui.overlay() == Overlay::None && f.drain() == 0);
  f.ok();
  f.ui.onServerMode(Mode::Emergency, f.t);
  CHECK(f.ui.mode() == Mode::Emergency && f.ui.screen() == Screen::Home && f.ui.overlay() == Overlay::Notice);
  CHECK(f.line(0, "緊急 本部から [圏外]") && f.line(1, "緊急モードになりました"));
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::SetMode && a[0].mode == Mode::Emergency);

  // 切り替えの確認を出している間に指示が届いても、確認は消えない(文はチャンネルに入る)
  f.back();
  f.ui.op(Op::ModeSwitch, false, f.t += 100);
  Message m = f.msg(1, 50, "本部", "待機");
  m.instruction = true;
  f.ui.onMessage(m, true, false, f.t);
  CHECK(f.ui.overlay() == Overlay::ModeConfirm && f.st.countIn(1) == 1);
}

static void toVoiceConfirm(Fx& f) {
  f.sel();
  f.ok();  // メッセージを送る
  f.ok();  // 音声
  f.ok();  // 宛先: 本部
}

static void test_ui_voice() {
  puts("ui: voice");
  Action a[4];
  {  // ボタン: 押している間だけ録音し、離すと送る
    Fx f;
    f.ui.setLink(true, false);
    f.sel();
    f.ok();
    f.ok();
    CHECK(f.ui.screen() == Screen::SendChannel && f.line(0, "平常 送る: 宛先") && f.line(1, ">本部"));
    f.ok();
    CHECK(f.ui.screen() == Screen::SendConfirm && !f.ui.recording());
    CHECK(f.line(1, "宛先: 本部") && f.line(2, "決定を押して話す"));
    CHECK(f.drain() == 0);

    f.ui.op(Op::OkDown, false, f.t = 10000);
    CHECK(f.ui.recording());
    CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::StartRecording && a[0].ch == 1);
    CHECK(f.line(1, "録音中 0 秒") && f.line(2, "離すと送信"));
    f.ui.tick(12300);
    CHECK(f.line(1, "録音中 2 秒"));
    f.ui.op(Op::OkHold, false, 10500);  // 長押しの知らせは、ここでは使わない
    CHECK(f.ui.recording());
    f.ui.op(Op::OkUp, false, 12500);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::Result && f.ui.sendState() == SendState::Sending);
    CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::StopRecording && a[0].send && a[0].ch == 1);
    f.ui.onVoiceResult(true);
    CHECK(f.ui.sendState() == SendState::VoiceSent && f.line(1, "送りました") && f.line(2, "文字になって届きます"));
  }
  {  // 短すぎる録音は捨てる
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::OkDown, false, f.t = 10000);
    f.ui.op(Op::Ok, false, 10100);
    f.ui.op(Op::OkUp, false, 10100);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::SendConfirm && f.ui.overlay() == Overlay::Notice);
    CHECK(f.drain(a, 4) == 2 && a[0].kind == Action::StartRecording && a[1].kind == Action::StopRecording && !a[1].send);
    CHECK(f.line(0, "平常 送りませんでした") && f.line(1, "短すぎます"));
  }
  {  // サーバーにつながっていないときは、録音を始めない
    Fx f;
    toVoiceConfirm(f);
    f.ui.op(Op::OkDown, false, f.t += 100);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::Result && f.ui.sendState() == SendState::NeedWifi);
    CHECK(f.line(1, "音声は Wi-Fi が必要です") && f.drain() == 0);
  }
  {  // 15 秒で自動的に止めて送る。そのあとで離しても、何も起きない
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::OkDown, false, f.t = 10000);
    f.ui.tick(24900);
    CHECK(f.ui.recording() && f.line(1, "録音中 14 秒"));
    f.ui.tick(25000);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::Result);
    CHECK(f.drain(a, 4) == 2 && a[1].kind == Action::StopRecording && a[1].send);
    f.ui.op(Op::OkUp, false, 26000);
    CHECK(f.ui.screen() == Screen::Result && f.drain() == 0);
  }
  {  // 録音中にほかのボタンが押されたら、捨てる
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::OkDown, false, f.t = 10000);
    f.ui.op(Op::OkAbort, false, 11000);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::SendConfirm);
    CHECK(f.drain(a, 4) == 2 && a[1].kind == Action::StopRecording && !a[1].send);
  }
  {  // 録音中に 3 つ同時押し: 録音を捨てて、切り替えの確認へ
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::OkDown, false, f.t = 10000);
    f.ui.op(Op::OkAbort, false, 10200);
    f.ui.op(Op::ModeSwitch, false, 11700);
    CHECK(!f.ui.recording() && f.ui.overlay() == Overlay::ModeConfirm);
    CHECK(f.drain(a, 4) == 2 && !a[1].send);
  }
  {  // タッチで始めた録音の最中に 3 つ同時押し(OkAbort は来ない): ここでも録音を捨てる
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::Ok, true, f.t = 10000);
    CHECK(f.ui.recording());
    f.ui.op(Op::ModeSwitch, false, 12000);
    CHECK(!f.ui.recording() && f.ui.overlay() == Overlay::ModeConfirm);
    CHECK(f.drain(a, 4) == 2 && a[1].kind == Action::StopRecording && !a[1].send);
    f.ui.tick(40000);  // 時間がたっても、あとから送られない
    CHECK(f.drain() == 0);
  }
  {  // タッチ: 2 回タップで始めて、もう一度 2 回タップで送る
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::Ok, true, f.t = 10000);
    CHECK(f.ui.recording() && f.line(2, "2回タップで送信"));
    f.ui.op(Op::Select, true, 10500);  // 1 回タップでは止まらない
    CHECK(f.ui.recording());
    f.ui.op(Op::Ok, true, 13000);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::Result);
    CHECK(f.drain(a, 4) == 2 && a[1].kind == Action::StopRecording && a[1].send);
    f.ui.onVoiceResult(false);
    CHECK(f.ui.sendState() == SendState::Failed && f.line(1, "送れませんでした"));
  }
  {  // 戻る: 録音していなければ宛先へ。録音中なら捨てる
    Fx f;
    f.ui.setLink(true, false);
    toVoiceConfirm(f);
    f.ui.op(Op::Ok, true, f.t = 10000);
    f.ui.op(Op::Back, true, 11000);
    CHECK(!f.ui.recording() && f.ui.screen() == Screen::SendConfirm);
    f.back();
    CHECK(f.ui.screen() == Screen::SendChannel);
    f.back();
    CHECK(f.ui.screen() == Screen::SendType && f.ui.cursor() == 0);
  }
}

static void test_ui_reader_talk() {
  puts("ui: reader talk shortcut");
  Action a[4];
  Fx f;
  f.ui.setLink(true, false);
  f.ok();
  f.ok();  // 本部を読む
  CHECK(f.ui.screen() == Screen::Reader);
  CHECK(f.drain() == 0);  // 短く押しただけでは、録音しない

  f.ui.op(Op::OkDown, false, f.t = 20000);
  f.ui.op(Op::OkHold, false, 20500);  // 長押しで、このチャンネルに話す
  CHECK(f.ui.recording());
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::StartRecording && a[0].ch == 1);
  f.ui.tick(21600);
  CHECK(f.line(0, "平常 本部") && f.line(1, "録音中 1 秒"));
  f.ui.op(Op::OkUp, false, 22000);
  CHECK(!f.ui.recording() && f.ui.screen() == Screen::Reader && f.ui.overlay() == Overlay::Notice);
  CHECK(f.drain(a, 4) == 1 && a[0].kind == Action::StopRecording && a[0].send);
  CHECK(f.line(0, "平常 送信中"));
  f.ui.onVoiceResult(true);
  CHECK(f.line(0, "平常 送りました") && f.line(1, "文字になって届きます"));
  f.ui.tick(22000 + 2600);
  CHECK(f.ui.overlay() == Overlay::None && f.ui.screen() == Screen::Reader);

  // つながっていないときは、知らせだけ
  f.ui.setLink(false, false);
  f.ui.op(Op::OkHold, false, f.t = 30000);
  CHECK(!f.ui.recording() && f.ui.overlay() == Overlay::Notice && f.line(1, "音声は Wi-Fi が必要です"));
  CHECK(f.drain() == 0);
  f.back();

  // タッチの長押しでは話さない。「送った記録」と「全体」には話せない。緊急時は話せない
  f.ui.setLink(true, false);
  f.ui.op(Op::OkHold, true, f.t += 100);
  CHECK(!f.ui.recording());
  f.back();
  f.sel(2);
  f.ok();
  CHECK(f.ui.readerChannel() == kChLocalSent);
  f.ui.op(Op::OkHold, false, f.t += 100);
  CHECK(!f.ui.recording());
  f.back();
  f.sel(2);   // 送った記録(2)→ 本部(0)→ 全体(1)
  f.ok();
  CHECK(f.ui.readerChannel() == kChBroadcast);
  f.ui.op(Op::OkHold, false, f.t += 100);
  CHECK(!f.ui.recording());
  f.ui.restoreMode(Mode::Emergency);
  f.back();
  f.sel(2);
  f.ok();
  CHECK(f.ui.readerChannel() == 1);
  f.ui.op(Op::OkHold, false, f.t += 100);
  CHECK(!f.ui.recording() && f.drain() == 0);
}

static void test_ui_config_change() {
  puts("ui: channel config");
  Fx f;
  f.ok();
  f.ok();  // 本部(ch 1)を読んでいる
  const Channel list[] = {mk(5, ChKind::Group, "北班"), mk(6, ChKind::Direct, "本部")};
  f.ch.setFromConfig(list, 2, 1);
  f.ui.onChannelsChanged();
  CHECK(f.ui.screen() == Screen::ChannelList);  // 読んでいたチャンネルが無くなったら、一覧へ戻る
  CHECK(f.line(1, ">北班") && f.line(2, " 本部") && f.line(3, " 全体"));
  f.sel(3);
  CHECK(f.line(1, ">送った記録"));  // 4 つ目は次のページ
  CHECK(f.recv(5, 1, "山田", "聞こえますか"));
  CHECK(f.st.countIn(5) == 1 && f.ch.at(0).unread == 1);

  // 話す宛先を選んでいる最中に、そのチャンネルが無くなったら、タイプへ戻る
  f.back();
  f.back();
  f.back();
  f.ui.setLink(true, false);
  f.sel();
  f.ok();
  f.ok();
  f.sel();
  f.ok();  // 宛先: 本部(ch 6)
  CHECK(f.ui.screen() == Screen::SendConfirm && f.line(1, "宛先: 本部"));
  const Channel only[] = {mk(5, ChKind::Group, "北班")};
  f.ch.setFromConfig(only, 1, 2);
  f.ui.onChannelsChanged();
  CHECK(f.ui.screen() == Screen::SendType);
}

// 1 行目は、幅が足りないとき、題名だけを縮める。ページ番号と、つながりの表示は残す
static void test_ui_header() {
  puts("ui: header");
  Fx f;
  const Channel list[] = {mk(5, ChKind::Group, "あいうえおかきく")};  // 幅 16 の名前
  f.ch.setFromConfig(list, 1, 1);
  f.ok();
  f.ok();
  CHECK(f.ui.screen() == Screen::Reader);
  CHECK(f.line(0, "平常 あいうえ 1/1 [圏外]"));   // 24 = 平常(4)+空白+題名(8)+ページ(4)+表示(7)
  f.ui.setLink(true, false);
  CHECK(f.line(0, "平常 あいうえおかき 1/1"));    // 表示が無いと、題名に 15 使える(全角 7 文字)
  f.ui.restoreMode(Mode::Emergency);
  f.ui.setLink(false, true);
  CHECK(f.line(0, "緊急 あいうえ 1/1 [LoRa]"));
  f.back();
  f.back();
  f.sel();
  f.ok();
  CHECK(f.line(0, "緊急 送る: タイプ [LoRa]"));    // ちょうど 24
}

// どの画面のどの行も、決めた幅に収まっている
static void test_ui_width() {
  puts("ui: width");
  for (uint16_t cols : {16, 24, 40}) {
    Channels ch;
    Messages st;
    UiConfig cfg;
    cfg.cols = cols;
    Ui ui(ch, st, cfg);
    const Channel list[] = {mk(5, ChKind::Group, "とても長い名前の北班"), mk(6, ChKind::Direct, "本部")};
    ch.setFromConfig(list, 2, 1);
    ui.setLink(true, true);
    uint32_t t = 1000;
    auto check = [&]() {
      View v;
      ui.render(v);
      for (size_t i = 0; i < kViewLines; i++) CHECK(textWidth(v.line[i]) <= cols);
    };
    const Op script[] = {Op::Ok,     Op::Ok,     Op::Back,   Op::Select, Op::Ok,     Op::Back, Op::Back, Op::Select,
                         Op::Ok,     Op::Select, Op::Select, Op::Select, Op::Ok,     Op::Ok,   Op::Ok,   Op::Ok,
                         Op::Ok,     Op::ModeSwitch, Op::Ok, Op::Select, Op::Ok,     Op::Select, Op::Select, Op::Select,
                         Op::Ok,     Op::Select, Op::Ok,     Op::Ok,     Op::Back};
    check();
    for (Op o : script) {
      ui.op(o, false, t += 100);
      check();
    }
    Message m;
    m.ch = 5;
    m.mid = 1;
    copyText(m.from, sizeof m.from, "とても長い名前の人");
    copyText(m.body, sizeof m.body, "とても長い文です。とても長い文です。とても長い文です。abcdefghijklmnopqrstuvwxyz");
    ui.onMessage(m, true, false, t);
    check();
    m.mid = 2;
    m.instruction = true;
    ui.onMessage(m, true, false, t);
    check();
  }
}

void run_ui_tests() {
  test_codes();
  test_channels();
  test_store();
  test_ui_navigation();
  test_ui_reader();
  test_ui_incoming();
  test_ui_instruction();
  test_ui_send_signal();
  test_ui_send_emergency();
  test_ui_mode();
  test_ui_voice();
  test_ui_reader_talk();
  test_ui_config_change();
  test_ui_header();
  test_ui_width();
}
