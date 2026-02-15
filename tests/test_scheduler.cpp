// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <catch2/catch_test_macros.hpp>
#include <ztask/ztask.hpp>
#include <cstdint>
#include <limits>

// Test helpers
static uint32_t g_callback_count = 0;
static void* g_last_ctx = nullptr;

static void TestCallback(void* ctx) {
  ++g_callback_count;
  g_last_ctx = ctx;
}

static void ResetGlobals() {
  g_callback_count = 0;
  g_last_ctx = nullptr;
}

// =============================================================================
// 基础功能测试 (~15 cases)
// =============================================================================

TEST_CASE("TaskScheduler - Default construction", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  REQUIRE(sched.IsEmpty());
  REQUIRE(sched.ActiveCount() == 0);
  REQUIRE(sched.GetTicks() == 0);
  REQUIRE(sched.Capacity() == 16);
}

TEST_CASE("TaskScheduler - Capacity is compile-time constant", "[scheduler][basic]") {
  ztask::TaskScheduler<8> sched8;
  ztask::TaskScheduler<32> sched32;
  ztask::TaskScheduler<64> sched64;

  REQUIRE(sched8.Capacity() == 8);
  REQUIRE(sched32.Capacity() == 32);
  REQUIRE(sched64.Capacity() == 64);
}

TEST_CASE("TaskScheduler - Tick increments counter", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  REQUIRE(sched.GetTicks() == 0);

  sched.Tick();
  REQUIRE(sched.GetTicks() == 1);

  sched.Tick();
  REQUIRE(sched.GetTicks() == 2);

  for (uint32_t i = 0; i < 100; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.GetTicks() == 102);
}

TEST_CASE("TaskScheduler - ResetTicks clears counter", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  for (uint32_t i = 0; i < 50; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.GetTicks() == 50);

  sched.ResetTicks();
  REQUIRE(sched.GetTicks() == 0);
}

TEST_CASE("TaskScheduler - Bind periodic task returns valid TaskId", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(TestCallback, 10, 0);

  REQUIRE(id != ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 1);
  REQUIRE_FALSE(sched.IsEmpty());
}

TEST_CASE("TaskScheduler - Bind one-shot task with BindOneShot", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.BindOneShot(TestCallback, 5);

  REQUIRE(id != ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 1);
}

TEST_CASE("TaskScheduler - Unbind removes task", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE(sched.ActiveCount() == 1);

  sched.Unbind(id);
  REQUIRE(sched.ActiveCount() == 0);
  REQUIRE(sched.IsEmpty());
}

TEST_CASE("TaskScheduler - Unbind invalid ID is safe", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Unbind(ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 0);

  sched.Unbind(0x1234);
  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("TaskScheduler - Double Unbind is safe", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE(sched.ActiveCount() == 1);

  sched.Unbind(id);
  REQUIRE(sched.ActiveCount() == 0);

  // Second unbind should be no-op (generation mismatch)
  sched.Unbind(id);
  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("TaskScheduler - Bind nullptr callback returns kInvalidId", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(nullptr, 10, 0);

  REQUIRE(id == ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("TaskScheduler - Multiple tasks increase ActiveCount", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id1 = sched.Bind(TestCallback, 10, 0);
  auto id2 = sched.Bind(TestCallback, 20, 0);
  auto id3 = sched.Bind(TestCallback, 30, 0);

  REQUIRE(sched.ActiveCount() == 3);
  REQUIRE(id1 != id2);
  REQUIRE(id2 != id3);
  REQUIRE(id1 != id3);
}

TEST_CASE("TaskScheduler - Unbind middle task maintains list integrity", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id1 = sched.Bind(TestCallback, 10, 10);
  auto id2 = sched.Bind(TestCallback, 20, 20);
  auto id3 = sched.Bind(TestCallback, 30, 30);

  sched.Unbind(id2);

  REQUIRE(sched.ActiveCount() == 2);

  // Verify remaining tasks still work: id1 ready at tick 10
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);  // id1 should execute (id3 not ready until tick 30)
}

TEST_CASE("TaskScheduler - IsEmpty reflects active state", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  REQUIRE(sched.IsEmpty());

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE_FALSE(sched.IsEmpty());

  sched.Unbind(id);
  REQUIRE(sched.IsEmpty());
}

TEST_CASE("TaskScheduler - Context pointer is preserved", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  int my_data = 42;
  auto id = sched.Bind(TestCallback, 10, 0, &my_data);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(g_last_ctx == &my_data);
}

TEST_CASE("TaskScheduler - Null context is allowed", "[scheduler][basic]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(TestCallback, 10, 0, nullptr);
  REQUIRE(id != ztask::TaskScheduler<16>::kInvalidId);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(g_last_ctx == nullptr);
}

// =============================================================================
// 调度功能测试 (~15 cases)
// =============================================================================

TEST_CASE("TaskScheduler - Poll with no tasks returns 0", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 0);
  REQUIRE(g_callback_count == 0);
}

TEST_CASE("TaskScheduler - Poll executes ready periodic task", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 10);  // delay=10, first ready at tick 10

  // Not ready yet (tick 0, next_schedule=10)
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 0);
  REQUIRE(g_callback_count == 0);

  // Advance to ready time
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("TaskScheduler - Poll executes one-shot task and auto-unbinds", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.BindOneShot(TestCallback, 5);
  REQUIRE(sched.ActiveCount() == 1);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);
  REQUIRE(sched.ActiveCount() == 0);  // Auto-unbound

  // Second poll should not execute
  executed = sched.Poll();
  REQUIRE(executed == 0);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("TaskScheduler - Poll executes multiple ready tasks", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 100, 5);   // Ready at tick 5
  sched.Bind(TestCallback, 100, 10);  // Ready at tick 10
  sched.Bind(TestCallback, 100, 10);  // Ready at tick 10

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 3);
  REQUIRE(g_callback_count == 3);
}

