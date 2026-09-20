#!/usr/bin/env bash
#
# mempool-lab 全量门禁：一条命令给出「这份代码是否可接受」的结论。
#
# 设计要点（来自实战教训）：
#   - 绝不用 `| tail` 之类的管道判断成败：它会吞掉真实退出码
#   - 用例数从输出里数，不硬编码
#   - 每项独立判定并记录，最后统一汇总
#
# 用法：bash scripts/ci.sh   （耗时约 2~4 分钟）

set -u
cd "$(dirname "$0")/.." || exit 1

declare -a NAMES=()
declare -a RESULTS=()
FAILED=0

record() {
  NAMES+=("$1")
  if [ "$2" -eq 0 ]; then RESULTS+=("PASS${3:+ ($3)}"); else RESULTS+=("FAIL${3:+ ($3)}"); FAILED=1; fi
}
section() { printf '\n=== %s ===\n' "$1"; }
cases() { printf '%s' "$1" | grep -c '^\[  OK  \]'; }

BIN="./build/linux/x86_64/release/test_mempool_lab"
SRCS="test/test_main.cpp test/test_basic.cpp test/test_realloc.cpp test/test_audit.cpp test/test_fuzz.cpp test/test_concurrency.cpp"

# ---------------------------------------------------------------- 1. 构建 + 用例
section "1. 构建与用例"
rm -rf build .xmake
if xmake >/tmp/lab_build.log 2>&1; then
  OUT=$("$BIN" --quiet 2>&1); RC=$?
  N=$(cases "$OUT")
  if [ $RC -eq 0 ]; then
    echo "通过（$N 用例）"
    record "构建 + 用例" 0 "$N 用例"
  else
    echo "用例失败："; printf '%s\n' "$OUT" | grep -E 'FAIL' | head -10
    record "构建 + 用例" 1
  fi
else
  echo "构建失败："; tail -20 /tmp/lab_build.log
  record "构建 + 用例" 1
fi

# ---------------------------------------------------------------- 2. xmake test 门禁
section "2. xmake test 门禁"
if xmake test >/tmp/lab_test.log 2>&1; then
  echo "通过"; record "xmake test" 0
else
  echo "失败："; tail -10 /tmp/lab_test.log; record "xmake test" 1
fi

# ---------------------------------------------------------------- 3. 零告警
section "3. 编译告警（-Wall -Wextra -Wpedantic）"
rm -rf build .xmake
xmake f --cxflags="-Wall -Wextra -Wpedantic" >/dev/null 2>&1
W=$(xmake -r 2>&1 | grep -ci warning)
echo "告警数：$W"
[ "$W" -eq 0 ] && record "零告警" 0 || record "零告警" 1 "$W 条"

# ---------------------------------------------------------------- 4. 格式
section "4. clang-format"
BAD=0
for f in include/mempool_lab.hpp include/mempool_lab/*.hpp src/*.cpp test/*.hpp test/*.cpp bench/*.cpp; do
  [ -f "$f" ] || continue
  clang-format --dry-run -Werror "$f" >/dev/null 2>&1 || { echo "未格式化：$f"; BAD=$((BAD+1)); }
done
[ "$BAD" -eq 0 ] && record "clang-format" 0 || record "clang-format" 1 "$BAD 个文件"

# ---------------------------------------------------------------- 5. ASan + UBSan
section "5. ASan + UBSan"
if clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I include -I test $SRCS src/default_pool.cpp \
     -o /tmp/lab_asan -pthread >/tmp/lab_asan_build.log 2>&1; then
  if ASAN_OPTIONS=detect_leaks=0 /tmp/lab_asan --quiet >/tmp/lab_asan.log 2>&1; then
    echo "通过"; record "ASan+UBSan" 0
  else
    echo "失败："; tail -15 /tmp/lab_asan.log; record "ASan+UBSan" 1
  fi
else
  echo "构建失败："; tail -10 /tmp/lab_asan_build.log; record "ASan+UBSan" 1
fi

# ---------------------------------------------------------------- 6. TSan
section "6. TSan（多线程）"
if clang++ -std=c++20 -O1 -g -fsanitize=thread -I include -I test $SRCS src/default_pool.cpp \
     -o /tmp/lab_tsan -pthread >/tmp/lab_tsan_build.log 2>&1; then
  if TSAN_OPTIONS=halt_on_error=1 /tmp/lab_tsan --quiet >/tmp/lab_tsan.log 2>&1; then
    echo "通过"; record "TSan" 0
  else
    echo "检测到竞态："; grep -E "WARNING|SUMMARY" /tmp/lab_tsan.log | head -10; record "TSan" 1
  fi
else
  echo "构建失败："; tail -10 /tmp/lab_tsan_build.log; record "TSan" 1
fi

# ---------------------------------------------------------------- 7. valgrind
section "7. valgrind"
rm -rf build .xmake
xmake f --cxflags="" >/dev/null 2>&1
if xmake >/dev/null 2>&1; then
  VG=$(valgrind --leak-check=full --error-exitcode=99 "$BIN" --quiet 2>&1); RC=$?
  # 注意判定条件：当「没有任何泄漏」时，valgrind 打印的是
  #   "All heap blocks were freed -- no leaks are possible"
  # 而**不会**打印 "definitely lost: 0 bytes" 那一行。
  # 因此不能只查后者——那会造成假失败（本脚本踩过这个坑）。
  ok_errors=0; printf '%s' "$VG" | grep -q "ERROR SUMMARY: 0 errors" && ok_errors=1
  ok_leaks=0
  printf '%s' "$VG" | grep -q "All heap blocks were freed -- no leaks are possible" && ok_leaks=1
  printf '%s' "$VG" | grep -q "definitely lost: 0 bytes" && ok_leaks=1
  if [ $RC -eq 0 ] && [ $ok_errors -eq 1 ] && [ $ok_leaks -eq 1 ]; then
    echo "通过"; record "valgrind" 0
  else
    echo "发现问题："; printf '%s\n' "$VG" | tail -15; record "valgrind" 1
  fi
else
  record "valgrind" 1 "构建失败"
fi

# ---------------------------------------------------------------- 汇总
section "汇总"
i=0
while [ $i -lt ${#NAMES[@]} ]; do
  printf '  %-28s %s\n' "${NAMES[$i]}" "${RESULTS[$i]}"
  i=$((i+1))
done
echo
[ "$FAILED" -eq 0 ] && echo "全部检查通过。" || echo "存在失败项。"
exit $FAILED
