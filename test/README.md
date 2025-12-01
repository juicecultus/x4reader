# MicroReader Test Suite

This directory contains the test suite for the MicroReader e-ink display project. The tests validate text layout, word providers, pagination, and rendering functionality.

## Directory Structure

```
test/
├── mocks/                      # Mock implementations for host testing
│   ├── Arduino.h              # Arduino API compatibility layer
│   ├── WString.h              # Arduino String mock
│   ├── SD.h                   # SD card file system mock
│   ├── platform_stubs.h       # Platform-specific stubs
│   └── platform_stubs.cpp     # Platform stub implementations
├── build/                      # Compiled test executables (generated)
├── output/                     # Test output files (generated)
├── test_utils.h               # Common test utilities and TestRunner
├── test_utils.cpp             # Test utilities implementation
├── test_config.h              # Configuration constants for tests
├── StringWordProviderBidirectionalTest.cpp
├── GreedyLayoutBidirectionalParagraphTest.cpp
├── TextLayoutPageRenderTest.cpp
└── README.md                   # This file
```

## Test Descriptions

### 1. StringWordProviderBidirectionalTest
**Purpose:** Validates that `StringWordProvider` and `FileWordProvider` can correctly tokenize and reconstruct text in both forward and backward directions.

**What it tests:**
- Forward traversal (reconstructing text by reading words left-to-right)
- Backward traversal (reconstructing text by reading words right-to-left)
- Consistency between String and File providers
- Step-by-step word-by-word comparison

**Expected behavior:** Both providers should reconstruct the original text identically when traversing forward or backward.

### 2. GreedyLayoutBidirectionalParagraphTest
**Purpose:** Tests the greedy layout strategy's ability to lay out paragraphs consistently in both forward and backward directions.

**What it tests:**
- Forward paragraph layout line-by-line
- Backward paragraph layout line-by-line
- Consistency of paragraph start/end positions
- Paragraph boundary detection

**Expected behavior:** Forward and backward passes should identify the same paragraph boundaries and positions.

### 3. TextLayoutPageRenderTest
**Purpose:** Tests pagination by laying out a document across multiple pages and verifying backward navigation.

**What it tests:**
- Multi-page layout with Knuth-Plass algorithm
- Forward page traversal
- Backward page navigation using `getPreviousPageStart()`
- Page boundary consistency

**Expected behavior:** Computing the previous page start from any page should match the start position computed during forward traversal.

**Outputs:** Saves rendered pages as PBM image files in `test/output/` for visual inspection.

## Running Tests

### Using VS Code (Recommended)

#### Run Individual Tests
1. Press `F5` or go to Run and Debug (`Ctrl+Shift+D`)
2. Select a test from the dropdown:
   - `StringWordProviderBidirectionalTest`
   - `TextLayoutPageRenderTest`
   - `GreedyLayoutBidirectionalParagraphTest`
3. Click the green play button or press `F5`

The test will automatically build before running and show output in the integrated terminal.

#### Build All Tests
1. Press `Ctrl+Shift+B`
2. Select `build all tests`

This will compile all tests in parallel.

#### Manual Build and Run

```powershell
# Build a specific test
g++ -g -DTEST_BUILD -Itest/mocks -Itest -Isrc/ui/screens/textview -Isrc/rendering -Isrc/core `
  test/StringWordProviderBidirectionalTest.cpp `
  src/ui/screens/textview/StringWordProvider.cpp `
  src/ui/screens/textview/FileWordProvider.cpp `
  src/ui/screens/textview/LayoutStrategy.cpp `
  src/rendering/TextRenderer.cpp `
  src/core/EInkDisplay.cpp `
  test/test_utils.cpp `
  test/mocks/platform_stubs.cpp `
  -o test/build/StringWordProviderBidirectionalTest.exe

# Run the test
.\test\build\StringWordProviderBidirectionalTest.exe

