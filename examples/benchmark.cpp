// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <ztask/ztask.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>

// =============================================================================
// Embedded C version of ztask (simplified for benchmark)
// =============================================================================

#define ZTASK_MAX_TASKS 16

typedef void (*ztask_fn_t)(void* ctx);
typedef uint16_t ztask_id_t;
typedef uint32_t ztask_tick_t;

#define ZTASK_INVALID_ID 0xFFFF
#define ZTASK_NO_TASK 0xFFFFFFFF

typedef struct {
  ztask_fn_t fn;
  void* ctx;
  ztask_tick_t repeat_ticks;
  ztask_tick_t next_schedule;
  uint32_t next_index;
  uint8_t generation;
  bool active;
} ztask_slot_t;

typedef struct {
  ztask_slot_t slots[ZTASK_MAX_TASKS];
  ztask_tick_t current_ticks;
  uint32_t active_count;
  uint32_t head_index;
} ztask_t;

static void ztask_init(ztask_t* z) {
  z->current_ticks = 0;
  z->active_count = 0;
  z->head_index = ZTASK_NO_TASK;
  for (uint32_t i = 0; i < ZTASK_MAX_TASKS; ++i) {
    z->slots[i].active = false;
    z->slots[i].generation = 0;
    z->slots[i].next_index = ZTASK_NO_TASK;
  }
}

static void ztask_tick(ztask_t* z) {
  ++z->current_ticks;
}

static ztask_tick_t ztask_get_ticks(const ztask_t* z) {
  return z->current_ticks;
}

static ztask_id_t ztask_make_id(uint32_t slot_idx, uint8_t generation) {
  return (ztask_id_t)((generation << 8) | (slot_idx & 0xFF));
}

static uint32_t ztask_get_slot_index(ztask_id_t id) {
  return id & 0xFF;
}

static uint8_t ztask_get_generation(ztask_id_t id) {
  return (uint8_t)(id >> 8);
}

static void ztask_insert_sorted(ztask_t* z, uint32_t slot_idx) {
  ztask_slot_t* slot = &z->slots[slot_idx];

  if (z->head_index == ZTASK_NO_TASK) {
    z->head_index = slot_idx;
    slot->next_index = ZTASK_NO_TASK;
    return;
  }

  if (slot->next_schedule < z->slots[z->head_index].next_schedule) {
    slot->next_index = z->head_index;
    z->head_index = slot_idx;
    return;
  }

  uint32_t prev_idx = z->head_index;
  uint32_t curr_idx = z->slots[prev_idx].next_index;

  while (curr_idx != ZTASK_NO_TASK) {
    if (slot->next_schedule < z->slots[curr_idx].next_schedule) {
      break;
    }
    prev_idx = curr_idx;
    curr_idx = z->slots[curr_idx].next_index;
  }

  slot->next_index = curr_idx;
  z->slots[prev_idx].next_index = slot_idx;
}

static void ztask_remove_from_list(ztask_t* z, uint32_t slot_idx) {
  if (z->head_index == ZTASK_NO_TASK) {
    return;
  }

  if (z->head_index == slot_idx) {
    z->head_index = z->slots[slot_idx].next_index;
    return;
  }

  uint32_t prev_idx = z->head_index;
  uint32_t curr_idx = z->slots[prev_idx].next_index;

  while (curr_idx != ZTASK_NO_TASK) {
    if (curr_idx == slot_idx) {
      z->slots[prev_idx].next_index = z->slots[curr_idx].next_index;
      return;
    }
    prev_idx = curr_idx;
    curr_idx = z->slots[curr_idx].next_index;
  }
}

