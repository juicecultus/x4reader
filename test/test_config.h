#pragma once

#include <string>

namespace TestConfig {

// Default test file paths
constexpr const char* DEFAULT_TEST_FILE = "data/books/chapter one.txt";
constexpr const char* FONT_TEST_FILE = "data/font test.txt";

// Display dimensions (typical e-ink display)
constexpr int DISPLAY_WIDTH = 480;
constexpr int DISPLAY_HEIGHT = 800;

// Default layout configuration values
constexpr int DEFAULT_MARGIN_LEFT = 10;
constexpr int DEFAULT_MARGIN_RIGHT = 10;
constexpr int DEFAULT_MARGIN_TOP = 40;
constexpr int DEFAULT_MARGIN_BOTTOM = 20;
constexpr int DEFAULT_LINE_HEIGHT = 30;
constexpr int DEFAULT_MIN_SPACE_WIDTH = 8;

// Small margin for tight tests
constexpr int TEST_MARGIN = 4;
constexpr int TEST_LINE_SPACING = 4;

// File buffer sizes
constexpr size_t FILE_BUFFER_SIZE = 1024;

// Test output directory
const std::string TEST_OUTPUT_DIR = "test/output";

// Dummy pin values for host tests (-1 = unused)
constexpr int DUMMY_PIN = -1;

}  // namespace TestConfig