TEST_CASE("TaskScheduler - Poll respects task order by schedule time", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  static uint32_t execution_order[3] = {0, 0, 0};
  static uint32_t order_idx = 0;
  order_idx = 0;

  auto callback1 = [](void* ctx) {
    execution_order[order_idx++] = 1;
  };
  auto callback2 = [](void* ctx) {
    execution_order[order_idx++] = 2;
  };
  auto callback3 = [](void* ctx) {
    execution_order[order_idx++] = 3;
  };

  sched.Bind(callback3, 100, 30);  // Ready at tick 30
  sched.Bind(callback1, 100, 10);  // Ready at tick 10
  sched.Bind(callback2, 100, 20);  // Ready at tick 20

  for (uint32_t i = 0; i < 30; ++i) {
    sched.Tick();
  }

  sched.Poll();

  REQUIRE(execution_order[0] == 1);
  REQUIRE(execution_order[1] == 2);
  REQUIRE(execution_order[2] == 3);
}

TEST_CASE("TaskScheduler - Poll with delay_ticks defers first execution", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 5);  // Delay 5 ticks

  for (uint32_t i = 0; i < 4; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.Poll() == 0);

  sched.Tick();  // Now at tick 5
  REQUIRE(sched.Poll() == 1);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("TaskScheduler - Periodic task reschedules correctly", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 0);

  // First execution at tick 10
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.Poll() == 1);
  REQUIRE(g_callback_count == 1);

  // Second execution at tick 20
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.Poll() == 1);
  REQUIRE(g_callback_count == 2);

  // Third execution at tick 30
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.Poll() == 1);
  REQUIRE(g_callback_count == 3);
}

TEST_CASE("TaskScheduler - Poll reschedules from current tick (no catch-up)", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 10);  // First ready at tick 10

  // Advance 35 ticks without polling
  for (uint32_t i = 0; i < 35; ++i) {
    sched.Tick();
  }

  // Poll once: executes overdue task, reschedules to current_ticks + 10 = 45
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);

  // Next poll: nothing ready (next at tick 45)
  executed = sched.Poll();
  REQUIRE(executed == 0);
}

TEST_CASE("TaskScheduler - Callback receives correct context", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  int data1 = 100;
  int data2 = 200;

  static int* received_ctx = nullptr;

  auto callback = [](void* ctx) {
    received_ctx = static_cast<int*>(ctx);
  };

  sched.Bind(callback, 10, 0, &data1);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(received_ctx == &data1);
  REQUIRE(*received_ctx == 100);
}

