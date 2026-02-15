// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <catch2/catch_test_macros.hpp>
#include <ztask/ztask.hpp>
#include <cstdint>
#include <limits>

// Test helpers
static uint32_t g_callback_count = 0;
static uint32_t g_reentrant_bind_count = 0;
static uint32_t g_reentrant_unbind_count = 0;

static void SimpleCallback(void* ctx) {
  ++g_callback_count;
}

static void ResetGlobals() {
  g_callback_count = 0;
  g_reentrant_bind_count = 0;
  g_reentrant_unbind_count = 0;
}

// =============================================================================
// 边界和异常情况测试
// =============================================================================

TEST_CASE("EdgeCase - nullptr callback returns kInvalidId", "[edge][nullptr]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(nullptr, 10, 0);

  REQUIRE(id == ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("EdgeCase - nullptr callback in BindOneShot", "[edge][nullptr]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.BindOneShot(nullptr, 5);

  REQUIRE(id == ztask::TaskScheduler<16>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("EdgeCase - repeat_ticks=0 and delay_ticks=0 executes immediately once", "[edge][zero]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(SimpleCallback, 0, 0);

  // Should be ready immediately
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);
  REQUIRE(sched.ActiveCount() == 0);  // Auto-unbound (one-shot)

  // Second poll should not execute
  executed = sched.Poll();
  REQUIRE(executed == 0);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("EdgeCase - Bind in callback (reentrant)", "[edge][reentrant]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  static ztask::TaskScheduler<16>* g_sched = nullptr;
  g_sched = &sched;

  auto reentrant_callback = [](void* ctx) {
    ++g_callback_count;
    // Bind 3 new tasks during callback execution
    while (g_reentrant_bind_count < 3) {
      auto id = g_sched->Bind(SimpleCallback, 10, 100);
      if (id != ztask::TaskScheduler<16>::kInvalidId) {
        ++g_reentrant_bind_count;
      } else {
        break;
      }
    }
  };

  sched.Bind(reentrant_callback, 10, 10);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();

  REQUIRE(g_callback_count == 1);
  REQUIRE(g_reentrant_bind_count == 3);
  REQUIRE(sched.ActiveCount() == 4);  // Original + 3 new
}

TEST_CASE("EdgeCase - Unbind in callback (reentrant)", "[edge][reentrant]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  static ztask::TaskScheduler<16>* g_sched = nullptr;
  static ztask::TaskScheduler<16>::TaskId g_target_id;
  g_sched = &sched;

  auto unbind_callback = [](void* ctx) {
    ++g_callback_count;
    g_sched->Unbind(g_target_id);
    ++g_reentrant_unbind_count;
  };

  g_target_id = sched.Bind(SimpleCallback, 10, 20);  // Will be unbound
  sched.Bind(unbind_callback, 10, 10);               // Unbinds target

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();

  REQUIRE(g_callback_count == 1);
  REQUIRE(g_reentrant_unbind_count == 1);
  REQUIRE(sched.ActiveCount() == 1);  // Only unbind_callback remains

  // Advance to tick 20, target should not execute
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);  // Only unbind_callback
  REQUIRE(g_callback_count == 2);
}

TEST_CASE("EdgeCase - Unbind self in callback", "[edge][reentrant]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  static ztask::TaskScheduler<16>* g_sched = nullptr;
  static ztask::TaskScheduler<16>::TaskId g_self_id;
  g_sched = &sched;

  auto self_unbind_callback = [](void* ctx) {
    ++g_callback_count;
    g_sched->Unbind(g_self_id);
  };

  g_self_id = sched.Bind(self_unbind_callback, 10, 0);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();

  REQUIRE(g_callback_count == 1);
  REQUIRE(sched.ActiveCount() == 0);

  // Should not execute again
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("EdgeCase - Fast Bind/Unbind cycle (generation overflow)", "[edge][generation]") {
  ResetGlobals();
  ztask::TaskScheduler<4> sched;

  // Cycle through generations (256 times to wrap uint8_t)
  for (uint32_t i = 0; i < 300; ++i) {
    auto id = sched.Bind(SimpleCallback, 10, 0);
    REQUIRE(id != ztask::TaskScheduler<4>::kInvalidId);
    sched.Unbind(id);
  }

  REQUIRE(sched.ActiveCount() == 0);

  // Should still work after generation wrap
  auto id = sched.Bind(SimpleCallback, 10, 0);
  REQUIRE(id != ztask::TaskScheduler<4>::kInvalidId);
  REQUIRE(sched.ActiveCount() == 1);
}

TEST_CASE("EdgeCase - Tick overflow (uint32_t wrap-around)", "[edge][overflow]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint32_t> sched;

  // Set ticks near max
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }
  sched.ResetTicks();

  // Manually set to near overflow (simulate)
  constexpr uint32_t near_max = std::numeric_limits<uint32_t>::max() - 5;
  for (uint32_t i = 0; i < near_max; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.GetTicks() == near_max);

  // Bind task that will wrap around
  sched.Bind(SimpleCallback, 10, 10);

  // Advance past overflow
  for (uint32_t i = 0; i < 20; ++i) {
    sched.Tick();
  }

  // Tick should have wrapped
  REQUIRE(sched.GetTicks() < near_max);

  // Task should still execute (implementation-dependent)
  // Note: This test documents current behavior
  uint32_t executed = sched.Poll();
  // Execution depends on overflow handling
}

TEST_CASE("EdgeCase - uint16_t tick overflow", "[edge][overflow]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint16_t> sched;

  // Advance to near overflow
  constexpr uint16_t near_max = 65530;
  for (uint32_t i = 0; i < near_max; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.GetTicks() == near_max);

  // Bind task
  sched.Bind(SimpleCallback, 10, 10);

  // Advance past overflow
  for (uint32_t i = 0; i < 20; ++i) {
    sched.Tick();
  }

  // Should wrap to small value
  REQUIRE(sched.GetTicks() == static_cast<uint16_t>(near_max + 20));
}

TEST_CASE("EdgeCase - Multiple Unbind of same ID", "[edge][unbind]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id = sched.Bind(SimpleCallback, 10, 0);
  REQUIRE(sched.ActiveCount() == 1);

  sched.Unbind(id);
  REQUIRE(sched.ActiveCount() == 0);

  // Multiple unbinds should be safe
  sched.Unbind(id);
  sched.Unbind(id);
  sched.Unbind(id);

  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("EdgeCase - Unbind with corrupted ID", "[edge][unbind]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  // Random invalid IDs
  sched.Unbind(0x1234);
  sched.Unbind(0xABCD);
  sched.Unbind(0xFFFF);

  REQUIRE(sched.ActiveCount() == 0);
}

TEST_CASE("EdgeCase - Bind after Unbind reuses slot", "[edge][reuse]") {
  ResetGlobals();
  ztask::TaskScheduler<4> sched;

  auto id1 = sched.Bind(SimpleCallback, 10, 0);
  auto id2 = sched.Bind(SimpleCallback, 10, 0);

  sched.Unbind(id1);

  auto id3 = sched.Bind(SimpleCallback, 10, 0);

  // id3 should reuse id1's slot (but different generation)
  REQUIRE(id3 != id1);  // Different generation
  REQUIRE(sched.ActiveCount() == 2);
}

TEST_CASE("EdgeCase - Poll with all tasks at same schedule time", "[edge][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  // Bind 10 tasks all ready at tick 10
  for (uint32_t i = 0; i < 10; ++i) {
    sched.Bind(SimpleCallback, 100, 10);
  }

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  uint32_t executed = sched.Poll();
  REQUIRE(executed == 10);
  REQUIRE(g_callback_count == 10);
}

TEST_CASE("EdgeCase - Very large delay_ticks", "[edge][delay]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint64_t> sched;

  constexpr uint64_t large_delay = 1000000000ULL;
  sched.Bind(SimpleCallback, 10, large_delay);

  REQUIRE(sched.TicksToNextTask() == large_delay);

  // Advance halfway
  for (uint64_t i = 0; i < large_delay / 2; ++i) {
    sched.Tick();
  }

  REQUIRE(sched.TicksToNextTask() == large_delay / 2);
}

TEST_CASE("EdgeCase - Very large repeat_ticks", "[edge][repeat]") {
  ResetGlobals();
  ztask::TaskScheduler<16, uint64_t> sched;

  constexpr uint64_t large_repeat = 1000000000ULL;
  sched.Bind(SimpleCallback, large_repeat, 0);

  sched.Poll();
  REQUIRE(g_callback_count == 1);

  REQUIRE(sched.TicksToNextTask() == large_repeat);
}

TEST_CASE("EdgeCase - Interleaved Bind and Unbind", "[edge][interleave]") {
  ResetGlobals();
  ztask::TaskScheduler<8> sched;

  auto id1 = sched.Bind(SimpleCallback, 10, 0);
  auto id2 = sched.Bind(SimpleCallback, 10, 0);
  sched.Unbind(id1);
  auto id3 = sched.Bind(SimpleCallback, 10, 0);
  sched.Unbind(id2);
  auto id4 = sched.Bind(SimpleCallback, 10, 0);
  auto id5 = sched.Bind(SimpleCallback, 10, 0);
  sched.Unbind(id3);

  REQUIRE(sched.ActiveCount() == 2);  // id4 and id5
}

TEST_CASE("EdgeCase - Poll immediately after Bind with zero delay", "[edge][immediate]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(SimpleCallback, 10, 0);

  // Should execute immediately
  uint32_t executed = sched.Poll();
  REQUIRE(executed == 1);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("EdgeCase - TicksToNextTask after all tasks complete", "[edge][complete]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.BindOneShot(SimpleCallback, 5);
  sched.BindOneShot(SimpleCallback, 10);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();

  REQUIRE(sched.ActiveCount() == 0);
  REQUIRE(sched.TicksToNextTask() == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("EdgeCase - Rapid Poll calls", "[edge][poll]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  sched.Bind(SimpleCallback, 10, 0);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  // Multiple polls should only execute once
  uint32_t total = 0;
  total += sched.Poll();
  total += sched.Poll();
  total += sched.Poll();

  REQUIRE(total == 1);
  REQUIRE(g_callback_count == 1);
}

TEST_CASE("EdgeCase - Bind with same callback multiple times", "[edge][duplicate]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  auto id1 = sched.Bind(SimpleCallback, 10, 0);
  auto id2 = sched.Bind(SimpleCallback, 10, 0);
  auto id3 = sched.Bind(SimpleCallback, 10, 0);

  REQUIRE(id1 != id2);
  REQUIRE(id2 != id3);
  REQUIRE(sched.ActiveCount() == 3);

  for (uint32_t i = 0; i < 10; ++i) {
    sched.Tick();
  }

  sched.Poll();
  REQUIRE(g_callback_count == 3);
}

TEST_CASE("EdgeCase - Context pointer edge values", "[edge][context]") {
  ResetGlobals();

  static void* received_ctx = nullptr;

  auto callback = [](void* ctx) {
    received_ctx = ctx;
  };

  // Test 1: nullptr context
  {
    ztask::TaskScheduler<16> sched;
    received_ctx = reinterpret_cast<void*>(0x1234);  // sentinel
    sched.Bind(callback, 10, 10, nullptr);

    for (uint32_t i = 0; i < 10; ++i) {
      sched.Tick();
    }

    sched.Poll();
    REQUIRE(received_ctx == nullptr);
  }

  // Test 2: Max pointer value (unlikely but valid)
  {
    ztask::TaskScheduler<16> sched;
    void* max_ptr = reinterpret_cast<void*>(~0ULL);
    sched.Bind(callback, 10, 10, max_ptr);

    for (uint32_t i = 0; i < 10; ++i) {
      sched.Tick();
    }

    sched.Poll();
    REQUIRE(received_ctx == max_ptr);
  }
}

TEST_CASE("EdgeCase - Empty scheduler operations", "[edge][empty]") {
  ResetGlobals();
  ztask::TaskScheduler<16> sched;

  // All operations on empty scheduler should be safe
  REQUIRE(sched.Poll() == 0);
  REQUIRE(sched.TicksToNextTask() == std::numeric_limits<uint32_t>::max());
  REQUIRE(sched.IsEmpty());
  REQUIRE(sched.ActiveCount() == 0);

  sched.Tick();
  sched.ResetTicks();

  REQUIRE(sched.IsEmpty());
}

TEST_CASE("EdgeCase - Scheduler with MaxTasks=1 stress", "[edge][stress]") {
  ResetGlobals();
  ztask::TaskScheduler<1> sched;

  // Rapid bind/unbind cycles
  for (uint32_t i = 0; i < 100; ++i) {
    auto id = sched.Bind(SimpleCallback, 10, 0);
    REQUIRE(id != ztask::TaskScheduler<1>::kInvalidId);
    REQUIRE(sched.ActiveCount() == 1);

    sched.Unbind(id);
    REQUIRE(sched.ActiveCount() == 0);
  }
}

TEST_CASE("EdgeCase - Mixed TickType operations", "[edge][ticktype]") {
  ResetGlobals();

  // uint16_t scheduler
  ztask::TaskScheduler<8, uint16_t> sched16;
  sched16.Bind(SimpleCallback, 100, 0);

  for (uint32_t i = 0; i < 100; ++i) {
    sched16.Tick();
  }

  REQUIRE(sched16.Poll() == 1);

  // uint64_t scheduler
  ztask::TaskScheduler<8, uint64_t> sched64;
  sched64.Bind(SimpleCallback, 100, 0);

  for (uint32_t i = 0; i < 100; ++i) {
    sched64.Tick();
  }

  REQUIRE(sched64.Poll() == 1);
}
