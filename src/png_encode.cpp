#include "png_encode.h"

#include <vector>

// Vendored third-party header; its aggregate initializers trip -Wextra.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace grlibre {

namespace {

void append_bytes(void* context, void* data, int size) {
  auto* out = static_cast<std::string*>(context);
  out->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

}  // namespace

std::string encode_png(const std::uint8_t* pixels, int width, int height, bool bgra) {
  if (width <= 0 || height <= 0) return {};
  std::string png;
  const std::uint8_t* source = pixels;
  std::vector<std::uint8_t> swapped;
  if (bgra) {
    swapped.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < swapped.size(); i += 4) {
      std::swap(swapped[i], swapped[i + 2]);
    }
    source = swapped.data();
  }
  if (!stbi_write_png_to_func(append_bytes, &png, width, height, 4, source, width * 4)) {
    return {};
  }
  return png;
}

}  // namespace grlibre
