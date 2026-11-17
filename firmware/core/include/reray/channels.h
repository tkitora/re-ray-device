// チャンネルと、届いたメッセージの置き場(docs/DESIGN.md §5)。動的な確保はしない。
//
// チャンネルはサーバーが決め、端末ごとに見られるものを配る(config)。端末は次の 2 つを、いつも持つ:
//   0   = 全体(全員あての文が入る)
//   255 = 送った記録(この端末の中だけ)
// サーバーが決めるチャンネルは 1〜254。config が届く前は、1 =「本部」(1 対 1)だけがあるものとして動く。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace reray {

constexpr uint8_t kChBroadcast = 0;
constexpr uint8_t kChLocalSent = 255;
constexpr uint8_t kChDefaultDirect = 1;

enum class ChKind : uint8_t { Direct = 0, Group = 1, Broadcast = 2, Local = 3 };

constexpr size_t kChNameMax = 24;   // 全角 8 文字
constexpr size_t kFromMax = 24;
constexpr size_t kBodyMax = 255;

struct Channel {
  uint8_t ch = 0;
  ChKind kind = ChKind::Direct;
  char name[kChNameMax + 1] = {};
  uint8_t unread = 0;
};

inline void copyText(char* dst, size_t cap, const char* src) {
  // cap バイトに収まるところで切る(UTF-8 の文字の途中では切らない)
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// 見られるチャンネルの一覧。並びは、サーバーのチャンネル(届いた順)→ 全体 → 送った記録。
template <size_t MaxServer = 8>
class ChannelList {
 public:
  ChannelList() { resetDefault(); }

  void resetDefault() {
    serverCount_ = 1;
    server_[0] = make(kChDefaultDirect, ChKind::Direct, "本部");
    broadcast_ = make(kChBroadcast, ChKind::Broadcast, "全体");
    local_ = make(kChLocalSent, ChKind::Local, "送った記録");
    rev_ = 0;
    hasConfig_ = false;
  }

  // サーバーから届いた一覧に置き換える。前に届いたものより古い(rev が小さい)ものは無視する。
  // 0 と 255 は端末のものなので、入っていたら飛ばす。MaxServer を超えた分は捨てる。読んでいない数は引き継ぐ。
  bool setFromConfig(const Channel* list, size_t n, uint32_t rev) {
    if (hasConfig_ && rev <= rev_) return false;
    Channel next[MaxServer];
    size_t count = 0;
    for (size_t i = 0; i < n && count < MaxServer; i++) {
      if (list[i].ch == kChBroadcast || list[i].ch == kChLocalSent) continue;
      bool dup = false;
      for (size_t k = 0; k < count; k++) dup = dup || next[k].ch == list[i].ch;
      if (dup) continue;
      next[count] = make(list[i].ch, list[i].kind == ChKind::Group ? ChKind::Group : ChKind::Direct, list[i].name);
      const int old = indexOf(list[i].ch);
      if (old >= 0) next[count].unread = at((size_t)old).unread;
      count++;
    }
    for (size_t i = 0; i < count; i++) server_[i] = next[i];
    serverCount_ = count;
    rev_ = rev;
    hasConfig_ = true;
    return true;
  }

  size_t count() const { return serverCount_ + 2; }
  Channel& at(size_t i) { return i < serverCount_ ? server_[i] : (i == serverCount_ ? broadcast_ : local_); }
  const Channel& at(size_t i) const {
    return i < serverCount_ ? server_[i] : (i == serverCount_ ? broadcast_ : local_);
  }
  int indexOf(uint8_t ch) const {
    for (size_t i = 0; i < count(); i++)
      if (at(i).ch == ch) return (int)i;
    return -1;
  }
  uint32_t rev() const { return rev_; }

  // 話しかけられるチャンネル(サーバーが決めた 1 対 1 とグループ)
  size_t talkableCount() const { return serverCount_; }
  const Channel& talkable(size_t i) const { return server_[i]; }

  // 届いた文を、どのチャンネルに入れるか。
  //   ch が付いていて、見られるチャンネルにあれば、そこへ。
  //   付いていない(今のサーバー)か、知らない番号なら: 全員あて → 全体、個別あて → 最初の 1 対 1(無ければ全体)。
  uint8_t resolve(bool hasCh, uint8_t ch, bool toAll) const {
    if (hasCh && ch != kChLocalSent && indexOf(ch) >= 0) return ch;
    if (toAll) return kChBroadcast;
    for (size_t i = 0; i < serverCount_; i++)
      if (server_[i].kind == ChKind::Direct) return server_[i].ch;
    return kChBroadcast;
  }

 private:
  static Channel make(uint8_t ch, ChKind kind, const char* name) {
    Channel c;
    c.ch = ch;
    c.kind = kind;
    copyText(c.name, sizeof c.name, name);
    return c;
  }

  Channel server_[MaxServer];
  size_t serverCount_ = 0;
  Channel broadcast_;
  Channel local_;
  uint32_t rev_ = 0;
  bool hasConfig_ = false;
};

struct Message {
  uint8_t ch = 0;
  uint32_t mid = 0;          // サーバーが付けた番号。端末の中の記録は 0
  uint32_t ts = 0;
  bool instruction = false;  // 返事(了解 / できない)が要る指示
  bool own = false;          // 自分が話したもの(知らせを出さない)
  char from[kFromMax + 1] = {};
  char body[kBodyMax + 1] = {};
};

// 届いたメッセージを、全チャンネル合わせて N 件まで覚える。あふれたら、いちばん古いものから捨てる。
template <size_t N = 64>
class MessageStore {
 public:
  // 同じチャンネルに同じ mid(0 以外)が既にあれば、入れない(サーバーの送り直しで 2 回届くことがある)。
  bool add(const Message& m) {
    if (m.mid != 0) {
      for (size_t i = 0; i < count_; i++) {
        const Message& e = item(i);
        if (e.ch == m.ch && e.mid == m.mid) return false;
      }
    }
    if (count_ == N) {
      head_ = (head_ + 1) % N;
      count_--;
    }
    buf_[(head_ + count_) % N] = m;
    count_++;
    return true;
  }

  size_t countIn(uint8_t ch) const {
    size_t n = 0;
    for (size_t i = 0; i < count_; i++) n += item(i).ch == ch ? 1 : 0;
    return n;
  }

  // チャンネル ch の、古い方から数えて idx 番目。無ければ nullptr。
  const Message* nth(uint8_t ch, size_t idx) const {
    for (size_t i = 0; i < count_; i++) {
      const Message& e = item(i);
      if (e.ch != ch) continue;
      if (idx == 0) return &e;
      idx--;
    }
    return nullptr;
  }

  size_t size() const { return count_; }

 private:
  const Message& item(size_t i) const { return buf_[(head_ + i) % N]; }

  Message buf_[N];
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace reray
