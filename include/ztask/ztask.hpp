// Copyright (c) 2025 dgliu
// SPDX-License-Identifier: MIT

#ifndef ZTASK_ZTASK_HPP_
#define ZTASK_ZTASK_HPP_

#include <cstdint>
#include <cstddef>

namespace ztask {

/**
 * @brief Header-only cooperative task scheduler for embedded systems.
 *
 * Features:
 * - Tick-driven time base
 * - Zero heap allocation (compile-time fixed capacity)
 * - O(1) poll operation (checks head of sorted list)
 * - Periodic and one-shot tasks
 * - Precise sleep calculation via TicksToNextTask()
 * - C++14 compatible, -fno-exceptions -fno-rtti safe
 *
 * @tparam MaxTasks Maximum number of concurrent tasks (compile-time constant)
 * @tparam TickType Tick counter type (uint32_t or uint64_t)
 */
template <uint32_t MaxTasks, typename TickType = uint32_t>
class TaskScheduler {
 public:
  /// Task function signature: void fn(void* ctx)
  using TaskFn = void (*)(void* ctx);

  /// Task identifier (opaque handle)
  using TaskId = uint16_t;

  /// Invalid task ID sentinel
  static constexpr TaskId kInvalidId = 0xFFFF;

  /**
   * @brief Construct a new TaskScheduler.
   *
   * All task slots are initially inactive.
   */
  TaskScheduler() noexcept
      : current_ticks_(0), active_count_(0), head_index_(kNoTask) {
    for (uint32_t i = 0; i < MaxTasks; ++i) {
      slots_[i].active = false;
      slots_[i].generation = 0;
      slots_[i].next_index = kNoTask;
    }
  }

  /**
   * @brief Advance the tick counter by one.
   *
   * Call this from your timer ISR or main loop at a fixed rate.
   */
  void Tick() noexcept {
    ++current_ticks_;
  }

  /**
   * @brief Get the current tick count.
   *
   * @return Current tick value
   */
  TickType GetTicks() const noexcept {
    return current_ticks_;
  }

  /**
   * @brief Reset tick counter to zero.
   *
   * Use with caution: may cause scheduling anomalies if tasks are active.
   */
  void ResetTicks() noexcept {
    current_ticks_ = 0;
  }

  /**
   * @brief Bind a periodic task.
   *
   * @param fn Task function pointer
   * @param repeat_ticks Period in ticks (0 = one-shot)
   * @param delay_ticks Initial delay before first execution
   * @param ctx User context pointer (passed to fn)
   * @return TaskId on success, kInvalidId if no slots available
   */
  TaskId Bind(TaskFn fn, TickType repeat_ticks, TickType delay_ticks,
              void* ctx = nullptr) noexcept {
    if (fn == nullptr || active_count_ >= MaxTasks) {
      return kInvalidId;
    }

    // Find free slot
    uint32_t slot_idx = kNoTask;
    for (uint32_t i = 0; i < MaxTasks; ++i) {
      if (!slots_[i].active) {
        slot_idx = i;
        break;
      }
    }

    if (slot_idx == kNoTask) {
      return kInvalidId;
    }

    // Initialize slot
    TaskSlot& slot = slots_[slot_idx];
    slot.fn = fn;
    slot.ctx = ctx;
    slot.repeat_ticks = repeat_ticks;
    slot.next_schedule = current_ticks_ + delay_ticks;
    slot.active = true;
    slot.next_index = kNoTask;

    // Insert into sorted list
    InsertSorted(slot_idx);

    ++active_count_;

    // Encode TaskId: (generation << 8) | slot_index
    return MakeTaskId(slot_idx, slot.generation);
  }

  /**
   * @brief Bind a one-shot task (executes once then auto-unbinds).
   *
   * @param fn Task function pointer
   * @param delay_ticks Delay before execution
   * @param ctx User context pointer
   * @return TaskId on success, kInvalidId if no slots available
   */
  TaskId BindOneShot(TaskFn fn, TickType delay_ticks,
                     void* ctx = nullptr) noexcept {
    return Bind(fn, 0, delay_ticks, ctx);
  }

  /**
   * @brief Unbind a task by ID.
   *
   * @param id TaskId returned by Bind() or BindOneShot()
   */
  void Unbind(TaskId id) noexcept {
    if (id == kInvalidId) {
      return;
    }

    uint32_t slot_idx = GetSlotIndex(id);
    uint8_t generation = GetGeneration(id);

    if (slot_idx >= MaxTasks) {
      return;
    }

    TaskSlot& slot = slots_[slot_idx];

    // Validate generation (prevent use-after-free)
    if (!slot.active || slot.generation != generation) {
      return;
    }

    // Remove from sorted list
    RemoveFromList(slot_idx);

    // Mark inactive and bump generation
    slot.active = false;
    slot.generation = static_cast<uint8_t>((slot.generation + 1) & 0xFF);

    --active_count_;
  }

