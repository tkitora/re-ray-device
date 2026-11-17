// PC 上のユニットテスト。  make -C firmware/core test
#include "harness.h"

int g_fail = 0;
int g_checks = 0;

int main() {
  run_core_tests();
  run_gateway_tests();
  run_input_tests();
  run_ui_tests();
  printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
