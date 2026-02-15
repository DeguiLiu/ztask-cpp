// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#include <ztask/ztask.hpp>
#include <cstdio>
#include <cstdint>

// Demonstrate dynamic task management: bind, unbind, query

struct TaskContext {
  uint32_t id;
  uint32_t exec_count;
};

void dynamic_task(void* ctx) {
  auto* task_ctx = static_cast<TaskContext*>(ctx);
  task_ctx->exec_count++;
  printf("[Task %u] Executed (count: %u)\n", task_ctx->id, task_ctx->exec_count);
}

void print_scheduler_state(const ztask::TaskScheduler<16>& sched, uint32_t tick) {
  printf("[Tick %4u] Scheduler state: %u/%u tasks active, %s\n",
         tick,
         sched.ActiveCount(),
         sched.Capacity(),
         sched.IsEmpty() ? "EMPTY" : "ACTIVE");

  auto remaining = sched.TicksToNextTask();
  if (remaining == static_cast<uint32_t>(-1)) {
    printf("            Next task: NONE\n");
  } else if (remaining == 0) {
    printf("            Next task: READY NOW\n");
  } else {
    printf("            Next task: in %u ticks\n", remaining);
  }
}

int main() {
  printf("=== ztask-cpp Dynamic Task Management Demo ===\n\n");

  ztask::TaskScheduler<16> sched;
  TaskContext contexts[8];

  // Initialize contexts
  for (uint32_t i = 0; i < 8; ++i) {
    contexts[i].id = i;
    contexts[i].exec_count = 0;
  }

  printf("Phase 1: Bind multiple tasks\n");
  printf("-----------------------------\n");

  ztask::TaskScheduler<16>::TaskId task_ids[8];

  // Bind 5 tasks with different periods
  task_ids[0] = sched.Bind(dynamic_task, 20, 0, &contexts[0]);
  task_ids[1] = sched.Bind(dynamic_task, 30, 5, &contexts[1]);
  task_ids[2] = sched.Bind(dynamic_task, 40, 10, &contexts[2]);
  task_ids[3] = sched.Bind(dynamic_task, 50, 15, &contexts[3]);
  task_ids[4] = sched.Bind(dynamic_task, 60, 20, &contexts[4]);

  printf("Bound 5 tasks:\n");
  for (uint32_t i = 0; i < 5; ++i) {
    printf("  Task %u: ID=0x%04X\n", i, task_ids[i]);
  }
  printf("\n");

  print_scheduler_state(sched, 0);
  printf("\n");

  // Run for 100 ticks
  printf("Running for 100 ticks...\n\n");
  for (uint32_t i = 0; i < 100; ++i) {
    sched.Tick();
    sched.Poll();
  }

  print_scheduler_state(sched, 100);
  printf("\n");

  // Phase 2: Unbind some tasks
  printf("Phase 2: Unbind tasks 1 and 3\n");
  printf("------------------------------\n");

  sched.Unbind(task_ids[1]);
  sched.Unbind(task_ids[3]);
  printf("Unbound tasks 1 and 3\n\n");

  print_scheduler_state(sched, 100);
  printf("\n");

  // Run for another 100 ticks
  printf("Running for another 100 ticks...\n\n");
  for (uint32_t i = 100; i < 200; ++i) {
    sched.Tick();
    sched.Poll();
  }

  print_scheduler_state(sched, 200);
  printf("\n");

  // Phase 3: Bind new tasks (reuse slots)
  printf("Phase 3: Bind new tasks (reuse freed slots)\n");
  printf("--------------------------------------------\n");

  task_ids[5] = sched.Bind(dynamic_task, 25, 0, &contexts[5]);
  task_ids[6] = sched.Bind(dynamic_task, 35, 5, &contexts[6]);
  task_ids[7] = sched.BindOneShot(dynamic_task, 10, &contexts[7]);

  printf("Bound 3 new tasks:\n");
  printf("  Task 5: ID=0x%04X (periodic, period 25)\n", task_ids[5]);
  printf("  Task 6: ID=0x%04X (periodic, period 35)\n", task_ids[6]);
  printf("  Task 7: ID=0x%04X (one-shot, delay 10)\n", task_ids[7]);
  printf("\n");

  print_scheduler_state(sched, 200);
  printf("\n");

  // Run for another 100 ticks
  printf("Running for another 100 ticks...\n\n");
  for (uint32_t i = 200; i < 300; ++i) {
    sched.Tick();
    sched.Poll();

    // One-shot task should auto-unbind
    if (i == 210) {
      printf("[Tick %4u] One-shot task should have auto-unbound\n", i);
      print_scheduler_state(sched, i);
      printf("\n");
    }
  }

  print_scheduler_state(sched, 300);
  printf("\n");

  // Phase 4: Test TaskId validation (stale ID)
  printf("Phase 4: Test stale TaskId validation\n");
  printf("--------------------------------------\n");

  auto stale_id = task_ids[1];  // Already unbound in Phase 2
  printf("Attempting to unbind stale TaskId 0x%04X (should be ignored)\n", stale_id);
  sched.Unbind(stale_id);
  printf("No crash - stale ID correctly rejected\n\n");

  print_scheduler_state(sched, 300);
  printf("\n");

  // Phase 5: Unbind all remaining tasks
  printf("Phase 5: Unbind all remaining tasks\n");
  printf("------------------------------------\n");

  for (uint32_t i = 0; i < 8; ++i) {
    if (task_ids[i] != ztask::TaskScheduler<16>::kInvalidId) {
      sched.Unbind(task_ids[i]);
    }
  }

  printf("All tasks unbound\n\n");
  print_scheduler_state(sched, 300);
  printf("\n");

  // Print execution statistics
  printf("=== Execution Statistics ===\n");
  for (uint32_t i = 0; i < 8; ++i) {
    if (contexts[i].exec_count > 0) {
      printf("  Task %u: %u executions\n", i, contexts[i].exec_count);
    }
  }

  printf("\n=== Demo Complete ===\n");

  return 0;
}
