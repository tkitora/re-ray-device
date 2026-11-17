#!/bin/sh
# 電波法のガードの確認(docs/COMPLIANCE.md §4.1)。
# 次の設定は「コンパイルが通らない」のが正しい。通ってしまったら、このスクリプトは失敗する。
#   使い方: sh test/guard_test.sh "<コンパイラ>" "<フラグ>"   (make guard-test から呼ぶ)
CXX="$1"
FLAGS="$2"
fail=0

reject() {  # reject <ソース> <指定> : static_assert で拒否されることを確かめる
  out=$($CXX $FLAGS $2 -fsyntax-only "$1" 2>&1)
  if [ $? -eq 0 ]; then
    echo "NG: $2 がビルドできてしまった"
    fail=1
  elif ! echo "$out" | grep -q "static assertion failed"; then
    echo "NG: $2 は static_assert 以外の理由で失敗した"
    echo "$out"
    fail=1
  else
    echo "OK: $2 は拒否された"
  fi
}

accept() {  # accept <ソース> <指定> : ビルドできることを確かめる
  if $CXX $FLAGS $2 -fsyntax-only "$1" 2>/dev/null; then
    echo "OK: $2 はビルドできる"
  else
    echo "NG: $2 がビルドできない"
    fail=1
  fi
}

# 出力と、送信の有効化
reject test/test_core.cpp "-DRERAY_MAX_TX_DBM=13"
reject test/test_core.cpp "-DRERAY_MAX_TX_DBM=10 -DRERAY_TX_DBM=11"
reject test/test_core.cpp "-DRERAY_TX_ENABLE=2"

# 周波数: 証明書の範囲の外(公式サンプルの 920.0MHz、海外向けの 868MHz)
reject test/test_input.cpp "-DRERAY_LORA_FREQ_KHZ=920000"
reject test/test_input.cpp "-DRERAY_LORA_FREQ_KHZ=868000"
# 周波数: 範囲の中でも、確認欄で決めていないチャンネル
reject test/test_input.cpp "-DRERAY_LORA_FREQ_KHZ=923400"
reject test/test_input.cpp "-DRERAY_LORA_FREQ_KHZ=920600"
# 帯域幅と SF
reject test/test_input.cpp "-DRERAY_LORA_BW_HZ=250000"
reject test/test_input.cpp "-DRERAY_LORA_SF=10"
reject test/test_input.cpp "-DRERAY_LORA_SF=6"

# 決めた設定は通る
accept test/test_input.cpp "-DRERAY_LORA_FREQ_KHZ=923200 -DRERAY_LORA_SF=9"

exit $fail
