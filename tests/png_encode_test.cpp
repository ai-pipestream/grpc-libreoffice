#include "png_encode.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <zlib.h>

// A second, self-contained copy of the stb writer with its built-in
// compressor: the reference arm for the size guard. STB_IMAGE_WRITE_STATIC
// keeps it from colliding with the zlib-backed copy linked from
// png_encode.cpp.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace {

void require(bool condition, const char* what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}

const unsigned char kPngMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

std::uint32_t read_be32(const unsigned char* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

// Walks the chunk stream verifying structure and CRCs, checks the IHDR
// matches the expected 8-bit RGBA geometry, and returns the concatenated
// IDAT payload.
std::vector<unsigned char> validate_structure(const std::string& png, int width, int height) {
  require(png.size() > 8, "PNG longer than signature");
  require(std::memcmp(png.data(), kPngMagic, 8) == 0, "PNG magic");
  const auto* bytes = reinterpret_cast<const unsigned char*>(png.data());
  size_t pos = 8;
  bool saw_ihdr = false;
  bool saw_iend = false;
  std::vector<unsigned char> idat;
  while (pos < png.size()) {
    require(png.size() - pos >= 12, "chunk header and CRC fit");
    const std::uint32_t length = read_be32(bytes + pos);
    require(png.size() - pos - 12 >= length, "chunk data fits");
    const unsigned char* type = bytes + pos + 4;
    const unsigned char* data = bytes + pos + 8;
    const std::uint32_t stored_crc = read_be32(data + length);
    std::uint32_t crc = static_cast<std::uint32_t>(crc32(0L, type, 4));
    crc = static_cast<std::uint32_t>(crc32(crc, data, length));
    require(crc == stored_crc, "chunk CRC");
    if (std::memcmp(type, "IHDR", 4) == 0) {
      require(!saw_ihdr && pos == 8, "IHDR first and unique");
      saw_ihdr = true;
      require(length == 13, "IHDR length");
      require(read_be32(data) == static_cast<std::uint32_t>(width), "IHDR width");
      require(read_be32(data + 4) == static_cast<std::uint32_t>(height), "IHDR height");
      require(data[8] == 8, "bit depth 8");
      require(data[9] == 6, "color type RGBA");
      require(data[10] == 0, "compression method deflate");
      require(data[11] == 0, "filter method adaptive");
      require(data[12] == 0, "no interlace");
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), data, data + length);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      require(length == 0, "IEND empty");
      saw_iend = true;
    }
    pos += 12 + length;
  }
  require(saw_ihdr, "IHDR present");
  require(saw_iend, "IEND present");
  require(pos == png.size(), "no trailing bytes");
  require(!idat.empty(), "IDAT present");
  return idat;
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

// Inflates the IDAT stream and reverses the per-row filters, recovering the
// raw RGBA rows the encoder was fed.
std::vector<std::uint8_t> decode_pixels(const std::string& png, int width, int height) {
  std::vector<unsigned char> idat = validate_structure(png, width, height);
  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<unsigned char> filtered((stride + 1) * height);
  uLongf dest_len = static_cast<uLongf>(filtered.size());
  require(uncompress(filtered.data(), &dest_len, idat.data(),
                     static_cast<uLong>(idat.size())) == Z_OK,
          "IDAT inflates");
  require(dest_len == filtered.size(), "inflated size matches geometry");
  std::vector<std::uint8_t> pixels(stride * height);
  for (int y = 0; y < height; y++) {
    const unsigned char filter = filtered[y * (stride + 1)];
    require(filter <= 4, "known filter type");
    const unsigned char* in = filtered.data() + y * (stride + 1) + 1;
    std::uint8_t* out = pixels.data() + y * stride;
    const std::uint8_t* prior = y > 0 ? pixels.data() + (y - 1) * stride : nullptr;
    for (size_t x = 0; x < stride; x++) {
      const int a = x >= 4 ? out[x - 4] : 0;
      const int b = prior ? prior[x] : 0;
      const int c = (prior && x >= 4) ? prior[x - 4] : 0;
      int value = in[x];
      switch (filter) {
        case 1: value += a; break;
        case 2: value += b; break;
        case 3: value += (a + b) / 2; break;
        case 4: value += paeth(a, b, c); break;
        default: break;
      }
      out[x] = static_cast<std::uint8_t>(value & 0xff);
    }
  }
  return pixels;
}

