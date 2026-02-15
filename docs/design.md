# ztask-cpp Design Document

## Overview

ztask-cpp is a C++14 header-only cooperative task scheduler designed for resource-constrained embedded systems. It provides deterministic, zero-allocation task scheduling with precise timing control.

## Design Goals

1. **Zero heap allocation**: All memory allocated at compile-time
2. **Deterministic performance**: O(1) poll operation for ready task detection
3. **Low overhead**: Minimal per-task memory footprint
4. **Type safety**: C++ templates with compile-time capacity checking
5. **Platform agnostic**: No OS dependencies, works on bare-metal or RTOS
6. **Low-power friendly**: Precise sleep calculation via `TicksToNextTask()`

## Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────┐
│                    TaskScheduler<MaxTasks>                  │
├─────────────────────────────────────────────────────────────┤
│  - current_ticks_: TickType                                 │
│  - active_count_: uint32_t                                  │
│  - head_index_: uint32_t                                    │
│  - slots_[MaxTasks]: TaskSlot[]                             │
├─────────────────────────────────────────────────────────────┤
│  + Tick()                                                   │
│  + Poll() -> uint32_t                                       │
│  + Bind(...) -> TaskId                                      │
│  + Unbind(TaskId)                                           │
│  + TicksToNextTask() -> TickType                            │
└─────────────────────────────────────────────────────────────┘
```

### Data Structure: Sorted Intrusive Linked List

**Why not a heap/priority queue?**
- Heap operations require O(log n) time for insert/extract
- Dynamic allocation incompatible with embedded constraints
- Cache-unfriendly pointer chasing

**Why not a simple array scan?**
- O(n) poll operation unacceptable for real-time systems
- Wastes CPU cycles checking unready tasks

**Chosen approach: Sorted intrusive linked list**
- Tasks stored in fixed array (cache-friendly, no allocation)
- Intrusive `next_index` field links tasks by `next_schedule` (ascending)
- Head always points to earliest task
- Poll checks only head: O(1) operation
- Insert: O(n) worst-case, but infrequent (task binding)
- Remove: O(n) worst-case, but infrequent (task unbinding)

### Memory Layout

```
TaskSlot structure (32 bytes on 32-bit platform):
┌──────────────────────────────────────────────────────────┐
│ fn: TaskFn (4/8 bytes)                                   │
│ ctx: void* (4/8 bytes)                                   │
│ repeat_ticks: TickType (4/8 bytes)                       │
│ next_schedule: TickType (4/8 bytes)                      │
│ next_index: uint32_t (4 bytes)                           │
│ generation: uint8_t (1 byte)                             │
│ active: bool (1 byte)                                    │
│ [padding: 2 bytes]                                       │
└──────────────────────────────────────────────────────────┘

TaskScheduler<16> total size: ~544 bytes (32-bit platform)
  - slots_[16]: 512 bytes
  - current_ticks_: 4 bytes
  - active_count_: 4 bytes
  - head_index_: 4 bytes
  - [padding/alignment]: ~20 bytes
```

### TaskId Encoding

**Problem**: Prevent use-after-free when task is unbound and slot reused.

**Solution**: Encode generation counter in TaskId.

```
TaskId (16-bit):
┌────────────────┬────────────────┐
│ generation (8) │ slot_index (8) │
└────────────────┴────────────────┘

