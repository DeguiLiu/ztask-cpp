// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <ztask/ztask.hpp>
#include <cstdio>
#include <cstdint>

// Simulate MCU low-power modes
enum class PowerMode {
  kActive,
  kIdle,
  kSleep,
  kDeepSleep
};

struct PowerStats {
  uint32_t active_ticks;
  uint32_t idle_ticks;
  uint32_t sleep_ticks;
  uint32_t deep_sleep_ticks;
};

// Select power mode based on sleep duration
PowerMode SelectPowerMode(uint32_t sleep_ticks) {
  if (sleep_ticks == 0) {
    return PowerMode::kActive;
  } else if (sleep_ticks < 5) {
    return PowerMode::kIdle;  // Fast wake-up
  } else if (sleep_ticks < 50) {
    return PowerMode::kSleep;  // Medium wake-up
  } else {
    return PowerMode::kDeepSleep;  // Slow wake-up, lowest power
  }
}

const char* PowerModeToString(PowerMode mode) {
  switch (mode) {
    case PowerMode::kActive: return "Active";
    case PowerMode::kIdle: return "Idle";
    case PowerMode::kSleep: return "Sleep";
    case PowerMode::kDeepSleep: return "DeepSleep";
    default: return "Unknown";
  }
}

// Task functions
void fast_task(void* ctx) {
  auto* counter = static_cast<uint32_t*>(ctx);
  (*counter)++;
  printf("[Tick %4u] Fast task executed (count: %u)\n",
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx) >> 32), *counter);
}

void medium_task(void* ctx) {
  printf("[Tick %4u] Medium task executed\n",
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx)));
}

void slow_task(void* ctx) {
  printf("[Tick %4u] Slow task executed\n",
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx)));
}

int main() {
  printf("=== ztask-cpp Low-Power Demo ===\n\n");
  printf("Simulating MCU power management with TicksToNextTask()\n\n");

  ztask::TaskScheduler<8> sched;
  PowerStats stats = {0, 0, 0, 0};
  uint32_t fast_count = 0;

  // Bind tasks with different periods
  sched.Bind(fast_task, 10, 0, &fast_count);      // Every 10 ticks
  sched.Bind(medium_task, 50, 5, nullptr);        // Every 50 ticks, delay 5
  sched.Bind(slow_task, 200, 20, nullptr);        // Every 200 ticks, delay 20

  printf("Tasks bound:\n");
  printf("  - Fast task: period 10 ticks\n");
  printf("  - Medium task: period 50 ticks, delay 5 ticks\n");
  printf("  - Slow task: period 200 ticks, delay 20 ticks\n\n");

  printf("Power mode selection:\n");
  printf("  - Active: 0 ticks (task ready)\n");
  printf("  - Idle: 1-4 ticks (fast wake-up)\n");
  printf("  - Sleep: 5-49 ticks (medium wake-up)\n");
  printf("  - DeepSleep: 50+ ticks (slow wake-up, lowest power)\n\n");

  printf("Starting simulation...\n\n");

  // Main loop: simulate 500 ticks
  for (uint32_t i = 0; i < 500; ++i) {
    sched.Tick();

    // Execute ready tasks
    uint32_t executed = sched.Poll();

    if (executed > 0) {
      stats.active_ticks++;
    } else {
      // Calculate sleep time
      uint32_t remaining = sched.TicksToNextTask();

      if (remaining == static_cast<uint32_t>(-1)) {
        printf("[Tick %4u] No tasks scheduled, entering infinite sleep\n", i);
        break;
      }

      // Select power mode
      PowerMode mode = SelectPowerMode(remaining);

      // Update statistics
      switch (mode) {
        case PowerMode::kActive:
          stats.active_ticks++;
          break;
        case PowerMode::kIdle:
          stats.idle_ticks++;
          break;
        case PowerMode::kSleep:
          stats.sleep_ticks++;
          break;
        case PowerMode::kDeepSleep:
          stats.deep_sleep_ticks++;
          break;
      }

      // Log power mode transitions (sample every 50 ticks)
      if (i % 50 == 0) {
        printf("[Tick %4u] Entering %s mode for %u ticks\n",
               i, PowerModeToString(mode), remaining);
      }
    }
  }

  // Calculate power efficiency
  uint32_t total_ticks = stats.active_ticks + stats.idle_ticks +
                         stats.sleep_ticks + stats.deep_sleep_ticks;

  printf("\n=== Power Statistics ===\n");
  printf("Total ticks: %u\n", total_ticks);
  printf("  Active:     %5u ticks (%5.2f%%) - Full power\n",
         stats.active_ticks,
         100.0f * stats.active_ticks / total_ticks);
  printf("  Idle:       %5u ticks (%5.2f%%) - ~80%% power\n",
         stats.idle_ticks,
         100.0f * stats.idle_ticks / total_ticks);
  printf("  Sleep:      %5u ticks (%5.2f%%) - ~20%% power\n",
         stats.sleep_ticks,
         100.0f * stats.sleep_ticks / total_ticks);
  printf("  DeepSleep:  %5u ticks (%5.2f%%) - ~5%% power\n",
         stats.deep_sleep_ticks,
         100.0f * stats.deep_sleep_ticks / total_ticks);

  // Estimate average power consumption (normalized to active = 100%)
  float avg_power = (stats.active_ticks * 100.0f +
                     stats.idle_ticks * 80.0f +
                     stats.sleep_ticks * 20.0f +
                     stats.deep_sleep_ticks * 5.0f) / total_ticks;

  printf("\nEstimated average power: %.2f%% of active mode\n", avg_power);
  printf("Power savings: %.2f%%\n", 100.0f - avg_power);

  printf("\n=== Demo Complete ===\n");

  return 0;
}
