#pragma once

#include <cstdint>
#include <string>

namespace grlibre {

// Length-prefixed framing for the worker's event stream: a 32-bit
// little-endian payload length followed by the payload. The parent reads
// frames off the worker's stdout; the worker writes them.

// Writes one frame; loops over partial writes. Returns false on error.
bool write_frame(int fd, const std::string& payload);

// Reads one frame; loops over partial reads. Returns false on EOF before a
// frame starts, throws std::runtime_error on a torn frame or oversized
// payload (> max_bytes).
bool read_frame(int fd, std::string* payload, std::uint32_t max_bytes);

}  // namespace grlibre