static ztask_id_t ztask_bind(ztask_t* z, ztask_fn_t fn, ztask_tick_t repeat_ticks,
                              ztask_tick_t delay_ticks, void* ctx) {
  if (fn == nullptr || z->active_count >= ZTASK_MAX_TASKS) {
    return ZTASK_INVALID_ID;
  }

  uint32_t slot_idx = ZTASK_NO_TASK;
  for (uint32_t i = 0; i < ZTASK_MAX_TASKS; ++i) {
    if (!z->slots[i].active) {
      slot_idx = i;
      break;
    }
  }

  if (slot_idx == ZTASK_NO_TASK) {
    return ZTASK_INVALID_ID;
  }

  ztask_slot_t* slot = &z->slots[slot_idx];
  slot->fn = fn;
  slot->ctx = ctx;
  slot->repeat_ticks = repeat_ticks;
  slot->next_schedule = z->current_ticks + delay_ticks;
  slot->active = true;
  slot->next_index = ZTASK_NO_TASK;

  ztask_insert_sorted(z, slot_idx);

  ++z->active_count;

  return ztask_make_id(slot_idx, slot->generation);
}

static void ztask_unbind(ztask_t* z, ztask_id_t id) {
  if (id == ZTASK_INVALID_ID) {
    return;
  }

  uint32_t slot_idx = ztask_get_slot_index(id);
  uint8_t generation = ztask_get_generation(id);

  if (slot_idx >= ZTASK_MAX_TASKS) {
    return;
  }

  ztask_slot_t* slot = &z->slots[slot_idx];

  if (!slot->active || slot->generation != generation) {
    return;
  }

  ztask_remove_from_list(z, slot_idx);

  slot->active = false;
  slot->generation = (uint8_t)((slot->generation + 1) & 0xFF);

  --z->active_count;
}

static uint32_t ztask_poll(ztask_t* z) {
  uint32_t executed = 0;

  while (z->head_index != ZTASK_NO_TASK) {
    ztask_slot_t* head = &z->slots[z->head_index];

    if (head->next_schedule > z->current_ticks) {
      break;
    }

    uint32_t exec_idx = z->head_index;
    z->head_index = head->next_index;

    ztask_slot_t* exec_slot = &z->slots[exec_idx];

    if (exec_slot->fn != nullptr) {
      exec_slot->fn(exec_slot->ctx);
    }

    ++executed;

    if (exec_slot->active) {
      if (exec_slot->repeat_ticks > 0) {
        exec_slot->next_schedule = z->current_ticks + exec_slot->repeat_ticks;
        exec_slot->next_index = ZTASK_NO_TASK;
        ztask_insert_sorted(z, exec_idx);
      } else {
        exec_slot->active = false;
        exec_slot->generation = (uint8_t)((exec_slot->generation + 1) & 0xFF);
        --z->active_count;
      }
    }
  }

  return executed;
}

static ztask_tick_t ztask_ticks_to_next(const ztask_t* z) {
  if (z->head_index == ZTASK_NO_TASK) {
    return (ztask_tick_t)(-1);
  }

  const ztask_slot_t* head = &z->slots[z->head_index];

  if (head->next_schedule <= z->current_ticks) {
    return 0;
  }

  return head->next_schedule - z->current_ticks;
}

// =============================================================================
// Benchmark helpers
// =============================================================================

static void dummy_callback(void* ctx) {
  // No-op
  (void)ctx;
}

template <typename Func>
static double benchmark_us(Func&& func, uint32_t iterations) {
  auto start = std::chrono::high_resolution_clock::now();
  func(iterations);
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  return static_cast<double>(duration.count());
}

// =============================================================================
// Benchmark tests
// =============================================================================

static void bench_c_bind(uint32_t iterations) {
  ztask_t z;
  ztask_init(&z);

  for (uint32_t i = 0; i < iterations; ++i) {
    ztask_bind(&z, dummy_callback, 10, 0, nullptr);
    if (z.active_count >= ZTASK_MAX_TASKS) {
      // Reset when full
      ztask_init(&z);
    }
  }
}

static void bench_cpp_bind(uint32_t iterations) {
  ztask::TaskScheduler<16> sched;

  for (uint32_t i = 0; i < iterations; ++i) {
    sched.Bind(dummy_callback, 10, 0, nullptr);
    if (sched.ActiveCount() >= 16) {
      // Reset when full
      sched = ztask::TaskScheduler<16>();
    }
  }
}

