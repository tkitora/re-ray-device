// 操作の種類と、ボタン・タッチの読み方(docs/DESIGN.md §3)。
// ボタンから来てもタッチから来ても、同じ「操作(Op)」に直してから画面に渡す。
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace reray {

enum class Op : uint8_t {
  None = 0,
  Back,        // 戻る(押して離した)
  Select,      // 選択(押して離した)
  Ok,          // 決定(短く押して離した)
  OkDown,      // 決定だけを押し始めた(話す画面で、録音をすぐ始めるため)
  OkHold,      // 決定だけを holdMs 以上押し続けている
  OkUp,        // OkDown のあとで決定を離した
  OkAbort,     // OkDown のあとで、ほかのボタンも押された(録音を捨てる)
  ModeSwitch,  // 3 つ同時に chordMs 押し続けた
};

constexpr uint8_t kBtnBack = 1 << 0;
constexpr uint8_t kBtnSelect = 1 << 1;
constexpr uint8_t kBtnOk = 1 << 2;
constexpr uint8_t kBtnAll = kBtnBack | kBtnSelect | kBtnOk;

struct ButtonConfig {
  uint16_t debounceMs = 30;  // チャタリングを捨てる時間
  uint16_t holdMs = 500;     // 決定の長押しとみなす時間
  uint16_t chordMs = 1500;   // 3 つ同時押しでモードを切り替えるまでの時間
};

// ボタン 3 つの押され方から、操作を作る。update() を数ミリ秒ごとに呼び、next() で取り出す。
//  - 1 つだけ押した操作は、離したときに出す(同時押しの途中で、誤って出ないように)。
//  - 2 つ以上を押した場合は、3 つそろって chordMs たったときの ModeSwitch だけが出る。
class ButtonDecoder {
 public:
  explicit ButtonDecoder(const ButtonConfig& cfg = ButtonConfig()) : cfg_(cfg) {}

  // rawMask: 押されているボタンのビット(kBtn*)。
  void update(uint8_t rawMask, uint32_t nowMs) {
    rawMask &= kBtnAll;
    if (rawMask != lastRaw_) {
      lastRaw_ = rawMask;
      lastChangeMs_ = nowMs;
    }
    if (rawMask != stable_ && (uint32_t)(nowMs - lastChangeMs_) >= cfg_.debounceMs) {
      const uint8_t prev = stable_;
      stable_ = rawMask;
      onChange(prev, stable_, nowMs);
    }
    onTime(nowMs);
  }

  Op next() {
    if (count_ == 0) return Op::None;
    const Op o = q_[head_];
    head_ = (head_ + 1) % kQueue;
    count_--;
    return o;
  }

 private:
  static constexpr size_t kQueue = 8;

  void push(Op o) {
    if (count_ == kQueue) return;  // あふれたら捨てる(通常は起きない)
    q_[(head_ + count_) % kQueue] = o;
    count_++;
  }

  void onChange(uint8_t prev, uint8_t cur, uint32_t nowMs) {
    if (prev == 0 && cur != 0) {  // 押し始め
      seen_ = cur;
      consumed_ = false;
      okDown_ = false;
      okAborted_ = false;
      holdSent_ = false;
      if (cur == kBtnOk) {
        okDown_ = true;
        okDownMs_ = nowMs;
        push(Op::OkDown);
      }
    } else if (cur != 0) {  // 途中でボタンが増えた・減った
      seen_ |= cur;
      if (okDown_ && !okAborted_ && (cur & ~kBtnOk)) {
        okAborted_ = true;
        push(Op::OkAbort);
      }
    }

    chordActive_ = (cur == kBtnAll);
    if (chordActive_) chordStartMs_ = nowMs;

    if (cur == 0 && prev != 0) {  // 全部離した
      if (!consumed_) {
        if (seen_ == kBtnBack) push(Op::Back);
        else if (seen_ == kBtnSelect) push(Op::Select);
        else if (seen_ == kBtnOk) {
          if (!holdSent_) push(Op::Ok);
          push(Op::OkUp);
        }
      }
      seen_ = 0;
      okDown_ = false;
    }
  }

  void onTime(uint32_t nowMs) {
    if (chordActive_ && !consumed_ && (uint32_t)(nowMs - chordStartMs_) >= cfg_.chordMs) {
      consumed_ = true;
      push(Op::ModeSwitch);
    }
    if (okDown_ && !okAborted_ && !holdSent_ && stable_ == kBtnOk &&
        (uint32_t)(nowMs - okDownMs_) >= cfg_.holdMs) {
      holdSent_ = true;
      push(Op::OkHold);
    }
  }

  ButtonConfig cfg_;
  uint8_t lastRaw_ = 0;
  uint8_t stable_ = 0;
  uint32_t lastChangeMs_ = 0;
  uint8_t seen_ = 0;        // この押し方のあいだに押されたボタン
  bool consumed_ = false;   // ModeSwitch を出した
  bool okDown_ = false;
  bool okAborted_ = false;
  bool holdSent_ = false;
  uint32_t okDownMs_ = 0;
  bool chordActive_ = false;
  uint32_t chordStartMs_ = 0;
  Op q_[kQueue] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};

// GUIDE01 のタッチ。値は SDK の g01_get_touch_consts() から取った種類に、呼び出し側で直して渡す。
enum class Touch : uint8_t { SingleClick, DoubleClick, SwipeForward, SwipeBack, LongPress };

// タッチを操作に直す。同じ通知が続けて届く(実機で 3 回ずつ届いた)ので、短い間の同じ通知は 1 回にまとめる。
class TouchMapper {
 public:
  explicit TouchMapper(uint16_t dedupMs = 300) : dedupMs_(dedupMs) {}

  Op map(Touch t, uint32_t nowMs) {
    if (has_ && t == last_ && (uint32_t)(nowMs - lastMs_) < dedupMs_) return Op::None;
    has_ = true;
    last_ = t;
    lastMs_ = nowMs;
    switch (t) {
      case Touch::SingleClick: return Op::Select;
      case Touch::SwipeForward: return Op::Select;
      case Touch::DoubleClick: return Op::Ok;
      case Touch::SwipeBack: return Op::Back;
      case Touch::LongPress: return Op::None;  // 割り当てない(誤操作を防ぐ)
    }
    return Op::None;
  }

 private:
  uint16_t dedupMs_;
  bool has_ = false;
  Touch last_ = Touch::SingleClick;
  uint32_t lastMs_ = 0;
};

}  // namespace reray
