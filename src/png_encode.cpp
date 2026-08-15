#include "png_encode.h"

#include <vector>

#include <webp/encode.h>
#include <zlib.h>

// stb's built-in deflate is routed through real zlib below: measured on
// captured worker bitmaps it emits ~28% fewer PageImage bytes on a mixed
// document set and encodes text pages faster (73 -> 43 ms), while stb's own
// compressor caps its match search regardless of level.
static unsigned char* grlibre_zlib_compress(unsigned char* data, int data_len,
                                            int* out_len, int quality);

// Vendored third-party header; its aggregate initializers trip -Wextra.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STBIW_ZLIB_COMPRESS grlibre_zlib_compress
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma GCC diagnostic pop

// Level 6 is pinned by measurement, ignoring stb's global level knob: level 9
// costs 2.8x the stream completion time for <=2.7% fewer bytes.
static unsigned char* grlibre_zlib_compress(unsigned char* data, int data_len,
                                            int* out_len, int quality) {
  (void)quality;
  const uLong bound = compressBound(static_cast<uLong>(data_len));
  auto* out = static_cast<unsigned char*>(STBIW_MALLOC(bound));
  if (!out) return nullptr;
  z_stream stream{};
  if (deflateInit2(&stream, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    STBIW_FREE(out);
    return nullptr;
  }
  stream.next_in = data;
  stream.avail_in = static_cast<uInt>(data_len);
  stream.next_out = out;
  stream.avail_out = static_cast<uInt>(bound);
  const int rc = deflate(&stream, Z_FINISH);
  deflateEnd(&stream);
  if (rc != Z_STREAM_END) {
    STBIW_FREE(out);
    return nullptr;
  }
  *out_len = static_cast<int>(stream.total_out);
  return out;
}

namespace grlibre {

namespace {

void append_bytes(void* context, void* data, int size) {
  auto* out = static_cast<std::string*>(context);
  out->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

// Returns pixels as RGBA, swapping a BGRA buffer through *storage. stb's
// writers only take RGBA; libwebp has a native BGRA entry point and skips
// this.
const std::uint8_t* as_rgba(const std::uint8_t* pixels, int width, int height,
                            bool bgra, std::vector<std::uint8_t>* storage) {
  if (!bgra) return pixels;
  storage->assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
  for (size_t i = 0; i < storage->size(); i += 4) {
    std::swap((*storage)[i], (*storage)[i + 2]);
  }
  return storage->data();
}

}  // namespace

void grayscale_pixels(std::uint8_t* pixels, int width, int height, bool bgra) {
  if (pixels == nullptr || width <= 0 || height <= 0) return;
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  for (size_t i = 0; i < count; i++) {
    std::uint8_t* p = pixels + i * 4;
    const int b = bgra ? p[0] : p[2];
    const int g = p[1];
    const int r = bgra ? p[2] : p[0];
    const std::uint8_t y = static_cast<std::uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
    p[0] = y;
    p[1] = y;
    p[2] = y;
  }
}

std::string encode_png(const std::uint8_t* pixels, int width, int height, bool bgra) {
  if (width <= 0 || height <= 0) return {};
  std::string png;
  std::vector<std::uint8_t> swapped;
  const std::uint8_t* source = as_rgba(pixels, width, height, bgra, &swapped);
  if (!stbi_write_png_to_func(append_bytes, &png, width, height, 4, source, width * 4)) {
    return {};
  }
  return png;
}

std::string encode_image(const std::uint8_t* pixels, int width, int height,
                         bool bgra, ImageFormat format, int quality) {
  if (width <= 0 || height <= 0) return {};
  switch (format) {
    case ImageFormat::kPng:
      return encode_png(pixels, width, height, bgra);
    case ImageFormat::kJpeg: {
      std::string jpeg;
      std::vector<std::uint8_t> swapped;
      const std::uint8_t* source = as_rgba(pixels, width, height, bgra, &swapped);
      if (!stbi_write_jpg_to_func(append_bytes, &jpeg, width, height, 4,
                                  source, quality)) {
        return {};
      }
      return jpeg;
    }
    case ImageFormat::kWebp: {
      std::uint8_t* output = nullptr;
      size_t size = bgra
          ? WebPEncodeBGRA(pixels, width, height, width * 4,
                           static_cast<float>(quality), &output)
          : WebPEncodeRGBA(pixels, width, height, width * 4,
                           static_cast<float>(quality), &output);
      if (size == 0 || output == nullptr) {
        WebPFree(output);
        return {};
      }
      std::string webp(reinterpret_cast<const char*>(output), size);
      WebPFree(output);
      return webp;
    }
  }
  return {};
}

}  // namespace grlibre