void collect_bytes(void* context, void* data, int size) {
  auto* out = static_cast<std::string*>(context);
  out->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

// The stb writer with its built-in compressor at the default level 8: the
// encoder this change replaced, kept as the size ceiling.
std::string encode_reference(const std::vector<std::uint8_t>& rgba, int width, int height) {
  std::string png;
  require(stbi_write_png_to_func(collect_bytes, &png, width, height, 4,
                                 rgba.data(), width * 4) != 0,
          "reference encode succeeds");
  return png;
}

struct Fixture {
  const char* name;
  int width;
  int height;
  std::vector<std::uint8_t> rgba;
};

// Deterministic page-like fixtures matching the measured page classes: flat
// white (near-blank pages), smooth gradient (slide fills), dithered
// continuous tone (embedded photos, the encoder's worst case), and sharp
// stripes on white (text-like edges).
std::vector<Fixture> make_fixtures() {
  std::vector<Fixture> fixtures;
  {
    Fixture f{"white", 96, 64, {}};
    f.rgba.assign(static_cast<size_t>(f.width) * f.height * 4, 255);
    fixtures.push_back(std::move(f));
  }
  {
    Fixture f{"gradient", 96, 64, {}};
    f.rgba.resize(static_cast<size_t>(f.width) * f.height * 4);
    for (int y = 0; y < f.height; y++) {
      for (int x = 0; x < f.width; x++) {
        const size_t i = (static_cast<size_t>(y) * f.width + x) * 4;
        f.rgba[i] = static_cast<std::uint8_t>(x * 255 / (f.width - 1));
        f.rgba[i + 1] = static_cast<std::uint8_t>(y * 255 / (f.height - 1));
        f.rgba[i + 2] = static_cast<std::uint8_t>((x + y) & 0xff);
        f.rgba[i + 3] = 255;
      }
    }
    fixtures.push_back(std::move(f));
  }
  {
    Fixture f{"photo", 64, 64, {}};
    f.rgba.resize(static_cast<size_t>(f.width) * f.height * 4);
    std::uint32_t state = 0x12345678u;
    for (int y = 0; y < f.height; y++) {
      for (int x = 0; x < f.width; x++) {
        const size_t i = (static_cast<size_t>(y) * f.width + x) * 4;
        state = state * 1664525u + 1013904223u;
        const int dither = static_cast<int>(state >> 29);
        f.rgba[i] = static_cast<std::uint8_t>((x * x / 24 + y + dither) & 0xff);
        f.rgba[i + 1] = static_cast<std::uint8_t>((x + y * y / 16 + dither) & 0xff);
        f.rgba[i + 2] = static_cast<std::uint8_t>((x * y / 8 + dither) & 0xff);
        f.rgba[i + 3] = 255;
      }
    }
    fixtures.push_back(std::move(f));
  }
  {
    Fixture f{"stripes", 96, 64, {}};
    f.rgba.assign(static_cast<size_t>(f.width) * f.height * 4, 255);
    for (int y = 0; y < f.height; y++) {
      for (int x = 0; x < f.width; x++) {
        if ((x / 3 + y / 5) % 2 == 0) continue;
        const size_t i = (static_cast<size_t>(y) * f.width + x) * 4;
        f.rgba[i] = f.rgba[i + 1] = f.rgba[i + 2] = 0;
      }
    }
    fixtures.push_back(std::move(f));
  }
  return fixtures;
}

}  // namespace

int main() {
  // A 2x2 image: red, green, blue, white in RGBA order.
  std::vector<std::uint8_t> rgba = {
      255, 0, 0, 255,  0, 255, 0, 255,
      0, 0, 255, 255,  255, 255, 255, 255};
  std::string png = grlibre::encode_png(rgba.data(), 2, 2, /*bgra=*/false);
  require(png.size() > 8, "PNG produced");
  require(std::memcmp(png.data(), kPngMagic, 8) == 0, "PNG magic");

  std::string again = grlibre::encode_png(rgba.data(), 2, 2, /*bgra=*/false);
  require(png == again, "deterministic encoding");

  // The same buffer interpreted as BGRA swaps red and blue, so the encodings
  // must differ; a white pixel is swap-invariant, so it alone cannot mask a
  // broken swap here.
  std::string swapped = grlibre::encode_png(rgba.data(), 2, 2, /*bgra=*/true);
  require(png != swapped, "BGRA swap changes output");

  // The swapped encode must decode to exactly the red/blue-exchanged source.
  std::vector<std::uint8_t> expected_swapped = rgba;
  for (size_t i = 0; i < expected_swapped.size(); i += 4) {
    std::swap(expected_swapped[i], expected_swapped[i + 2]);
  }
  require(decode_pixels(swapped, 2, 2) == expected_swapped, "BGRA decode round-trip");

  require(grlibre::encode_png(rgba.data(), 0, 2, false).empty(), "zero width rejected");

  // Losslessness, structure, and size on page-like fixtures: every encode
  // must decode to the exact input bytes and never exceed the stb built-in
  // level-8 output this encoder replaced.
  for (const Fixture& fixture : make_fixtures()) {
    std::string encoded = grlibre::encode_png(fixture.rgba.data(), fixture.width,
                                              fixture.height, /*bgra=*/false);
    require(!encoded.empty(), "fixture encode succeeds");
    std::vector<std::uint8_t> decoded = decode_pixels(encoded, fixture.width, fixture.height);
    if (decoded != fixture.rgba) {
      std::cerr << "FAIL: pixels differ after round-trip on " << fixture.name << "\n";
      return 1;
    }
    std::string reference = encode_reference(fixture.rgba, fixture.width, fixture.height);
    if (encoded.size() > reference.size()) {
      std::cerr << "FAIL: " << fixture.name << " grew vs stb level 8: "
                << encoded.size() << " > " << reference.size() << "\n";
      return 1;
    }
  }

  std::cout << "png-encode-test passed\n";
  return 0;
}
