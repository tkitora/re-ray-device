// 画面の状態遷移(docs/DESIGN.md §2、§4〜§6)。ハードには触らない。
//
//   入るもの: 操作(Op)、時刻、届いたメッセージ、送った結果、つながりの状態
//   出るもの: グラスに出す 4 行(View)と、端末にしてほしい動作(Action)
//
// ボタンの意味と画面の進み方は、平常時も緊急時も同じ。変わるのは「送れるものの一覧」。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reray/channels.h"
#include "reray/codes.h"
#include "reray/input.h"
#include "reray/text_layout.h"

namespace reray {

using Channels = ChannelList<8>;
using Messages = MessageStore<64>;

struct UiConfig {
  uint16_t cols = 24;             // 1 行の幅(半角 = 1、全角 = 2)。実機で測って直す
  uint32_t maxTalkMs = 15000;     // 1 回に話せる長さ
  uint32_t minTalkMs = 300;       // これより短い録音は捨てる
  uint32_t noticeMs = 8000;       // 届いた知らせを出しておく時間
  uint32_t shortNoticeMs = 2500;  // 短い知らせ(「送りました」など)
};

constexpr size_t kViewLines = 4;  // 1 行目 = 状態、2〜4 行目 = 中身
constexpr size_t kBodyLines = 3;
constexpr size_t kLineBytes = 100;

struct View {
  char line[kViewLines][kLineBytes];
  bool operator==(const View& o) const { return memcmp(line, o.line, sizeof line) == 0; }
  bool operator!=(const View& o) const { return !(*this == o); }
};

enum class Screen : uint8_t { Home, ChannelList, Reader, SendType, SendDir, SendCount, SendChannel, SendConfirm, Result };
enum class Overlay : uint8_t { None, ModeConfirm, Notice, Instruction };
enum class SendState : uint8_t { Idle, Sending, Delivered, Queued, VoiceSent, Failed, NeedWifi };

constexpr uint8_t kAckAccept = 4;  // 了解
constexpr uint8_t kAckRefuse = 5;  // できない

struct Action {
  enum Kind : uint8_t { None, SendSignal, StartRecording, StopRecording, SendAck, SetMode };
  Kind kind = None;
  uint16_t ticket = 0;  // SendSignal: 結果を知らせるときに返してもらう番号
  uint8_t code = 0, n = 0, dir = 0, pos = 0;
  uint8_t ch = 0;       // StartRecording / StopRecording: 宛先のチャンネル
  bool send = false;    // StopRecording: true = 送る、false = 捨てる
  uint32_t mid = 0;     // SendAck: どの文への返事か
  uint8_t ackKind = 0;  // SendAck: 4 = 了解、5 = できない
  Mode mode = Mode::Normal;  // SetMode
};

class Ui {
 public:
  Ui(Channels& channels, Messages& messages, const UiConfig& cfg = UiConfig())
      : ch_(channels), store_(messages), cfg_(cfg) {}

  // ---- 入るもの ----

  // 起動時に、覚えていたモードを戻す(動作は出さない)
  void restoreMode(Mode m) { mode_ = m; dirty_ = true; }

  void op(Op o, bool fromTouch, uint32_t nowMs) {
    now_ = nowMs;
    if (o == Op::None) return;
    dirty_ = true;
    if (o == Op::ModeSwitch) {
      cancelRecording();
      overlay_ = Overlay::ModeConfirm;
      return;
    }
    if (overlay_ != Overlay::None) {
      overlayOp(o);
      return;
    }
    switch (screen_) {
      case Screen::Home: homeOp(o); break;
      case Screen::ChannelList: channelListOp(o); break;
      case Screen::Reader: readerOp(o, fromTouch); break;
      case Screen::SendType: sendTypeOp(o); break;
      case Screen::SendDir: sendDirOp(o); break;
      case Screen::SendCount: sendCountOp(o); break;
      case Screen::SendChannel: sendChannelOp(o); break;
      case Screen::SendConfirm: sendConfirmOp(o, fromTouch); break;
      case Screen::Result:
        if (o == Op::Back || o == Op::Ok) enter(Screen::Home, 0);
        break;
    }
  }

