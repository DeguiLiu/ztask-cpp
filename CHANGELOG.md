# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2025-02-15

### Added
- Initial release: C++14 header-only cooperative task scheduler
- `TaskScheduler<MaxTasks, TickType>` template class with compile-time capacity
- Periodic task support via `Bind(fn, repeat_ticks, delay_ticks, ctx)`
- One-shot task support via `BindOneShot(fn, delay_ticks, ctx)`
- `TicksToNextTask()` for precise sleep calculation in low-power applications
- ABA-safe TaskId encoding with generation counter
- O(1) poll operation via sorted intrusive linked list
- Zero heap allocation design for embedded systems
- Catch2 v3 test suite with comprehensive coverage
- Performance benchmark comparing against original C ztask
- CI: GitHub Actions (Linux + macOS, Debug + Release, ASan/UBSan)
- `-fno-exceptions -fno-rtti` compatibility verification
- Examples: basic_demo, low_power_demo, dynamic_demo
- Documentation: README, design document, API reference
- CMake build system with FetchContent support

### Performance
- 5-7% faster than original C ztask due to template inlining
- 32 bytes per task slot on 32-bit platforms
- O(1) ready task detection, O(n) insert/remove

### Compatibility
- C++14 standard
- GCC 7+, Clang 6+, MSVC 2017+ (not primary target)
- Bare-metal MCU, RTOS, embedded Linux
