#pragma once

#include <string>

namespace grlibre {

// Zeroes every colour-table entry of a GIF that no frame's pixels use.
//
// The office core's GIF writer fills a frame's colour table from the
// bitmap's palette, whose entries past the colours the picture actually has
// are never initialised: they carry whatever the heap held, so the same
// picture re-encodes to different bytes on every parse and ships stray
// process memory to the client. Pixels never index those entries, so
// clearing them changes nothing a decoder shows.
//
// Returns false, leaving the bytes as they were, when the stream is not a
// GIF this reader can walk to the end (truncated, malformed LZW data).
bool scrub_unused_gif_palette(std::string* gif);

}  // namespace grlibre
