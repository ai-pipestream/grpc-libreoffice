#include "event_frame.h"

#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}

void verify_roundtrip() {
  int fds[2];
  require(::pipe(fds) == 0, "pipe");
  // The writer runs on its own thread, as the worker does in production: a
  // frame larger than the pipe buffer must not deadlock the pair.
  std::string big(300000, 'x');
  std::thread writer([&, write_fd = fds[1]] {
    require(grlibre::write_frame(write_fd, "hello"), "write frame");
    require(grlibre::write_frame(write_fd, ""), "write empty frame");
    require(grlibre::write_frame(write_fd, big), "write big frame");
    ::close(write_fd);
  });
  std::string payload;
  require(grlibre::read_frame(fds[0], &payload, 1 << 20), "read frame");
  require(payload == "hello", "payload matches");
  require(grlibre::read_frame(fds[0], &payload, 1 << 20), "read empty frame");
  require(payload.empty(), "empty payload matches");
  require(grlibre::read_frame(fds[0], &payload, 1 << 20), "read big frame");
  require(payload == big, "big payload matches");
  require(!grlibre::read_frame(fds[0], &payload, 1 << 20), "clean EOF");
  writer.join();
  ::close(fds[0]);
}

void verify_torn_frame_throws() {
  int fds[2];
  require(::pipe(fds) == 0, "pipe");
  unsigned char header[4] = {10, 0, 0, 0};
  require(::write(fds[1], header, 4) == 4, "partial header write");
  require(::write(fds[1], "abc", 3) == 3, "partial payload write");
  ::close(fds[1]);
  std::string payload;
  bool threw = false;
  try {
    grlibre::read_frame(fds[0], &payload, 1 << 20);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require(threw, "torn frame throws");
  ::close(fds[0]);
}

void verify_oversized_frame_throws() {
  int fds[2];
  require(::pipe(fds) == 0, "pipe");
  require(grlibre::write_frame(fds[1], "0123456789"), "write frame");
  ::close(fds[1]);
  std::string payload;
  bool threw = false;
  try {
    grlibre::read_frame(fds[0], &payload, 4);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require(threw, "oversized frame throws");
  ::close(fds[0]);
}

}  // namespace

int main() {
  verify_roundtrip();
  verify_torn_frame_throws();
  verify_oversized_frame_throws();
  std::cout << "event-frame-test passed\n";
  return 0;
}
