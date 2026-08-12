#pragma once

#include <cstdint>
#include <string>

namespace grlibre {

// The page image encodings the worker can produce. Values mirror
// ai.pipestream.office.v1.PageImageFormat's concrete entries.
enum class ImageFormat { kPng, kJpeg, kWebp };

// Encodes a 32-bit-per-pixel image as PNG. When bgra is true the buffer is
// BGRA (LibreOfficeKit's default tile mode) and is swapped to RGBA first.
// Returns the PNG bytes; empty on failure.
std::string encode_png(const std::uint8_t* pixels, int width, int height, bool bgra);

// Encodes a 32-bit-per-pixel image in the given format. quality applies to
// the lossy formats (1..100, higher is better) and is ignored for PNG.
// Returns the encoded bytes; empty on failure.
std::string encode_image(const std::uint8_t* pixels, int width, int height,
                         bool bgra, ImageFormat format, int quality);

}  // namespace grlibre