  // 定期的に呼ぶ(録音の時間切れ、知らせを消す、録音中の秒数の表示)
  void tick(uint32_t nowMs) {
    now_ = nowMs;
    if (rec_.active) {
      if ((uint32_t)(nowMs - rec_.startMs) >= cfg_.maxTalkMs) {
        finishRecording(true);
        dirty_ = true;
      } else {
        const uint32_t sec = (uint32_t)(nowMs - rec_.startMs) / 1000;
        if (sec != rec_.shownSec) {
          rec_.shownSec = sec;
          dirty_ = true;
        }
      }
    }
    if (overlay_ == Overlay::Notice && noticeTimed_ && (int32_t)(nowMs - noticeUntil_) >= 0) {
      overlay_ = Overlay::None;
      dirty_ = true;
    }
  }

  // 届いた文を入れる。hasCh = チャンネル番号が付いていた、toAll = 全員あてだった。
  // 戻り値は、新しく入ったか(同じものが 2 回届いたときは false)。
  bool onMessage(const Message& in, bool hasCh, bool toAll, uint32_t nowMs) {
    now_ = nowMs;
    Message m = in;
    m.ch = ch_.resolve(hasCh, in.ch, toAll);
    const bool viewing = screen_ == Screen::Reader && readerCh_ == m.ch;
    const bool atLast = viewing && readerPage_ + 1 >= readerPages();
    if (!store_.add(m)) return false;
    dirty_ = true;

    if (viewing) {
      if (atLast) readerPage_ = readerPages() - 1;
    } else {
      const int i = ch_.indexOf(m.ch);
      if (i >= 0 && ch_.at((size_t)i).unread < 99) ch_.at((size_t)i).unread++;
    }
    if (m.own) return true;  // 自分が話したものは、知らせない

    if (m.instruction) {
      if (overlay_ != Overlay::ModeConfirm) {
        overlay_ = Overlay::Instruction;
        instrMid_ = m.mid;
        copyText(noticeBody_, sizeof noticeBody_, m.body);
      }
    } else if (overlay_ == Overlay::None && !rec_.active && !viewing) {
      char title[kLineBytes];
      const int i = ch_.indexOf(m.ch);
      snprintf(title, sizeof title, "新着 %s", i >= 0 ? ch_.at((size_t)i).name : "");
      char body[kFromMax + kBodyMax + 4];
      composeMessage(m, body, sizeof body);
      showNotice(title, body, cfg_.noticeMs);
    }
    return true;
  }

  // 定型信号を送った結果(ticket は SendSignal の動作に付けた番号)
  void onSignalResult(uint16_t ticket, SendState s) {
    for (size_t i = 0; i < kTickets; i++) {
      Ticket& t = tickets_[i];
      if (!t.used || t.id != ticket) continue;
      const char* tail = s == SendState::Delivered ? "本部に届いた"
                         : s == SendState::Queued  ? "届いていない(あとで送ります)"
                         : s == SendState::Failed  ? "送れなかった"
                                                   : nullptr;
      if (tail) {  // 「送った記録」に残す
        Message m;
        m.ch = kChLocalSent;
        m.own = true;
        snprintf(m.body, sizeof m.body, "%s → %s", t.summary, tail);
        store_.add(m);
      }
      if (s == SendState::Delivered || s == SendState::Failed) t.used = false;
      if (screen_ == Screen::Result && shownTicket_ == ticket && !resultIsVoice_) sendState_ = s;
      dirty_ = true;
      return;
    }
  }

