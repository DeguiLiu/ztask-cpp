// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <ztask/ztask.hpp>
#include <cstdio>
#include <cstdint>

// Task context structures
struct LedContext {
  uint32_t toggle_count;
  bool state;
};

struct SensorContext {
  uint32_t sample_count;
  float last_value;
};

// Task functions
void led_blink(void* ctx) {
  auto* led = static_cast<LedContext*>(ctx);
  led->state = !led->state;
  led->toggle_count++;
  printf("[%s] LED %s (toggle #%u)\n",
         __func__, led->state ? "ON" : "OFF", led->toggle_count);
}

void sensor_read(void* ctx) {
  auto* sensor = static_cast<SensorContext*>(ctx);
  sensor->last_value = 20.0f + (sensor->sample_count % 10) * 0.5f;
  sensor->sample_count++;
  printf("[%s] Temperature: %.1f°C (sample #%u)\n",
         __func__, sensor->last_value, sensor->sample_count);
}

void log_status(void* ctx) {
  auto* sched = static_cast<ztask::TaskScheduler<8>*>(ctx);
  printf("[%s] Active tasks: %u/%u, Ticks: %u\n",
         __func__, sched->ActiveCount(), sched->Capacity(), sched->GetTicks());
}

void init_complete(void* ctx) {
  printf("[%s] System initialization complete!\n", __func__);
}

int main() {
  printf("=== ztask-cpp Basic Demo ===\n\n");

  // Create scheduler with capacity for 8 tasks
  ztask::TaskScheduler<8> sched;

  // Task contexts
  LedContext led_ctx = {0, false};
  SensorContext sensor_ctx = {0, 0.0f};

  // Bind periodic tasks
  auto led_id = sched.Bind(led_blink, 100, 0, &led_ctx);
  printf("Bound LED blink task (ID: 0x%04X, period: 100 ticks)\n", led_id);

  auto sensor_id = sched.Bind(sensor_read, 50, 10, &sensor_ctx);
  printf("Bound sensor read task (ID: 0x%04X, period: 50 ticks, delay: 10 ticks)\n", sensor_id);

  auto log_id = sched.Bind(log_status, 200, 0, &sched);
  printf("Bound log status task (ID: 0x%04X, period: 200 ticks)\n", log_id);

  // Bind one-shot task
  auto init_id = sched.BindOneShot(init_complete, 5, nullptr);
  printf("Bound init complete task (ID: 0x%04X, one-shot, delay: 5 ticks)\n\n", init_id);

  // Main loop: simulate 1000 ticks
  printf("Starting main loop...\n\n");

  for (uint32_t i = 0; i < 1000; ++i) {
    // Advance tick counter
    sched.Tick();

    // Execute ready tasks
    uint32_t executed = sched.Poll();

    // Optional: calculate sleep time for low-power mode
    if (executed == 0) {
      auto remaining = sched.TicksToNextTask();
      if (remaining > 0 && remaining != static_cast<uint32_t>(-1)) {
        // In real application, enter low-power mode here
        // printf("Could sleep for %u ticks\n", remaining);
      }
    }

    // Demonstrate dynamic unbind at tick 500
    if (i == 500) {
      printf("\n[main] Unbinding LED task at tick 500\n\n");
      sched.Unbind(led_id);
    }

    // Demonstrate dynamic rebind at tick 700
    if (i == 700) {
      printf("\n[main] Rebinding LED task at tick 700 with new period\n\n");
      led_id = sched.Bind(led_blink, 150, 0, &led_ctx);
    }
  }

  printf("\n=== Demo Complete ===\n");
  printf("Final statistics:\n");
  printf("  LED toggles: %u\n", led_ctx.toggle_count);
  printf("  Sensor samples: %u\n", sensor_ctx.sample_count);
  printf("  Active tasks: %u\n", sched.ActiveCount());

  return 0;
}