static void bench_c_poll_with_tasks(uint32_t iterations) {
  ztask_t z;
  ztask_init(&z);

  // Bind tasks
  for (uint32_t i = 0; i < ZTASK_MAX_TASKS; ++i) {
    ztask_bind(&z, dummy_callback, 10, 0, nullptr);
  }

  for (uint32_t i = 0; i < iterations; ++i) {
    ztask_tick(&z);
    ztask_poll(&z);
  }
}

static void bench_cpp_poll_with_tasks(uint32_t iterations) {
  ztask::TaskScheduler<16> sched;

  // Bind tasks
  for (uint32_t i = 0; i < 16; ++i) {
    sched.Bind(dummy_callback, 10, 0, nullptr);
  }

  for (uint32_t i = 0; i < iterations; ++i) {
    sched.Tick();
    sched.Poll();
  }
}

static void bench_c_poll_idle(uint32_t iterations) {
  ztask_t z;
  ztask_init(&z);

  // Bind tasks with long delay
  for (uint32_t i = 0; i < ZTASK_MAX_TASKS; ++i) {
    ztask_bind(&z, dummy_callback, 10, 1000, nullptr);
  }

  for (uint32_t i = 0; i < iterations; ++i) {
    ztask_poll(&z);
  }
}

static void bench_cpp_poll_idle(uint32_t iterations) {
  ztask::TaskScheduler<16> sched;

  // Bind tasks with long delay
  for (uint32_t i = 0; i < 16; ++i) {
    sched.Bind(dummy_callback, 10, 1000, nullptr);
  }

  for (uint32_t i = 0; i < iterations; ++i) {
    sched.Poll();
  }
}

static void bench_c_ticks_to_next(uint32_t iterations) {
  ztask_t z;
  ztask_init(&z);

  ztask_bind(&z, dummy_callback, 10, 100, nullptr);

  for (uint32_t i = 0; i < iterations; ++i) {
    volatile ztask_tick_t ticks = ztask_ticks_to_next(&z);
    (void)ticks;
  }
}

static void bench_cpp_ticks_to_next(uint32_t iterations) {
  ztask::TaskScheduler<16> sched;

  sched.Bind(dummy_callback, 10, 100, nullptr);

  for (uint32_t i = 0; i < iterations; ++i) {
    volatile uint32_t ticks = sched.TicksToNextTask();
    (void)ticks;
  }
}

static void bench_c_bind_unbind_cycle(uint32_t iterations) {
  ztask_t z;
  ztask_init(&z);

  for (uint32_t i = 0; i < iterations; ++i) {
    ztask_id_t id = ztask_bind(&z, dummy_callback, 10, 0, nullptr);
    ztask_unbind(&z, id);
  }
}

static void bench_cpp_bind_unbind_cycle(uint32_t iterations) {
  ztask::TaskScheduler<16> sched;

  for (uint32_t i = 0; i < iterations; ++i) {
    auto id = sched.Bind(dummy_callback, 10, 0, nullptr);
    sched.Unbind(id);
  }
}

// =============================================================================
// Main benchmark
// =============================================================================