  /**
   * @brief Poll and execute ready tasks.
   *
   * Checks the head of the sorted list. If next_schedule <= current_ticks,
   * executes the task and reschedules (periodic) or unbinds (one-shot).
   *
   * @return Number of tasks executed in this poll cycle
   */
  uint32_t Poll() noexcept {
    uint32_t executed = 0;

    while (head_index_ != kNoTask) {
      TaskSlot& head = slots_[head_index_];

      // Check if head task is ready
      if (head.next_schedule > current_ticks_) {
        break;  // Sorted list: no more ready tasks
      }

      // Remove from list before execution (in case fn calls Unbind)
      uint32_t exec_idx = head_index_;
      head_index_ = head.next_index;

      TaskSlot& exec_slot = slots_[exec_idx];

      // Execute task
      if (exec_slot.fn != nullptr) {
        exec_slot.fn(exec_slot.ctx);
      }

      ++executed;

      // Reschedule or unbind
      if (exec_slot.active) {  // May have been unbound during execution
        if (exec_slot.repeat_ticks > 0) {
          // Periodic: reschedule
          exec_slot.next_schedule = current_ticks_ + exec_slot.repeat_ticks;
          exec_slot.next_index = kNoTask;
          InsertSorted(exec_idx);
        } else {
          // One-shot: unbind
          exec_slot.active = false;
          exec_slot.generation =
              static_cast<uint8_t>((exec_slot.generation + 1) & 0xFF);
          --active_count_;
        }
      }
    }

    return executed;
  }

  /**
   * @brief Calculate ticks until next task is ready.
   *
   * Use this for precise sleep/idle calculation.
   *
   * @return Ticks until next task, or TickType(-1) if no tasks active
   */
  TickType TicksToNextTask() const noexcept {
    if (head_index_ == kNoTask) {
      return static_cast<TickType>(-1);  // No tasks
    }

    const TaskSlot& head = slots_[head_index_];

    if (head.next_schedule <= current_ticks_) {
      return 0;  // Task ready now
    }

    return head.next_schedule - current_ticks_;
  }

  /**
   * @brief Get number of active tasks.
   *
   * @return Active task count
   */
  uint32_t ActiveCount() const noexcept {
    return active_count_;
  }

  /**
   * @brief Get maximum task capacity.
   *
   * @return MaxTasks template parameter
   */
  uint32_t Capacity() const noexcept {
    return MaxTasks;
  }

  /**
   * @brief Check if scheduler has no active tasks.
   *
   * @return true if empty, false otherwise
   */
  bool IsEmpty() const noexcept {
    return active_count_ == 0;
  }

 private:
  static constexpr uint32_t kNoTask = 0xFFFFFFFF;

  struct TaskSlot {
    TaskFn fn;
    void* ctx;
    TickType repeat_ticks;
    TickType next_schedule;
    uint32_t next_index;  // Intrusive linked list
    uint8_t generation;   // ABA prevention
    bool active;
  };

  TaskSlot slots_[MaxTasks];
  TickType current_ticks_;
  uint32_t active_count_;
  uint32_t head_index_;  // Head of sorted list

  /**
   * @brief Encode TaskId from slot index and generation.
   */
  static TaskId MakeTaskId(uint32_t slot_idx, uint8_t generation) noexcept {
    return static_cast<TaskId>((generation << 8) | (slot_idx & 0xFF));
  }

  /**
   * @brief Extract slot index from TaskId.
   */
  static uint32_t GetSlotIndex(TaskId id) noexcept {
    return id & 0xFF;
  }

  /**
   * @brief Extract generation from TaskId.
   */
  static uint8_t GetGeneration(TaskId id) noexcept {
    return static_cast<uint8_t>(id >> 8);
  }

  /**
   * @brief Insert slot into sorted list by next_schedule (ascending).
   */
  void InsertSorted(uint32_t slot_idx) noexcept {
    TaskSlot& slot = slots_[slot_idx];

    // Empty list
    if (head_index_ == kNoTask) {
      head_index_ = slot_idx;
      slot.next_index = kNoTask;
      return;
    }

    // Insert before head
    if (slot.next_schedule < slots_[head_index_].next_schedule) {
      slot.next_index = head_index_;
      head_index_ = slot_idx;
      return;
    }

    // Find insertion point
    uint32_t prev_idx = head_index_;
    uint32_t curr_idx = slots_[prev_idx].next_index;

    while (curr_idx != kNoTask) {
      if (slot.next_schedule < slots_[curr_idx].next_schedule) {
        break;
      }
      prev_idx = curr_idx;
      curr_idx = slots_[curr_idx].next_index;
    }

    // Insert after prev_idx
    slot.next_index = curr_idx;
    slots_[prev_idx].next_index = slot_idx;
  }

  /**
   * @brief Remove slot from sorted list.
   */
  void RemoveFromList(uint32_t slot_idx) noexcept {
    if (head_index_ == kNoTask) {
      return;
    }

    // Remove head
    if (head_index_ == slot_idx) {
      head_index_ = slots_[slot_idx].next_index;
      return;
    }

    // Find predecessor
    uint32_t prev_idx = head_index_;
    uint32_t curr_idx = slots_[prev_idx].next_index;

    while (curr_idx != kNoTask) {
      if (curr_idx == slot_idx) {
        slots_[prev_idx].next_index = slots_[curr_idx].next_index;
        return;
      }
      prev_idx = curr_idx;
      curr_idx = slots_[curr_idx].next_index;
    }
  }
};

}  // namespace ztask

#endif  // ZTASK_ZTASK_HPP_