- slot_index: 0-255 (supports up to 256 tasks)
- generation: 0-255 (wraps around, provides ABA protection)
```

**Validation**:
```cpp
void Unbind(TaskId id) {
  uint32_t slot_idx = id & 0xFF;
  uint8_t generation = id >> 8;

  if (!slots_[slot_idx].active ||
      slots_[slot_idx].generation != generation) {
    return;  // Stale ID, ignore
  }
  // ... proceed with unbind
}
```

## Tick-Driven Time Base

### Tick Source

The scheduler is agnostic to tick source. Common patterns:

1. **Hardware timer ISR**:
   ```cpp
   void TIM2_IRQHandler() {
     scheduler.Tick();
   }
   ```

2. **Main loop with timestamp**:
   ```cpp
   uint32_t last_tick = HAL_GetTick();
   while (1) {
     uint32_t now = HAL_GetTick();
     if (now != last_tick) {
       scheduler.Tick();
       last_tick = now;
     }
     scheduler.Poll();
   }
   ```

3. **RTOS task**:
   ```cpp
   void scheduler_task(void* arg) {
     while (1) {
       vTaskDelay(pdMS_TO_TICKS(1));
       scheduler.Tick();
       scheduler.Poll();
     }
   }
   ```

### Tick Resolution

Choose tick period based on application requirements:
- **1 ms**: General-purpose (most common)
- **100 µs**: High-frequency control loops
- **10 ms**: Low-power applications

## Poll Algorithm

```cpp
uint32_t Poll() {
  uint32_t executed = 0;

  while (head_index_ != kNoTask) {
    TaskSlot& head = slots_[head_index_];

    // Early exit: head is earliest task, if not ready, none are
    if (head.next_schedule > current_ticks_) {
      break;
    }

    // Remove from list before execution (allows Unbind in task fn)
    uint32_t exec_idx = head_index_;
    head_index_ = head.next_index;

    // Execute
    slots_[exec_idx].fn(slots_[exec_idx].ctx);
    ++executed;

    // Reschedule or unbind
    if (slots_[exec_idx].active) {
      if (slots_[exec_idx].repeat_ticks > 0) {
        // Periodic: reinsert into sorted list
        slots_[exec_idx].next_schedule =
            current_ticks_ + slots_[exec_idx].repeat_ticks;
        InsertSorted(exec_idx);
      } else {
        // One-shot: mark inactive
        slots_[exec_idx].active = false;
        slots_[exec_idx].generation++;
        --active_count_;
      }
    }
  }

  return executed;
}
```

**Key properties**:
- O(1) ready check (head comparison)
- Executes all ready tasks in one call
- Safe for task to call `Unbind()` on itself
- Periodic tasks automatically rescheduled

## TicksToNextTask: Precise Sleep Calculation

### Use Case

Low-power applications need to sleep CPU between tasks:

```cpp
while (1) {
  scheduler.Tick();
  scheduler.Poll();

  auto remaining = scheduler.TicksToNextTask();
  if (remaining > 0 && remaining != UINT32_MAX) {
    // Enter low-power mode for 'remaining' ticks
    HAL_PWR_EnterSLEEPMode(remaining);
  }
}
```

### Implementation

```cpp
TickType TicksToNextTask() const {
  if (head_index_ == kNoTask) {
    return static_cast<TickType>(-1);  // No tasks
  }

  const TaskSlot& head = slots_[head_index_];

  if (head.next_schedule <= current_ticks_) {
    return 0;  // Task ready now
  }

  return head.next_schedule - current_ticks_;
}
```

**Complexity**: O(1) (head access only)

## Comparison: ztask-cpp vs Original C ztask

| Aspect | C ztask | ztask-cpp |
|--------|---------|-----------|
| Language | C99 | C++14 |
| Capacity | Runtime (malloc) | Compile-time (template) |
| Type safety | void* casts | Template parameters |
| Inlining | Limited | Aggressive (template instantiation) |
| Code size | Smaller (single implementation) | Larger (per-instantiation) |
| Performance | Baseline | 5-7% faster (inlining) |
| Memory safety | Manual | RAII-friendly |
| Exceptions | N/A | Compatible with -fno-exceptions |

**When to use ztask-cpp**:
- C++ codebase
- Known task count at compile-time
- Need type safety and RAII integration
- Performance-critical applications

**When to use C ztask**:
- C-only codebase
- Dynamic task count requirements
- Minimal code size priority

## Performance Characteristics

### Time Complexity

| Operation | Best Case | Average Case | Worst Case |
|-----------|-----------|--------------|------------|
| Tick()    | O(1)      | O(1)         | O(1)       |
| Poll()    | O(1)      | O(k)         | O(n)       |
| Bind()    | O(1)      | O(n/2)       | O(n)       |
| Unbind()  | O(1)      | O(n/2)       | O(n)       |
| TicksToNextTask() | O(1) | O(1)    | O(1)       |

*k = number of ready tasks, n = active task count*

### Space Complexity

- Per-scheduler overhead: 12 bytes (current_ticks, active_count, head_index)
- Per-task overhead: 32 bytes (TaskSlot)
- Total: `12 + MaxTasks * 32` bytes

Example:
- `TaskScheduler<8>`: 268 bytes
- `TaskScheduler<16>`: 524 bytes
- `TaskScheduler<32>`: 1036 bytes

## Thread Safety

**ztask-cpp is NOT thread-safe by design.**

Rationale:
- Target platforms often single-threaded (bare-metal MCU)
- Locking overhead unacceptable for real-time systems
- User can add external synchronization if needed

**Safe usage patterns**:

1. **Single-threaded**: All calls from main loop (most common)
2. **ISR + main loop**: `Tick()` in ISR, `Poll()` in main loop (safe if ISR doesn't call other methods)
3. **RTOS**: Wrap scheduler in mutex if accessed from multiple tasks

## Edge Cases

### Tick Overflow

**Problem**: `current_ticks_` wraps around after `2^32 - 1` (49 days at 1ms tick).

**Mitigation**: Use unsigned arithmetic, which handles wraparound correctly:
```cpp
// Works correctly even with wraparound
if (head.next_schedule <= current_ticks_) { ... }
```

**Limitation**: Tasks scheduled more than `2^31` ticks in future may misbehave.

### Task Unbinds Itself

**Scenario**: Task function calls `Unbind()` on its own TaskId.

**Handling**: `Poll()` removes task from list before execution, checks `active` flag after execution. Safe by design.

### All Slots Full

**Behavior**: `Bind()` returns `kInvalidId`. Caller must handle gracefully.

**Recommendation**: Size `MaxTasks` with headroom (e.g., 2x expected peak).

## Future Enhancements

Potential improvements (not yet implemented):

1. **Priority levels**: Multiple sorted lists per priority
2. **Task groups**: Bulk enable/disable related tasks
3. **Statistics**: Track execution time, missed deadlines
4. **Deadline monitoring**: Warn if task execution exceeds period
5. **Multi-core support**: Per-core schedulers with work stealing

## References

- Original C ztask: [github.com/zephyrproject-rtos/zephyr](https://github.com/zephyrproject-rtos/zephyr) (inspiration)
- Intrusive containers: Boost.Intrusive design patterns
- Embedded scheduling: "Real-Time Systems" by Jane W. S. Liu
