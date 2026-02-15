# Contributing to ztask-cpp

Thank you for your interest in contributing to ztask-cpp!

## Code of Conduct

Be respectful and constructive in all interactions.

## How to Contribute

### Reporting Bugs

Open an issue with:
- Clear description of the problem
- Minimal reproducible example
- Platform and compiler version
- Expected vs actual behavior

### Suggesting Features

Open an issue with:
- Use case description
- Proposed API design
- Performance/memory impact analysis

### Submitting Pull Requests

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Make your changes following the coding standards below
4. Add tests for new functionality
5. Ensure all tests pass: `ctest --output-on-failure`
6. Run sanitizers: `cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`
7. Update documentation if needed
8. Commit with clear messages (no AI attribution)
9. Push and open a pull request

## Coding Standards

### C++ Style

- Follow Google C++ Style Guide
- C++14 standard, no C++17/20 features
- Header-only implementation
- No exceptions, no RTTI (must work with `-fno-exceptions -fno-rtti`)
- Use fixed-width types: `uint32_t`, `uint8_t`, etc.
- Prefer `constexpr` and `noexcept` where applicable

### Naming Conventions

- Classes: `PascalCase`
- Functions/methods: `PascalCase`
- Variables: `snake_case`
- Private members: `snake_case_` (trailing underscore)
- Constants: `kPascalCase`
- Template parameters: `PascalCase`

### Documentation

- Doxygen-style comments for public API
- Explain non-obvious design decisions
- Include complexity analysis for algorithms

### Testing

- Use Catch2 v3 framework
- Test file naming: `test_<module>.cpp`
- Cover edge cases: overflow, empty scheduler, full capacity
- Verify with ASan, UBSan, TSan (if applicable)

### Performance

- Avoid heap allocation in hot paths
- Prefer stack allocation and compile-time sizing
- Document time/space complexity
- Benchmark performance-critical changes

## Development Workflow

```bash
# Clone
git clone https://github.com/DeguiLiu/ztask-cpp.git
cd ztask-cpp

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Test
cd build && ctest --output-on-failure

# Sanitizer check
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
cd build-asan && ctest --output-on-failure

# No-exceptions check
cmake -B build-noexcept -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"
cmake --build build-noexcept -j
cd build-noexcept && ctest --output-on-failure
```

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
