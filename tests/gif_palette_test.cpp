#include "gif_palette.h"

#include <cstdint>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* what) {
  if (!condition) {
    std::println(stderr, "FAIL: {}", what);
    std::exit(1);
  }
}

// Packs LZW codes least significant bit first at a fixed width, the way a
// GIF stores them while the table stays small.
std::string pack(const std::vector<int>& codes, int width) {
  std::string out;
  uint32_t bits = 0;
  int count = 0;
  for (const int code : codes) {
    bits |= static_cast<uint32_t>(code) << count;
    count += width;
    while (count >= 8) {
      out.push_back(static_cast<char>(bits & 0xFF));
      bits >>= 8;
      count -= 8;
    }
  }
  if (count > 0) out.push_back(static_cast<char>(bits & 0xFF));
  return out;
}

// A 2x2 GIF whose frame carries a full 256-entry local table: entries 0-2
// are the picture's colours, the rest hold junk standing in for the heap
// bytes the office core's writer leaves there. Pixels: 0 1 / 2 1.
std::string junk_palette_gif(uint8_t junk, bool transparent_7) {
  std::string gif = "GIF89a";
  gif += std::string("\x02\x00\x02\x00", 4);
  gif += std::string("\x00\x00\x00", 3);  // No global table.
  if (transparent_7) {
    gif += std::string("\x21\xF9\x04\x01\x00\x00\x07\x00", 8);
  }
  gif += std::string("\x2C\x00\x00\x00\x00\x02\x00\x02\x00\x87", 10);
  for (int entry = 0; entry < 256; entry++) {
    for (int channel = 0; channel < 3; channel++) {
      gif.push_back(static_cast<char>(entry < 3 ? 0x40 * (entry + 1) + channel
                                                : junk + entry + channel));
    }
  }
  gif.push_back('\x08');
  // clear, 0, 1, 2, 1, end; the table grows past 511 only after 255 adds.
  const std::string data = pack({256, 0, 1, 2, 1, 257}, 9);
  gif.push_back(static_cast<char>(data.size()));
  gif += data;
  gif.push_back('\0');
  gif.push_back('\x3B');
  return gif;
}

constexpr size_t kTable = 6 + 4 + 3 + 10;

void verify_unused_entries_are_cleared() {
  std::string gif = junk_palette_gif(0x11, false);
  const std::string before = gif;
  require(grlibre::scrub_unused_gif_palette(&gif), "a well-formed GIF is scrubbed");
  require(gif.size() == before.size(), "scrubbing never changes the length");
  require(gif.compare(kTable, 9, before, kTable, 9) == 0, "used entries 0-2 are kept");
  for (size_t at = kTable + 9; at < kTable + 768; at++) {
    require(gif[at] == '\0', "every unused entry is zeroed");
  }
  require(gif.compare(kTable + 768, std::string::npos, before, kTable + 768,
                      std::string::npos) == 0,
          "the image data is untouched");
}

void verify_two_junk_fillings_scrub_alike() {
  std::string a = junk_palette_gif(0x11, false);
  std::string b = junk_palette_gif(0x5A, false);
  require(a != b, "the fixtures differ before scrubbing");
  require(grlibre::scrub_unused_gif_palette(&a) && grlibre::scrub_unused_gif_palette(&b),
          "both scrub");
  require(a == b, "the same picture scrubs to the same bytes");
}

void verify_transparent_index_is_kept() {
  const size_t table = kTable + 8;
  std::string gif = junk_palette_gif(0x11, true);
  const std::string before = gif;
  require(grlibre::scrub_unused_gif_palette(&gif), "scrubs with a control extension");
  require(gif.compare(table + 7 * 3, 3, before, table + 7 * 3, 3) == 0,
          "the transparent entry survives");
  require(gif[table + 8 * 3] == '\0', "entries past it are cleared");
}

void verify_malformed_input_is_left_alone() {
  std::string not_gif = "PNG not a gif";
  require(!grlibre::scrub_unused_gif_palette(&not_gif) && not_gif == "PNG not a gif",
          "a non-GIF is refused untouched");
  std::string truncated = junk_palette_gif(0x11, false);
  truncated.resize(truncated.size() - 6);
  const std::string before = truncated;
  require(!grlibre::scrub_unused_gif_palette(&truncated) && truncated == before,
          "a truncated GIF is refused untouched");
}

}  // namespace

int main() {
  verify_unused_entries_are_cleared();
  verify_two_junk_fillings_scrub_alike();
  verify_transparent_index_is_kept();
  verify_malformed_input_is_left_alone();
  std::println("gif-palette-test: ok");
  return 0;
}
