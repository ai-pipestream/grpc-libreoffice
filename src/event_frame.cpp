#include "event_frame.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace grlibre {

namespace {

bool write_all(int fd, const void* data, size_t size) {
  const char* cursor = static_cast<const char*>(data);
  while (size > 0) {
    ssize_t wrote = ::write(fd, cursor, size);
    if (wrote < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    cursor += wrote;
    size -= static_cast<size_t>(wrote);
  }
  return true;
}

// Returns bytes read (0 on clean EOF at a boundary); throws on mid-buffer EOF.
size_t read_all(int fd, void* data, size_t size, bool allow_eof_at_start) {
  char* cursor = static_cast<char*>(data);
  size_t total = 0;
  while (total < size) {
    ssize_t got = ::read(fd, cursor + total, size - total);
    if (got < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error(std::string("frame read failed: ") + std::strerror(errno));
    }
    if (got == 0) {
      if (total == 0 && allow_eof_at_start) return 0;
      throw std::runtime_error("torn frame: stream ended mid-frame");
    }
    total += static_cast<size_t>(got);
  }
  return total;
}

}  // namespace

bool write_frame(int fd, const std::string& payload) {
  std::uint32_t length = static_cast<std::uint32_t>(payload.size());
  unsigned char header[4] = {
      static_cast<unsigned char>(length & 0xff),
      static_cast<unsigned char>((length >> 8) & 0xff),
      static_cast<unsigned char>((length >> 16) & 0xff),
      static_cast<unsigned char>((length >> 24) & 0xff)};
  return write_all(fd, header, 4) && write_all(fd, payload.data(), payload.size());
}

bool read_frame(int fd, std::string* payload, std::uint32_t max_bytes) {
  unsigned char header[4];
  if (read_all(fd, header, 4, /*allow_eof_at_start=*/true) == 0) return false;
  std::uint32_t length = static_cast<std::uint32_t>(header[0])
      | (static_cast<std::uint32_t>(header[1]) << 8)
      | (static_cast<std::uint32_t>(header[2]) << 16)
      | (static_cast<std::uint32_t>(header[3]) << 24);
  if (length > max_bytes) {
    throw std::runtime_error("frame exceeds " + std::to_string(max_bytes) + " bytes");
  }
  payload->resize(length);
  if (length > 0) read_all(fd, payload->data(), length, /*allow_eof_at_start=*/false);
  return true;
}

}  // namespace grlibre