# Optional: specify a different test file
.\test\build\StringWordProviderBidirectionalTest.exe "path/to/test/file.txt"
```

## Test Configuration

Test parameters are centralized in `test_config.h`:

```cpp
namespace TestConfig {
  constexpr const char* DEFAULT_TEST_FILE = "data/books/chapter one.txt";
  constexpr int DISPLAY_WIDTH = 480;
  constexpr int DISPLAY_HEIGHT = 800;
  constexpr int DEFAULT_MARGIN_LEFT = 10;
  // ... etc
}
```

Modify these constants to change test behavior globally.

## Test Utilities

The `TestRunner` class in `test_utils.h` provides a simple test framework:

```cpp
TestUtils::TestRunner runner("My Test Suite");

// Test equality
runner.expectEqual(expected, actual, "Test name");

// Test condition
runner.expectTrue(condition, "Test name", "Optional failure message");

// Manual test result
runner.runTest("Test name", passed, "Optional failure message");

// Check if all passed
return runner.allPassed() ? 0 : 1;
```

The runner automatically:
- Tracks pass/fail counts
- Prints formatted test results with ✓/✗ symbols
- Reports detailed failure information
- Provides a summary at the end

## Understanding Test Output

### Successful Test Run
```
========================================
Test Suite: StringWordProvider Bidirectional Test
========================================
  ✓ PASS: String forward: reconstructed text equals original
  ✓ PASS: String backward: reconstructed text equals original
  ✓ PASS: File forward: reconstructed text equals original
  ✓ PASS: File backward: reconstructed text equals original
  ✓ PASS: Providers match forward: String and File produce same output
  ✓ PASS: Providers match backward: String and File produce same output
  ✓ PASS: Step-by-step forward: providers return identical words

========================================
Test Suite: StringWordProvider Bidirectional Test - Summary
========================================
Total tests: 7
  Passed: 7
  Failed: 0

✓ ALL TESTS PASSED
========================================
```

### Failed Test Run
```
  ✗ FAIL: String forward: reconstructed text equals original
    First difference at index 42
    Expected byte: 0x0a, Actual byte: 0x20
```

## Adding New Tests

1. **Create your test file** in the `test/` directory
2. **Include common headers:**
   ```cpp
   #include "test_utils.h"
   #include "test_config.h"
   ```
3. **Use TestRunner:**
   ```cpp
   int main() {
     TestUtils::TestRunner runner("My New Test");
     // ... your tests ...
     return runner.allPassed() ? 0 : 1;
   }
   ```
4. **Add build task** to `.vscode/tasks.json`
5. **Add launch configuration** to `.vscode/launch.json`

## Troubleshooting

### Build Errors
- Ensure g++ is in your PATH
- Check that all include paths are correct
- Verify source files exist at specified paths

### Test File Not Found
- Default test file: `data/books/chapter one.txt`
- Override with command line: `test.exe path/to/file.txt`
- Check that test data files exist

### Output Files
- PBM images: `test/output/output_XXX.pbm`
- Page ranges: `test/output/page_ranges.txt`
- These directories are created automatically

## CI/CD Integration

The test suite returns proper exit codes:
- `0` = All tests passed
- `1` = One or more tests failed
- `2` = Fatal error (file not found, etc.)

Example CI usage:
```powershell
.\test\run_tests.ps1
if ($LASTEXITCODE -ne 0) {
  Write-Error "Tests failed"
  exit 1
}
```

## Requirements

- **Compiler:** g++ (MinGW on Windows, GCC on Linux/Mac)
- **C++ Standard:** C++17 or later (for `<filesystem>`)
- **PowerShell:** Version 5.1+ (for run_tests.ps1)
- **Test Data:** Text files in `data/books/`

## Notes

- Tests run on the host (Windows/Linux/Mac), not on target hardware
- Mock implementations simulate Arduino APIs
- Display operations are stubbed out (no actual hardware needed)
- Rendering tests save PBM files for visual verification
- Tests are single-threaded and deterministic
