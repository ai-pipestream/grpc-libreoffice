#include "gif_palette.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace grlibre {
namespace {

struct ColourTable {
  size_t offset = 0;
  size_t entries = 0;
};

// Concatenates a run of data sub-blocks starting at *at, leaving *at past
// the terminator. False when the run overruns the stream.
bool read_sub_blocks(const std::string& gif, size_t* at, std::string* out) {
  while (true) {
    if (*at >= gif.size()) return false;
    const size_t length = static_cast<unsigned char>(gif[*at]);
    ++*at;
    if (length == 0) return true;
    if (*at + length > gif.size()) return false;
    if (out != nullptr) out->append(gif, *at, length);
    *at += length;
  }
}

// Decodes one frame's LZW data and marks every colour index its pixels
// use. False on a code the stream could not have produced.
bool mark_used_indices(const std::string& data, int min_code_size,
                       std::array<bool, 256>* used) {
  if (min_code_size < 2 || min_code_size > 8) return false;
  const int clear = 1 << min_code_size;
  const int end = clear + 1;
  // Each code's prefix and last byte; first byte is tracked so a code's
  // successor can be built without walking the chain.
  std::vector<int> prefix(4096, -1);
  std::vector<uint8_t> suffix(4096, 0);
  std::vector<uint8_t> first(4096, 0);
  for (int i = 0; i < clear; i++) {
    suffix[i] = static_cast<uint8_t>(i);
    first[i] = static_cast<uint8_t>(i);
  }
  int code_size = min_code_size + 1;
  int next = end + 1;
  int previous = -1;
  uint32_t bits = 0;
  int bit_count = 0;
  for (const char byte : data) {
    bits |= static_cast<uint32_t>(static_cast<unsigned char>(byte)) << bit_count;
    bit_count += 8;
    while (bit_count >= code_size) {
      const int code = static_cast<int>(bits & ((1U << code_size) - 1));
      bits >>= code_size;
      bit_count -= code_size;
      if (code == clear) {
        code_size = min_code_size + 1;
        next = end + 1;
        previous = -1;
        continue;
      }
      if (code == end) return true;
      if (previous < 0) {
        if (code >= clear) return false;
        (*used)[code] = true;
        previous = code;
        continue;
      }
      if (code > next || (code == next && next >= 4096)) return false;
      // A code's indices are its chain's suffixes; only the set matters, so
      // the chain is walked rather than expanded in order.
      const uint8_t head = code < next ? first[code] : first[previous];
      if (next < 4096) {
        prefix[next] = previous;
        suffix[next] = head;
        first[next] = first[previous];
        next++;
        if (next == (1 << code_size) && code_size < 12) code_size++;
      }
      for (int walk = code; walk >= 0; walk = prefix[walk]) {
        (*used)[suffix[walk]] = true;
        if (walk < clear) break;
      }
      previous = code;
    }
  }
  return true;  // No end code: decoders accept the frame as it stands.
}

void clear_unused(std::string* gif, const ColourTable& table,
                  const std::array<bool, 256>& used) {
  for (size_t entry = 0; entry < table.entries; entry++) {
    if (used[entry]) continue;
    for (size_t channel = 0; channel < 3; channel++) {
      (*gif)[table.offset + entry * 3 + channel] = '\0';
    }
  }
}

}  // namespace

bool scrub_unused_gif_palette(std::string* gif) {
  if (gif == nullptr || gif->size() < 13 ||
      (gif->compare(0, 6, "GIF87a") != 0 && gif->compare(0, 6, "GIF89a") != 0)) {
    return false;
  }
  const auto byte = [&](size_t at) {
    return static_cast<unsigned char>((*gif)[at]);
  };
  size_t at = 13;
  ColourTable global;
  if ((byte(10) & 0x80) != 0) {
    global = {at, size_t{1} << ((byte(10) & 0x07) + 1)};
    at += global.entries * 3;
    if (at > gif->size()) return false;
  }
  std::array<bool, 256> global_used{};
  struct Frame {
    ColourTable table;
    std::array<bool, 256> used{};
  };
  std::vector<Frame> frames;
  while (true) {
    if (at >= gif->size()) return false;
    const unsigned char kind = byte(at++);
    if (kind == 0x3B) break;  // Trailer.
    if (kind == 0x21) {        // Extension: label, then sub-blocks.
      if (at >= gif->size()) return false;
      const unsigned char label = byte(at++);
      // A graphic control extension's transparent index is a pixel value
      // too: keep its entry whatever the pixels say.
      if (label == 0xF9 && at + 5 < gif->size() && byte(at) == 4 &&
          (byte(at + 1) & 0x01) != 0) {
        const unsigned char transparent = byte(at + 4);
        global_used[transparent] = true;
      }
      if (!read_sub_blocks(*gif, &at, nullptr)) return false;
      continue;
    }
    if (kind != 0x2C) return false;
    if (at + 9 > gif->size()) return false;
    const unsigned char flags = byte(at + 8);
    at += 9;
    Frame frame;
    if ((flags & 0x80) != 0) {
      frame.table = {at, size_t{1} << ((flags & 0x07) + 1)};
      at += frame.table.entries * 3;
      if (at > gif->size()) return false;
    }
    if (at >= gif->size()) return false;
    const int min_code_size = byte(at++);
    std::string data;
    if (!read_sub_blocks(*gif, &at, &data)) return false;
    std::array<bool, 256>& used =
        frame.table.entries > 0 ? frame.used : global_used;
    if (!mark_used_indices(data, min_code_size, &used)) return false;
    if (frame.table.entries > 0) frames.push_back(frame);
  }
  // Transparent indices count against every table: which frame a control
  // extension governs is not tracked, and keeping an entry is always safe.
  for (Frame& frame : frames) {
    for (size_t i = 0; i < 256; i++) frame.used[i] = frame.used[i] || global_used[i];
    clear_unused(gif, frame.table, frame.used);
  }
  if (global.entries > 0) clear_unused(gif, global, global_used);
  return true;
}

}  // namespace grlibre