int main() {
  printf("=== ztask-cpp Benchmark ===\n");
  printf("Platform: ");
#if defined(__linux__)
  printf("Linux");
#elif defined(__APPLE__)
  printf("macOS");
#elif defined(_WIN32)
  printf("Windows");
#else
  printf("Unknown");
#endif

#if defined(__x86_64__) || defined(_M_X64)
  printf(" x86_64\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
  printf(" ARM64\n");
#elif defined(__arm__) || defined(_M_ARM)
  printf(" ARM32\n");
#else
  printf(" Unknown arch\n");
#endif

  printf("Compiler: ");
#if defined(__clang__)
  printf("Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
  printf("GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  printf("MSVC %d\n", _MSC_VER);
#else
  printf("Unknown\n");
#endif

  printf("C++ Standard: C++%ld\n", __cplusplus / 100 % 100);
  printf("\n");

  constexpr uint32_t iterations = 1000000;

  // Bind performance
  printf("--- Bind Performance ---\n");
  double c_bind_us = benchmark_us(bench_c_bind, iterations);
  double cpp_bind_us = benchmark_us(bench_cpp_bind, iterations);
  printf("  C   ztask: %u binds in %.0f us (%.1f ns/op)\n",
         iterations, c_bind_us, c_bind_us * 1000.0 / iterations);
  printf("  C++ ztask: %u binds in %.0f us (%.1f ns/op)\n",
         iterations, cpp_bind_us, cpp_bind_us * 1000.0 / iterations);
  printf("  Ratio: %.2fx\n", cpp_bind_us / c_bind_us);
  printf("\n");

  // Poll with tasks
  printf("--- Poll Performance (with tasks) ---\n");
  double c_poll_us = benchmark_us(bench_c_poll_with_tasks, iterations);
  double cpp_poll_us = benchmark_us(bench_cpp_poll_with_tasks, iterations);
  printf("  C   ztask: %u polls in %.0f us (%.1f ns/op)\n",
         iterations, c_poll_us, c_poll_us * 1000.0 / iterations);
  printf("  C++ ztask: %u polls in %.0f us (%.1f ns/op)\n",
         iterations, cpp_poll_us, cpp_poll_us * 1000.0 / iterations);
  printf("  Ratio: %.2fx\n", cpp_poll_us / c_poll_us);
  printf("\n");

  // Poll idle
  printf("--- Poll Performance (idle) ---\n");
  double c_poll_idle_us = benchmark_us(bench_c_poll_idle, iterations);
  double cpp_poll_idle_us = benchmark_us(bench_cpp_poll_idle, iterations);
  printf("  C   ztask: %u polls in %.0f us (%.1f ns/op)\n",
         iterations, c_poll_idle_us, c_poll_idle_us * 1000.0 / iterations);
  printf("  C++ ztask: %u polls in %.0f us (%.1f ns/op)\n",
         iterations, cpp_poll_idle_us, cpp_poll_idle_us * 1000.0 / iterations);
  printf("  Ratio: %.2fx\n", cpp_poll_idle_us / c_poll_idle_us);
  printf("\n");

  // TicksToNextTask
  printf("--- TicksToNextTask Performance ---\n");
  double c_ticks_us = benchmark_us(bench_c_ticks_to_next, iterations);
  double cpp_ticks_us = benchmark_us(bench_cpp_ticks_to_next, iterations);
  printf("  C   ztask: %u calls in %.0f us (%.1f ns/op)\n",
         iterations, c_ticks_us, c_ticks_us * 1000.0 / iterations);
  printf("  C++ ztask: %u calls in %.0f us (%.1f ns/op)\n",
         iterations, cpp_ticks_us, cpp_ticks_us * 1000.0 / iterations);
  printf("  Ratio: %.2fx\n", cpp_ticks_us / c_ticks_us);
  printf("\n");

  // Bind/Unbind cycle
  printf("--- Bind+Unbind Cycle Performance ---\n");
  double c_cycle_us = benchmark_us(bench_c_bind_unbind_cycle, iterations);
  double cpp_cycle_us = benchmark_us(bench_cpp_bind_unbind_cycle, iterations);
  printf("  C   ztask: %u cycles in %.0f us (%.1f ns/op)\n",
         iterations, c_cycle_us, c_cycle_us * 1000.0 / iterations);
  printf("  C++ ztask: %u cycles in %.0f us (%.1f ns/op)\n",
         iterations, cpp_cycle_us, cpp_cycle_us * 1000.0 / iterations);
  printf("  Ratio: %.2fx\n", cpp_cycle_us / c_cycle_us);
  printf("\n");

  // Memory footprint
  printf("--- Memory Footprint ---\n");
  printf("  C   ztask_slot_t: %zu bytes\n", sizeof(ztask_slot_t));
  printf("  C++ TaskSlot: %zu bytes (estimated)\n", sizeof(ztask_slot_t));
  printf("  C   ztask_t (16 tasks): %zu bytes\n", sizeof(ztask_t));
  printf("  C++ TaskScheduler<16>: %zu bytes\n", sizeof(ztask::TaskScheduler<16>));
  printf("\n");

  printf("=== Benchmark Complete ===\n");

  return 0;
}