  // 音声を送った結果
  void onVoiceResult(bool ok) {
    dirty_ = true;
    if (screen_ == Screen::Result && resultIsVoice_) {
      sendState_ = ok ? SendState::VoiceSent : SendState::Failed;
    } else if (voiceFromReader_) {
      showNotice(ok ? "送りました" : "送れませんでした", ok ? "文字になって届きます" : "", cfg_.shortNoticeMs);
    }
    voiceFromReader_ = false;
  }

  // 本部からの一斉の切り替え
  void onServerMode(Mode m, uint32_t nowMs) {
    now_ = nowMs;
    if (m == mode_) return;
    cancelRecording();
    mode_ = m;
    enter(Screen::Home, 0);
    pushSetMode();
    showNotice("本部から", m == Mode::Emergency ? "緊急モードになりました" : "平常モードに戻りました", cfg_.noticeMs);
    dirty_ = true;
  }

  // チャンネルの一覧が変わった(config が届いた)
  void onChannelsChanged() {
    dirty_ = true;
    if (screen_ == Screen::Reader && ch_.indexOf(readerCh_) < 0) {
      cancelRecording();
      enter(Screen::ChannelList, 0);
    }
    if (screen_ == Screen::ChannelList && cursor_ >= ch_.count()) cursor_ = 0;
    if (screen_ == Screen::SendChannel && cursor_ >= ch_.talkableCount()) cursor_ = 0;
    if (screen_ == Screen::SendConfirm && isVoice() && !isTalkable(voiceCh_)) {
      cancelRecording();
      enter(Screen::SendType, 0);
    }
  }

  void setLink(bool serverUp, bool loraUsable) {
    if (serverUp != serverUp_ || loraUsable != loraUsable_) dirty_ = true;
    serverUp_ = serverUp;
    loraUsable_ = loraUsable;
  }

  // ---- 出るもの ----

  bool nextAction(Action& a) {
    if (actCount_ == 0) return false;
    a = acts_[actHead_];
    actHead_ = (actHead_ + 1) % kActs;
    actCount_--;
    return true;
  }

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

  void render(View& v) const {
    for (size_t i = 0; i < kViewLines; i++) v.line[i][0] = '\0';
    char body[kBodyLines][kLineBytes * 2] = {};
    char title[kLineBytes * 2] = {};
    char tail[16] = {};  // ページ番号など。題名を縮めても、これは残す

    if (overlay_ == Overlay::ModeConfirm) {
      snprintf(title, sizeof title, "モード切替");
      snprintf(body[0], sizeof body[0], "%s", mode_ == Mode::Normal ? "緊急モードにしますか" : "平常モードに戻しますか");
      snprintf(body[1], sizeof body[1], "決定 = はい");
      snprintf(body[2], sizeof body[2], "戻る = いいえ");
    } else if (overlay_ == Overlay::Notice) {
      snprintf(title, sizeof title, "%s", noticeTitle_);
      wrapInto(noticeBody_, body, kBodyLines);
    } else if (overlay_ == Overlay::Instruction) {
      snprintf(title, sizeof title, "指示");
      wrapInto(noticeBody_, body, 2);
      snprintf(body[2], sizeof body[2], "決定=了解 選択=できない");
    } else {
      renderScreen(title, sizeof title, tail, sizeof tail, body);
    }

    const char* modeLabel = mode_ == Mode::Emergency ? "緊急" : "平常";
    const char* link = serverUp_ ? "" : (mode_ == Mode::Emergency && loraUsable_) ? " [LoRa]" : " [圏外]";
    const uint16_t fixed = (uint16_t)(textWidth(modeLabel) + 1 + textWidth(tail) + textWidth(link));
    char fitted[kLineBytes];
    fitText(title, cfg_.cols > fixed ? (uint16_t)(cfg_.cols - fixed) : 0, fitted, sizeof fitted);
    char head[kLineBytes * 3];
    snprintf(head, sizeof head, "%s %s%s%s", modeLabel, fitted, tail, link);
    fitText(head, cfg_.cols, v.line[0], kLineBytes);
    for (size_t i = 0; i < kBodyLines; i++) fitText(body[i], cfg_.cols, v.line[i + 1], kLineBytes);
  }