TEST_CASE("TaskScheduler - Multiple callbacks with different contexts", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  static int sum = 0;
  sum = 0;

  auto callback = [](void* ctx) {
    sum += *static_cast<int*>(ctx);
  };

  int val1 = 10;
  int val2 = 20;
  int val3 = 30;

  sched.Bind(callback, 100, 5, &val1);
  sched.Bind(callback, 100, 5, &val2);
  sched.Bind(callback, 100, 5, &val3);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(sum == 60);
}

TEST_CASE("TaskScheduler - Poll increments callback counter correctly", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 5, 0);

  for (uint32_t i = 0; i < 20; ++i) {
    sched.Tick();
    sched.Poll();
  }

  // Should execute at ticks 5, 10, 15, 20
  REQUIRE(g_callback_count == 4);
}

TEST_CASE("TaskScheduler - Zero delay executes immediately", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 0);  // No delay

  // Should be ready at tick 0
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("TaskScheduler - repeat_ticks=0 creates one-shot task", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 0, 5);  // One-shot with delay
  REQUIRE(sched.ActiveCount() == 1);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(g_callback_count == 1);
  REQUIRE(sched.ActiveCount() == 0);  // Auto-unbound
}

TEST_CASE("TaskScheduler - Mixed periodic and one-shot tasks", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 0);      // Periodic
  sched.BindOneShot(TestCallback, 5);   // One-shot

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();

  // One-shot at tick 5, periodic at tick 10
  REQUIRE(g_callback_count == 2);
  REQUIRE(sched.ActiveCount() == 1);  // Only periodic remains
}

TEST_CASE("TaskScheduler - Poll returns correct execution count", "[scheduler][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 100, 10);
  sched.Bind(TestCallback, 100, 10);
  sched.Bind(TestCallback, 100, 10);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 3);
}

// =============================================================================
// TicksToNextTask 测试 (~8 cases)
// =============================================================================

TEST_CASE("TaskScheduler - TicksToNextTask with no tasks returns max", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint32_t> sched;

  auto ticks = sched.TicksToNextTask();
  REQUIRE(ticks == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("TaskScheduler - TicksToNextTask returns correct remaining ticks", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 20);  // Ready at tick 20

  REQUIRE(sched.TicksToNextTask() == 20);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.TicksToNextTask() == 15);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.TicksToNextTask() == 5);
}

TEST_CASE("TaskScheduler - TicksToNextTask returns 0 for ready task", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 5);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.TicksToNextTask() == 0);

  // Still 0 after more ticks
  sched.Tick();
  REQUIRE(sched.TicksToNextTask() == 0);
}

TEST_CASE("TaskScheduler - TicksToNextTask returns minimum of multiple tasks", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 100, 30);  // Ready at tick 30
  sched.Bind(TestCallback, 100, 10);  // Ready at tick 10
  sched.Bind(TestCallback, 100, 20);  // Ready at tick 20

  REQUIRE(sched.TicksToNextTask() == 10);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }
  REQUIRE(sched.TicksToNextTask() == 5);
}

TEST_CASE("TaskScheduler - TicksToNextTask updates after Poll", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 10, 0);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.TicksToNextTask() == 0);

  sched.Poll();

  // Next execution at tick 20
  REQUIRE(sched.TicksToNextTask() == 10);
}

