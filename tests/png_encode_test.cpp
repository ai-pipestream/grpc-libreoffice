#include "png_encode.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}

const unsigned char kPngMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

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

  require(grlibre::encode_png(rgba.data(), 0, 2, false).empty(), "zero width rejected");

  std::cout << "png-encode-test passed\n";
  return 0;
}
