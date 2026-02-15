# ztask-cpp

A C++14 header-only cooperative task scheduler for embedded systems.

## Features

- **Zero heap allocation**: Compile-time fixed capacity via template parameters
- **Tick-driven time base**: Integrates with hardware timers or main loop
- **O(1) poll operation**: Sorted list ensures constant-time head check
- **Periodic and one-shot tasks**: Flexible scheduling patterns
- **Precise sleep calculation**: `TicksToNextTask()` for low-power optimization
- **ABA-safe task IDs**: Generation counter prevents use-after-free
- **C++14 compatible**: Works with `-fno-exceptions -fno-rtti`
- **Platform agnostic**: Bare-metal MCU, RTOS, or embedded Linux

## Quick Start

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
  ztask
  GIT_REPOSITORY https://github.com/DeguiLiu/ztask-cpp.git
  GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(ztask)

target_link_libraries(your_target PRIVATE ztask::ztask)
```

### Basic Usage

```cpp
#include <ztask/ztask.hpp>
#include <cstdio>

void led_blink(void* ctx) {
  printf("LED toggle\n");
}

void sensor_read(void* ctx) {
  int* counter = static_cast<int*>(ctx);
  printf("Sensor read #%d\n", (*counter)++);
}

int main() {
  ztask::TaskScheduler<8> sched;
  int counter = 0;

  // Periodic task: blink LED every 100 ticks
  sched.Bind(led_blink, 100, 0);

  // Periodic task: read sensor every 50 ticks, start after 10 ticks
  sched.Bind(sensor_read, 50, 10, &counter);

  // One-shot task: initialization complete notification after 5 ticks
  sched.BindOneShot([](void*) { printf("Init complete\n"); }, 5);

  // Main loop
  for (uint32_t i = 0; i < 1000; ++i) {
    sched.Tick();
    sched.Poll();

    // Optional: calculate sleep time for low-power mode
    auto remaining = sched.TicksToNextTask();
    if (remaining > 0 && remaining != static_cast<uint32_t>(-1)) {
      // Sleep for 'remaining' ticks (platform-specific)
    }
  }

  return 0;
}
```

## API Reference

| Method | Description |
|--------|-------------|
| `Bind(fn, repeat_ticks, delay_ticks, ctx)` | Bind periodic task |
| `BindOneShot(fn, delay_ticks, ctx)` | Bind one-shot task |
| `Unbind(id)` | Remove task by ID |
| `Tick()` | Advance tick counter (call from ISR or main loop) |
| `Poll()` | Execute ready tasks |
| `TicksToNextTask()` | Calculate ticks until next task (for sleep) |
| `GetTicks()` | Get current tick count |
| `ActiveCount()` | Get number of active tasks |
| `IsEmpty()` | Check if scheduler is empty |

## Build & Test

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j

# Run tests
cd build && ctest --output-on-failure

# Run examples
./build/examples/basic_demo
./build/examples/low_power_demo
./build/examples/dynamic_demo
```

## Benchmark

Performance comparison vs original C ztask (1M operations, GCC 11.4, -O3):

| Operation | ztask-cpp | C ztask | Overhead |
|-----------|-----------|---------|----------|
| Bind      | 42 ns     | 45 ns   | -6.7%    |
| Poll (hit)| 38 ns     | 40 ns   | -5.0%    |
| Poll (miss)| 2 ns     | 2 ns    | 0%       |
| Unbind    | 35 ns     | 37 ns   | -5.4%    |

*ztask-cpp achieves comparable or better performance through template instantiation and inlining.*

## Design Highlights

- **Sorted intrusive list**: Tasks ordered by `next_schedule` for O(1) poll
- **TaskId encoding**: `(generation << 8) | slot_index` prevents stale ID reuse
- **Fixed array storage**: Cache-friendly, no fragmentation
- **Minimal state**: 32 bytes per task slot (on 32-bit platforms)

See [docs/design.md](docs/design.md) for detailed architecture.

## License

MIT License. See [LICENSE](LICENSE) for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history.
