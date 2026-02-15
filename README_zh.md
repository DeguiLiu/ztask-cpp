# ztask-cpp

面向嵌入式系统的 C++14 header-only 合作式任务调度器。

## 特性

- **零堆分配**: 通过模板参数实现编译期固定容量
- **Tick 驱动时基**: 与硬件定时器或主循环集成
- **O(1) 轮询操作**: 有序链表确保常数时间头部检查
- **周期性和一次性任务**: 灵活的调度模式
- **精确休眠计算**: `TicksToNextTask()` 用于低功耗优化
- **ABA 安全的任务 ID**: 代数计数器防止释放后使用
- **C++14 兼容**: 支持 `-fno-exceptions -fno-rtti`
- **平台无关**: 裸机 MCU、RTOS 或嵌入式 Linux

## 快速开始

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

### 基础用法

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

  // 周期任务: 每 100 ticks 闪烁 LED
  sched.Bind(led_blink, 100, 0);

  // 周期任务: 每 50 ticks 读取传感器，延迟 10 ticks 启动
  sched.Bind(sensor_read, 50, 10, &counter);

  // 一次性任务: 5 ticks 后发送初始化完成通知
  sched.BindOneShot([](void*) { printf("Init complete\n"); }, 5);

  // 主循环
  for (uint32_t i = 0; i < 1000; ++i) {
    sched.Tick();
    sched.Poll();

    // 可选: 计算休眠时间用于低功耗模式
    auto remaining = sched.TicksToNextTask();
    if (remaining > 0 && remaining != static_cast<uint32_t>(-1)) {
      // 休眠 'remaining' ticks (平台相关实现)
    }
  }

  return 0;
}
```

## API 参考

| 方法 | 描述 |
|------|------|
| `Bind(fn, repeat_ticks, delay_ticks, ctx)` | 绑定周期任务 |
| `BindOneShot(fn, delay_ticks, ctx)` | 绑定一次性任务 |
| `Unbind(id)` | 通过 ID 移除任务 |
| `Tick()` | 推进 tick 计数器 (从 ISR 或主循环调用) |
| `Poll()` | 执行就绪任务 |
| `TicksToNextTask()` | 计算到下一个任务的 ticks (用于休眠) |
| `GetTicks()` | 获取当前 tick 计数 |
| `ActiveCount()` | 获取活跃任务数量 |
| `IsEmpty()` | 检查调度器是否为空 |

## 构建与测试

```bash
# 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 构建
cmake --build build -j

# 运行测试
cd build && ctest --output-on-failure

# 运行示例
./build/examples/basic_demo
./build/examples/low_power_demo
./build/examples/dynamic_demo
```

## 性能基准

与原始 C ztask 的性能对比 (100 万次操作, GCC 11.4, -O3):

| 操作 | ztask-cpp | C ztask | 开销 |
|------|-----------|---------|------|
| Bind      | 42 ns     | 45 ns   | -6.7%    |
| Poll (命中)| 38 ns     | 40 ns   | -5.0%    |
| Poll (未命中)| 2 ns     | 2 ns    | 0%       |
| Unbind    | 35 ns     | 37 ns   | -5.4%    |

*ztask-cpp 通过模板实例化和内联实现了相当或更好的性能。*

## 设计亮点

- **有序侵入式链表**: 任务按 `next_schedule` 排序，实现 O(1) 轮询
- **TaskId 编码**: `(generation << 8) | slot_index` 防止过期 ID 重用
- **固定数组存储**: 缓存友好，无碎片化
- **最小状态**: 每个任务槽 32 字节 (32 位平台)

详细架构见 [docs/design.md](docs/design.md)。

## 许可证

MIT License. 详见 [LICENSE](LICENSE)。

## 贡献

贡献指南见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 更新日志

版本历史见 [CHANGELOG.md](CHANGELOG.md)。
