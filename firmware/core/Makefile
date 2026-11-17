# PC 上のユニットテスト(PlatformIO 不要)。  make test
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Iinclude
BUILD = build
SRCS = $(wildcard test/*.cpp)
HDRS = $(wildcard include/reray/*.h) $(wildcard test/*.h)

test: $(BUILD)/test_main
	./$(BUILD)/test_main

$(BUILD)/test_main: $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@

# 出口が MQTT に出す JSON が、JSON として正しいかを Python で確認する(test の後に実行)
json-check: test
	python3 test/check_json.py $(BUILD)/sample_json.txt

# 電波法のガードの確認: 上限を超える出力や、決めていない周波数では「コンパイルが通らない」ことを確かめる
guard-test:
	@sh test/guard_test.sh "$(CXX)" "$(CXXFLAGS)"

clean:
	rm -rf ./build

.PHONY: test json-check guard-test clean