  // ---- 状態(テストと、端末側の判断に使う) ----
  Mode mode() const { return mode_; }
  Screen screen() const { return screen_; }
  Overlay overlay() const { return overlay_; }
  bool recording() const { return rec_.active; }
  SendState sendState() const { return sendState_; }
  size_t cursor() const { return cursor_; }
  uint8_t readerChannel() const { return readerCh_; }
  size_t readerPage() const { return readerPage_; }

  size_t readerPages() const {
    const size_t lines = readerTotalLines();
    return lines == 0 ? 1 : (lines + kBodyLines - 1) / kBodyLines;
  }

 private:
  // ---- 画面ごとの操作 ----

  void homeOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % 2;
    else if (o == Op::Ok) enter(cursor_ == 0 ? Screen::ChannelList : Screen::SendType, 0);
  }

  void channelListOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % ch_.count();
    else if (o == Op::Back) enter(Screen::Home, 0);
    else if (o == Op::Ok) {
      readerCh_ = ch_.at(cursor_).ch;
      ch_.at(cursor_).unread = 0;
      screen_ = Screen::Reader;
      readerPage_ = readerPages() - 1;  // いちばん新しい所から
    }
  }

  void readerOp(Op o, bool fromTouch) {
    if (rec_.active) {
      if (o == Op::OkUp) finishRecording(true);
      else if (o == Op::OkAbort || o == Op::Back) cancelRecording();
      return;
    }
    const size_t pages = readerPages();
    if (o == Op::Select) {  // 下へ(新しい方へ)
      if (readerPage_ + 1 < pages) readerPage_++;
    } else if (o == Op::Ok) {  // 上へ(古い方へ)
      if (readerPage_ > 0) readerPage_--;
    } else if (o == Op::Back) {
      const int i = ch_.indexOf(readerCh_);
      enter(Screen::ChannelList, i >= 0 ? (size_t)i : 0);
    } else if (o == Op::OkHold && !fromTouch) {  // 近道: 決定の長押しで、このチャンネルに話す
      if (mode_ != Mode::Normal || !isTalkable(readerCh_)) return;
      if (!serverUp_) {
        showNotice("送れません", "音声は Wi-Fi が必要です", cfg_.shortNoticeMs);
        return;
      }
      startRecording(readerCh_, true);
    }
  }

  void sendTypeOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % sendTypeCount(mode_);
    else if (o == Op::Back) enter(Screen::Home, 1);
    else if (o == Op::Ok) {
      typeIdx_ = cursor_;
      dir_ = 0;
      n_ = 0;
      const SendType& t = type();
      if (t.code == kCodeVoice) {
        if (ch_.talkableCount() == 0) showNotice("送れません", "話せるチャンネルがありません", cfg_.shortNoticeMs);
        else enter(Screen::SendChannel, 0);
      } else if (t.direction) enter(Screen::SendDir, 0);
      else if (t.count != Need::No) enter(Screen::SendCount, 0);
      else enter(Screen::SendConfirm, 0);
    }
  }

  void sendDirOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % kDirCount;
    else if (o == Op::Back) enter(Screen::SendType, typeIdx_);
    else if (o == Op::Ok) {
      dir_ = (uint8_t)(cursor_ + 1);
      enter(type().count != Need::No ? Screen::SendCount : Screen::SendConfirm, 0);
    }
  }

  size_t countItems() const { return countChoices(type()) + (type().count == Need::Optional ? 1 : 0); }
  // 数量の候補。任意のときは、先頭が「指定なし」(0)
  uint8_t countValue(size_t i) const {
    if (type().count == Need::Optional) return i == 0 ? 0 : type().counts[i - 1];
    return type().counts[i];
  }

  void sendCountOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % countItems();
    else if (o == Op::Back) {
      if (type().direction) enter(Screen::SendDir, dir_ > 0 ? (size_t)(dir_ - 1) : 0);
      else enter(Screen::SendType, typeIdx_);
    } else if (o == Op::Ok) {
      n_ = countValue(cursor_);
      enter(Screen::SendConfirm, 0);
    }
  }

  void sendChannelOp(Op o) {
    if (o == Op::Select) cursor_ = (cursor_ + 1) % ch_.talkableCount();
    else if (o == Op::Back) enter(Screen::SendType, typeIdx_);
    else if (o == Op::Ok) {
      voiceCh_ = ch_.talkable(cursor_).ch;
      enter(Screen::SendConfirm, 0);
    }
  }

  void sendConfirmOp(Op o, bool fromTouch) {
    if (isVoice()) {
      if (rec_.active) {
        // ボタン: 離したら送る。タッチ: もう一度 2 回タップで送る
        if ((!fromTouch && o == Op::OkUp) || (fromTouch && o == Op::Ok)) finishRecording(true);
        else if (o == Op::OkAbort || o == Op::Back) cancelRecording();
        return;
      }
      if ((!fromTouch && o == Op::OkDown) || (fromTouch && o == Op::Ok)) {
        if (!serverUp_) {
          resultIsVoice_ = true;
          sendState_ = SendState::NeedWifi;
          screen_ = Screen::Result;
          return;
        }
        startRecording(voiceCh_, false);
        rec_.byTouch = fromTouch;
      } else if (o == Op::Back) {
        const int i = talkableIndex(voiceCh_);
        enter(Screen::SendChannel, i >= 0 ? (size_t)i : 0);
      }
      return;
    }
    if (o == Op::Back) {
      const SendType& t = type();
      if (t.count != Need::No) enter(Screen::SendCount, 0);
      else if (t.direction) enter(Screen::SendDir, dir_ > 0 ? (size_t)(dir_ - 1) : 0);
      else enter(Screen::SendType, typeIdx_);
    } else if (o == Op::Ok) {
      Action a;
      a.kind = Action::SendSignal;
      a.ticket = ++ticketSeq_;
      a.code = type().code;
      a.n = n_;
      a.dir = dir_;
      a.pos = 0;  // ポジションの配り方が決まるまで、0(なし)で送る
      pushAction(a);
      Ticket& t = tickets_[ticketNext_];
      ticketNext_ = (ticketNext_ + 1) % kTickets;
      t.used = true;
      t.id = a.ticket;
      formatSignal(type(), n_, dir_, t.summary, sizeof t.summary);
      shownTicket_ = a.ticket;
      resultIsVoice_ = false;
      sendState_ = SendState::Sending;
      screen_ = Screen::Result;
    }
  }

  void overlayOp(Op o) {
    switch (overlay_) {
      case Overlay::ModeConfirm:
        if (o == Op::Ok) {
          mode_ = mode_ == Mode::Normal ? Mode::Emergency : Mode::Normal;
          pushSetMode();
          overlay_ = Overlay::None;
          enter(Screen::Home, 0);
        } else if (o == Op::Back) {
          overlay_ = Overlay::None;
        }
        break;
      case Overlay::Notice:
        if (o == Op::Back || o == Op::Select || o == Op::Ok) overlay_ = Overlay::None;
        break;
      case Overlay::Instruction:
        if (o == Op::Ok || o == Op::Select) {
          Action a;
          a.kind = Action::SendAck;
          a.mid = instrMid_;
          a.ackKind = o == Op::Ok ? kAckAccept : kAckRefuse;
          pushAction(a);
          overlay_ = Overlay::None;
        } else if (o == Op::Back) {
          overlay_ = Overlay::None;  // あとで(チャンネルには残っている)
        }
        break;
      case Overlay::None: break;
    }
  }

  // ---- 録音 ----

  void startRecording(uint8_t ch, bool fromReader) {
    rec_.active = true;
    rec_.ch = ch;
    rec_.startMs = now_;
    rec_.shownSec = 0;
    rec_.fromReader = fromReader;
    rec_.byTouch = false;
    Action a;
    a.kind = Action::StartRecording;
    a.ch = ch;
    pushAction(a);
  }

  void finishRecording(bool send) {
    if (!rec_.active) return;
    rec_.active = false;
    const bool tooShort = (uint32_t)(now_ - rec_.startMs) < cfg_.minTalkMs;
    Action a;
    a.kind = Action::StopRecording;
    a.ch = rec_.ch;
    a.send = send && !tooShort;
    pushAction(a);
    if (!a.send) {
      if (send) showNotice("送りませんでした", "短すぎます", cfg_.shortNoticeMs);
      return;
    }
    if (rec_.fromReader) {
      voiceFromReader_ = true;
      showNotice("送信中", "", cfg_.shortNoticeMs);
    } else {
      resultIsVoice_ = true;
      sendState_ = SendState::Sending;
      screen_ = Screen::Result;
    }
  }

  void cancelRecording() {
    if (!rec_.active) return;
    rec_.active = false;
    Action a;
    a.kind = Action::StopRecording;
    a.ch = rec_.ch;
    a.send = false;
    pushAction(a);
  }

  // ---- 小さな道具 ----

  void enter(Screen s, size_t cursor) {
    screen_ = s;
    cursor_ = cursor;
  }

  const SendType& type() const { return sendType(mode_, typeIdx_); }
  bool isVoice() const { return type().code == kCodeVoice; }

  int talkableIndex(uint8_t ch) const {
    for (size_t i = 0; i < ch_.talkableCount(); i++)
      if (ch_.talkable(i).ch == ch) return (int)i;
    return -1;
  }
  bool isTalkable(uint8_t ch) const { return talkableIndex(ch) >= 0; }

  void pushAction(const Action& a) {
    if (actCount_ == kActs) return;
    acts_[(actHead_ + actCount_) % kActs] = a;
    actCount_++;
  }

  void pushSetMode() {
    Action a;
    a.kind = Action::SetMode;
    a.mode = mode_;
    pushAction(a);
  }

  void showNotice(const char* title, const char* body, uint32_t ms) {
    overlay_ = Overlay::Notice;
    copyText(noticeTitle_, sizeof noticeTitle_, title);
    copyText(noticeBody_, sizeof noticeBody_, body);
    noticeTimed_ = ms > 0;
    noticeUntil_ = now_ + ms;
  }

  static void composeMessage(const Message& m, char* out, size_t cap) {
    if (m.from[0]) snprintf(out, cap, "%s: %s", m.from, m.body);
    else snprintf(out, cap, "%s", m.body);
  }

  // text を折り返して、先頭から max 行を body に入れる
  void wrapInto(const char* text, char body[][kLineBytes * 2], size_t max) const {
    Span sp[kBodyLines];
    const size_t n = wrapText(text, strlen(text), cfg_.cols, sp, max < kBodyLines ? max : kBodyLines);
    for (size_t i = 0; i < n && i < max && i < kBodyLines; i++) {
      memcpy(body[i], text + sp[i].off, sp[i].len);
      body[i][sp[i].len] = '\0';
    }
  }

  // ---- 読む画面の行 ----

  static constexpr size_t kMaxLinesPerMessage = 48;

  size_t readerTotalLines() const {
    size_t total = 0;
    for (size_t i = 0;; i++) {
      const Message* m = store_.nth(readerCh_, i);
      if (!m) break;
      char text[kFromMax + kBodyMax + 4];
      composeMessage(*m, text, sizeof text);
      total += wrapText(text, strlen(text), cfg_.cols, nullptr, 0);
    }
    return total;
  }

  // 全体の中の idx 行目を out に入れる。無ければ false
  bool readerLine(size_t idx, char* out, size_t cap) const {
    for (size_t i = 0;; i++) {
      const Message* m = store_.nth(readerCh_, i);
      if (!m) return false;
      char text[kFromMax + kBodyMax + 4];
      composeMessage(*m, text, sizeof text);
      Span sp[kMaxLinesPerMessage];
      size_t n = wrapText(text, strlen(text), cfg_.cols, sp, kMaxLinesPerMessage);
      if (n > kMaxLinesPerMessage) n = kMaxLinesPerMessage;
      if (idx < n) {
        const size_t len = sp[idx].len < cap - 1 ? sp[idx].len : cap - 1;
        memcpy(out, text + sp[idx].off, len);
        out[len] = '\0';
        return true;
      }
      idx -= n;
    }
  }

  // ---- 表示 ----

  // 一覧を 3 つずつのページで出す。選んでいる項目に「>」を付ける
  template <typename NameFn>
  void renderMenu(char body[][kLineBytes * 2], size_t count, NameFn name) const {
    const size_t start = cursor_ - (cursor_ % kBodyLines);
    for (size_t i = 0; i < kBodyLines && start + i < count; i++) {
      char item[kLineBytes];
      name(start + i, item, sizeof item);
      snprintf(body[i], kLineBytes * 2, "%s%s", start + i == cursor_ ? ">" : " ", item);
    }
  }

  void renderScreen(char* title, size_t titleCap, char* tail, size_t tailCap, char body[][kLineBytes * 2]) const {
    const size_t L = kLineBytes * 2;
    switch (screen_) {
      case Screen::Home:
        snprintf(title, titleCap, "ホーム");
        renderMenu(body, 2, [](size_t i, char* o, size_t c) {
          snprintf(o, c, "%s", i == 0 ? "チャンネルを見る" : "メッセージを送る");
        });
        break;
      case Screen::ChannelList:
        snprintf(title, titleCap, "チャンネル");
        renderMenu(body, ch_.count(), [this](size_t i, char* o, size_t c) {
          const Channel& ch = ch_.at(i);
          if (ch.unread) snprintf(o, c, "%s (%u)", ch.name, (unsigned)ch.unread);
          else snprintf(o, c, "%s", ch.name);
        });
        break;
      case Screen::Reader: {
        const int i = ch_.indexOf(readerCh_);
        const char* name = i >= 0 ? ch_.at((size_t)i).name : "";
        if (rec_.active) {
          snprintf(title, titleCap, "%s", name);
          snprintf(body[0], L, "録音中 %u 秒", (unsigned)rec_.shownSec);
          snprintf(body[1], L, "離すと送信");
          break;
        }
        snprintf(title, titleCap, "%s", name);
        snprintf(tail, tailCap, " %u/%u", (unsigned)(readerPage_ + 1), (unsigned)readerPages());
        bool any = false;
        for (size_t k = 0; k < kBodyLines; k++) any = readerLine(readerPage_ * kBodyLines + k, body[k], L) || any;
        if (!any) snprintf(body[0], L, "(まだありません)");
        break;
      }
      case Screen::SendType:
        snprintf(title, titleCap, "送る: タイプ");
        renderMenu(body, sendTypeCount(mode_), [this](size_t i, char* o, size_t c) {
          snprintf(o, c, "%s", sendType(mode_, i).name);
        });
        break;
      case Screen::SendDir:
        snprintf(title, titleCap, "送る: 方角");
        renderMenu(body, kDirCount, [](size_t i, char* o, size_t c) { snprintf(o, c, "%s", dirName((uint8_t)(i + 1))); });
        break;
      case Screen::SendCount:
        snprintf(title, titleCap, "送る: 数");
        renderMenu(body, countItems(), [this](size_t i, char* o, size_t c) {
          const uint8_t n = countValue(i);
          if (n == 0) snprintf(o, c, "指定なし");
          else formatCount(n, type().unit, o, c);
        });
        break;
      case Screen::SendChannel:
        snprintf(title, titleCap, "送る: 宛先");
        renderMenu(body, ch_.talkableCount(), [this](size_t i, char* o, size_t c) {
          snprintf(o, c, "%s", ch_.talkable(i).name);
        });
        break;
      case Screen::SendConfirm:
        snprintf(title, titleCap, "送る: 確認");
        if (isVoice()) {
          const int i = ch_.indexOf(voiceCh_);
          if (rec_.active) {
            snprintf(body[0], L, "録音中 %u 秒", (unsigned)rec_.shownSec);
            snprintf(body[1], L, "%s", rec_.byTouch ? "2回タップで送信" : "離すと送信");
            snprintf(body[2], L, "戻る = やめる");
          } else {
            snprintf(body[0], L, "宛先: %s", i >= 0 ? ch_.at((size_t)i).name : "");
            snprintf(body[1], L, "決定を押して話す");
            snprintf(body[2], L, "離すと送信");
          }
        } else {
          char sum[80];
          formatSignal(type(), n_, dir_, sum, sizeof sum);
          snprintf(body[0], L, "%s", sum);
          snprintf(body[1], L, "決定 = 送信");
          snprintf(body[2], L, "戻る = やり直す");
        }
        break;
      case Screen::Result:
        snprintf(title, titleCap, "送る: 結果");
        switch (sendState_) {
          case SendState::Sending: snprintf(body[0], L, "送信中"); break;
          case SendState::Delivered: snprintf(body[0], L, "本部に届きました"); break;
          case SendState::Queued:
            snprintf(body[0], L, "届いていません");
            snprintf(body[1], L, "つながったら送ります");
            break;
          case SendState::VoiceSent:
            snprintf(body[0], L, "送りました");
            snprintf(body[1], L, "文字になって届きます");
            break;
          case SendState::Failed: snprintf(body[0], L, "送れませんでした"); break;
          case SendState::NeedWifi: snprintf(body[0], L, "音声は Wi-Fi が必要です"); break;
          case SendState::Idle: break;
        }
        snprintf(body[2], L, "決定 = ホームへ");
        break;
    }
  }

  // ---- 持っているもの ----

  struct Rec {
    bool active = false;
    uint8_t ch = 0;
    uint32_t startMs = 0;
    uint32_t shownSec = 0;
    bool fromReader = false;
    bool byTouch = false;
  };
  struct Ticket {
    bool used = false;
    uint16_t id = 0;
    char summary[80] = {};
  };
  static constexpr size_t kActs = 8;
  static constexpr size_t kTickets = 8;

  Channels& ch_;
  Messages& store_;
  UiConfig cfg_;

  Mode mode_ = Mode::Normal;
  Screen screen_ = Screen::Home;
  Overlay overlay_ = Overlay::None;
  size_t cursor_ = 0;
  uint32_t now_ = 0;
  bool dirty_ = true;

  uint8_t readerCh_ = 0;
  size_t readerPage_ = 0;

  size_t typeIdx_ = 0;
  uint8_t dir_ = 0;
  uint8_t n_ = 0;
  uint8_t voiceCh_ = 0;
  SendState sendState_ = SendState::Idle;
  bool resultIsVoice_ = false;
  uint16_t shownTicket_ = 0;
  bool voiceFromReader_ = false;

  Rec rec_;

  char noticeTitle_[kLineBytes] = {};
  char noticeBody_[kFromMax + kBodyMax + 4] = {};
  bool noticeTimed_ = false;
  uint32_t noticeUntil_ = 0;
  uint32_t instrMid_ = 0;

  bool serverUp_ = false;
  bool loraUsable_ = false;

  Action acts_[kActs];
  size_t actHead_ = 0;
  size_t actCount_ = 0;

  Ticket tickets_[kTickets];
  size_t ticketNext_ = 0;
  uint16_t ticketSeq_ = 0;
};

}  // namespace reray
