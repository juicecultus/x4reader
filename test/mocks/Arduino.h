#pragma once

// Tiny Arduino compatibility shim for host tests. Reuse platform stubs.
#include <ctype.h>

#include "WString.h"
#include "platform_stubs.h"

#define HEX 16

inline bool isPrintable(char c) {
  return std::isprint(static_cast<unsigned char>(c));
}