TEST_CASE("TaskScheduler - TicksToNextTask after one-shot completes", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.BindOneShot(TestCallback, 5);

  REQUIRE(sched.TicksToNextTask() == 5);

  for (uint32_t i = 0; i < 5; ++i) {
    sched.Tick();
  }

  sched.Poll();

  // No more tasks
  REQUIRE(sched.TicksToNextTask() == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("TaskScheduler - TicksToNextTask with mixed task types", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(TestCallback, 20, 15);     // Periodic, ready at 15
  sched.BindOneShot(TestCallback, 10);  // One-shot, ready at 10

  REQUIRE(sched.TicksToNextTask() == 10);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();  // Execute one-shot

  // Only periodic remains, ready at tick 15
  REQUIRE(sched.TicksToNextTask() == 5);
}

TEST_CASE("TaskScheduler - TicksToNextTask after Unbind", "[scheduler][ticks]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id1 = sched.Bind(TestCallback, 100, 10);
  auto id2 = sched.Bind(TestCallback, 100, 20);

  REQUIRE(sched.TicksToNextTask() == 10);

  sched.Unbind(id1);

  REQUIRE(sched.TicksToNextTask() == 20);

  sched.Unbind(id2);

  REQUIRE(sched.TicksToNextTask() == std::numeric_limits<uint32_t>::max());
}

// =============================================================================
// 容量边界测试 (~5 cases)
// =============================================================================

TEST_CASE("TaskScheduler - Fill all slots", "[scheduler][capacity]") {
  ResetGlobals();
  ztask::TaskScheduler<8> sched;

  for (uint32_t i = 0; i < 8; ++i) {
    auto id = sched.Bind(TestCallback, 10, 0);
    REQUIRE(id != ztask::TaskScheduler<8>::kInvalidId);
  }

  REQUIRE(sched.ActiveCount() == 8);
}

TEST_CASE("TaskScheduler - Bind when full returns kInvalidId", "[scheduler][capacity]") {
  ResetGlobals();
  ztask::TaskScheduler<4> sched;

  for (uint32_t i = 0; i < 4; ++i) {
    sched.Bind(TestCallback, 10, 0);
  }

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id == ztask::TaskScheduler<4>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 4);
}

TEST_CASE("TaskScheduler - Unbind allows re-bind", "[scheduler][capacity]") {
  ResetGlobals();
  ztask::TaskScheduler<4> sched;

  auto id1 = sched.Bind(TestCallback, 10, 0);
  auto id2 = sched.Bind(TestCallback, 10, 0);
  auto id3 = sched.Bind(TestCallback, 10, 0);
  auto id4 = sched.Bind(TestCallback, 10, 0);

  REQUIRE(sched.ActiveCount() == 4);

  // Full, cannot bind
  auto id5 = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id5 == ztask::TaskScheduler<4>::kInvalidId);

  // Unbind one
  sched.Unbind(id2);
  REQUIRE(sched.ActiveCount() == 3);

  // Now can bind again
  auto id6 = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id6 != ztask::TaskScheduler<4>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 4);
}

TEST_CASE("TaskScheduler - MaxTasks=1 edge case", "[scheduler][capacity]") {
  ResetGlobals();
  ztask::TaskScheduler<1> sched;

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id != ztask::TaskScheduler<1>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 1);

  auto id2 = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id2 == ztask::TaskScheduler<1>::kInvalidId);

  sched.Unbind(id);
  REQUIRE(sched.ActiveCount() == 0);

  auto id3 = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id3 != ztask::TaskScheduler<1>::kInvalidId);
}

TEST_CASE("TaskScheduler - MaxTasks=64 large capacity", "[scheduler][capacity]") {
  ResetGlobals();
  ztask::TaskScheduler<64> sched;

  for (uint32_t i = 0; i < 64; ++i) {
    auto id = sched.Bind(TestCallback, 10, 0);
    REQUIRE(id != ztask::TaskScheduler<64>::kInvalidId);
  }

  REQUIRE(sched.ActiveCount() == 64);

  auto id = sched.Bind(TestCallback, 10, 0);
  REQUIRE(id == ztask::TaskScheduler<64>::kInvalidId);
}

// =============================================================================
// TickType 模板参数测试 (~3 cases)
// =============================================================================

TEST_CASE("TaskScheduler - uint32_t TickType default", "[scheduler][ticktype]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint32_t> sched;

  sched.Bind(TestCallback, 10, 0);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.GetTicks() == 10);
  REQUIRE(sched.Poll() == 1);
}

TEST_CASE("TaskScheduler - uint64_t TickType large range", "[scheduler][ticktype]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint64_t> sched;

  sched.Bind(TestCallback, 1000, 0);

  for (uint64_t i = 0; i < 1000; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.GetTicks() == 1000);
  REQUIRE(sched.Poll() == 1);
}

TEST_CASE("TaskScheduler - uint16_t TickType small range", "[scheduler][ticktype]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint16_t> sched;

  sched.Bind(TestCallback, 100, 0);

  for (uint16_t i = 0; i < 100; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.GetTicks() == 100);
  REQUIRE(sched.Poll() == 1);

  // Test wrap-around
  for (uint32_t i = 0; i < 65536; ++i) {
    sched.Tick();
  }

  // Should wrap to 100 (65536 + 100 = 100 in uint16_t)
  REQUIRE(sched.GetTicks() == static_cast<uint16_t>(65636));
}
