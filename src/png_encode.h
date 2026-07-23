#pragma once

#include <cstdint>
#include <string>

namespace grlibre {

// Encodes a 32-bit-per-pixel image as PNG. When bgra is true the buffer is
// BGRA (LibreOfficeKit's default tile mode) and is swapped to RGBA first.
// Returns the PNG bytes; empty on failure.
std::string encode_png(const std::uint8_t* pixels, int width, int height, bool bgra);

}  // namespace grlibre
