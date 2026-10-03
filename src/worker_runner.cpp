#include "worker_runner.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "lok_engine.h"

namespace grlibre {

namespace {

// How often a running worker's caller is asked whether it went away.
constexpr std::chrono::milliseconds kCancelPoll{100};

// Reassembles the worker's length-prefixed frames (event_frame.h) from
// whatever each nonblocking read delivers, so the parent never blocks
// inside a frame: the deadline and the cancellation probe keep applying
// while a large page streams in.
class FrameReader {
 public:
  explicit FrameReader(std::uint32_t max_bytes) : max_bytes_(max_bytes) {}

  // Consumes size bytes, handing each completed payload to deliver.
  // Returns false as soon as deliver declines one; throws
  // std::runtime_error on a frame header above the bound.
  bool feed(const char* data, size_t size,
            const std::function<bool(std::string&&)>& deliver) {
    while (size > 0) {
      if (header_filled_ < sizeof header_) {
        size_t take = std::min(size, sizeof header_ - header_filled_);
        std::memcpy(header_ + header_filled_, data, take);
        header_filled_ += take;
        data += take;
        size -= take;
        if (header_filled_ < sizeof header_) return true;
        std::uint32_t length = static_cast<std::uint32_t>(header_[0])
            | (static_cast<std::uint32_t>(header_[1]) << 8)
            | (static_cast<std::uint32_t>(header_[2]) << 16)
            | (static_cast<std::uint32_t>(header_[3]) << 24);
        if (length > max_bytes_) {
          throw std::runtime_error("frame exceeds " + std::to_string(max_bytes_)
                                   + " bytes");
        }
        payload_.assign(length, '\0');
        payload_filled_ = 0;
      }
      size_t take = std::min(size, payload_.size() - payload_filled_);
      std::memcpy(payload_.data() + payload_filled_, data, take);
      payload_filled_ += take;
      data += take;
      size -= take;
      if (payload_filled_ == payload_.size()) {
        header_filled_ = 0;
        if (!deliver(std::move(payload_))) return false;
        payload_ = std::string();
      }
    }
    return true;
  }

  // True when the stream stopped partway through a frame.
  bool mid_frame() const { return header_filled_ > 0; }

 private:
  std::uint32_t max_bytes_;
  unsigned char header_[4] = {0, 0, 0, 0};
  size_t header_filled_ = 0;
  std::string payload_;
  size_t payload_filled_ = 0;
};

bool set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// How long finish() waits for a worker whose stdout has closed to actually
// exit before killing it. Normally exit follows the last write within
// milliseconds (_exit right after run_render); a wedged process that closed
// its stream but lives on would otherwise hold the caller's concurrency
// slot forever.
constexpr std::chrono::milliseconds kReapGrace{2000};

WorkerOutcome finish(pid_t pid, bool kill_first, WorkerOutcome::Kind kind_on_exit_ok,
                     const std::string& detail) {
  if (kill_first) ::kill(pid, SIGKILL);
  int status = 0;
  bool reaped = false;
  const auto reap_deadline = std::chrono::steady_clock::now() + kReapGrace;
  for (;;) {
    pid_t got = ::waitpid(pid, &status, WNOHANG);
    if (got == pid) {
      reaped = true;
      break;
    }
    if (got < 0 && errno != EINTR) break;
    if (std::chrono::steady_clock::now() >= reap_deadline) break;
    ::poll(nullptr, 0, 10);
  }
  bool forced = false;
  if (!reaped) {
    forced = true;
    ::kill(pid, SIGKILL);
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  }
  WorkerOutcome outcome;
  outcome.detail = detail;
  if (forced && kind_on_exit_ok == WorkerOutcome::Kind::kOk) {
    outcome.kind = WorkerOutcome::Kind::kCrash;
    outcome.detail = "worker closed its stream but did not exit; killed "
                     "after the reap grace";
    return outcome;
  }
  if (kind_on_exit_ok != WorkerOutcome::Kind::kOk) {
    outcome.kind = kind_on_exit_ok;
    return outcome;
  }
  if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);
    if (code == kExitOk) {
      outcome.kind = WorkerOutcome::Kind::kOk;
    } else if (code == kExitLoadFailure) {
      outcome.kind = WorkerOutcome::Kind::kLoadFailure;
      outcome.detail = "the office core could not load the document";
    } else if (code == kExitRepairNeedsOptIn) {
      outcome.kind = WorkerOutcome::Kind::kRepairNeedsOptIn;
      outcome.detail = "the document package is broken; opening it needs the "
                       "office core's repair path, which rebuilds a rewritten "
                       "copy of the document and requires the "
                       "allow_package_repair opt-in";
    } else if (code == kExitRepairUnimplemented) {
      outcome.kind = WorkerOutcome::Kind::kRepairUnimplemented;
      outcome.detail = "allow_package_repair is set, but this server does not "
                       "implement the repair path; the broken package cannot "
                       "be loaded";
    } else if (code == kExitWorkDirNotTmpfs) {
      outcome.kind = WorkerOutcome::Kind::kWorkDirNotTmpfs;
      outcome.detail = "the worker's work dir is not on tmpfs; the server "
                       "refuses to stage document bytes on disk (check "
                       "GRLIBRE_TMPFS_DIR)";
    } else {
      outcome.kind = WorkerOutcome::Kind::kCrash;
      outcome.detail = "worker exited with code " + std::to_string(code);
    }
  } else if (WIFSIGNALED(status)) {
    outcome.kind = WorkerOutcome::Kind::kCrash;
    outcome.detail = std::string("worker killed by signal ")
        + std::to_string(WTERMSIG(status));
  }
  return outcome;
}

}  // namespace

WorkerOutcome run_worker(const std::vector<std::string>& argv,
                         const std::string& stdin_bytes,
                         std::chrono::milliseconds deadline,
                         std::uint32_t max_frame_bytes,
                         const std::function<bool(std::string&&)>& on_frame,
                         const std::function<bool()>& cancelled) {
  // The server runs several workers at once, each spawned from its own
  // request thread. Pipes created close-on-exec cannot leak into a sibling
  // worker forked in the window between pipe creation and this request's
  // own close: a leaked stdin write end keeps a worker from ever seeing
  // EOF on its upload, and a leaked stdout write end keeps the parent from
  // ever seeing the worker's EOF. dup2 onto 0 and 1 clears the flag on the
  // child's own copies.
  int to_child[2];
  int from_child[2];
  if (::pipe2(to_child, O_CLOEXEC) != 0) {
    return {WorkerOutcome::Kind::kCrash, "pipe creation failed"};
  }
  if (::pipe2(from_child, O_CLOEXEC) != 0) {
    ::close(to_child[0]);
    ::close(to_child[1]);
    return {WorkerOutcome::Kind::kCrash, "pipe creation failed"};
  }

  // Built before fork: the child of a multithreaded process may only make
  // async-signal-safe calls until exec, and allocation is not one of them.
  std::vector<char*> args;
  args.reserve(argv.size() + 1);
  for (const std::string& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
  args.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(to_child[0]);
    ::close(to_child[1]);
    ::close(from_child[0]);
    ::close(from_child[1]);
    return {WorkerOutcome::Kind::kCrash, "fork failed"};
  }
  if (pid == 0) {
    // dup2 onto the same descriptor makes no copy and keeps the flag, so
    // an end that already sits on its target (a parent started with a
    // closed stdin or stdout) has close-on-exec cleared directly.
    if (to_child[0] == STDIN_FILENO) {
      ::fcntl(STDIN_FILENO, F_SETFD, 0);
    } else {
      ::dup2(to_child[0], STDIN_FILENO);
    }
    if (from_child[1] == STDOUT_FILENO) {
      ::fcntl(STDOUT_FILENO, F_SETFD, 0);
    } else {
      ::dup2(from_child[1], STDOUT_FILENO);
    }
    // Nothing but stdin, stdout, and stderr crosses into the worker, even
    // a descriptor some library opened without close-on-exec.
    ::close_range(3, ~0U, 0);
    ::execv(args[0], args.data());
    ::_exit(kExitRenderFailure);
  }

  ::close(to_child[0]);
  ::close(from_child[1]);

  // One loop feeds the upload and drains the event stream at whatever pace
  // the worker sets, on nonblocking ends: neither side can wedge the other,
  // and the deadline and the caller's cancellation cover the stdin copy
  // too, not just the render after it.
  int input_fd = to_child[1];
  const int output_fd = from_child[0];
  if (!set_nonblocking(input_fd) || !set_nonblocking(output_fd)) {
    ::close(input_fd);
    ::close(output_fd);
    return finish(pid, true, WorkerOutcome::Kind::kCrash,
                  "cannot make the worker pipes nonblocking");
  }
  size_t written = 0;
  if (stdin_bytes.empty()) {
    ::close(input_fd);
    input_fd = -1;
  }
  auto stop = [&](bool kill_first, WorkerOutcome::Kind kind,
                  const std::string& detail) {
    if (input_fd >= 0) ::close(input_fd);
    ::close(output_fd);
    return finish(pid, kill_first, kind, detail);
  };

  FrameReader reader(max_frame_bytes);
  char buffer[1 << 16];
  const auto end_time = std::chrono::steady_clock::now() + deadline;
  try {
    for (;;) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - std::chrono::steady_clock::now());
      if (remaining.count() <= 0) {
        return stop(true, WorkerOutcome::Kind::kTimeout, "worker deadline elapsed");
      }
      if (cancelled && cancelled()) {
        return stop(true, WorkerOutcome::Kind::kCancelled,
                    "the caller cancelled the call");
      }
      struct pollfd waiters[2] = {
          {.fd = output_fd, .events = POLLIN, .revents = 0},
          {.fd = input_fd, .events = POLLOUT, .revents = 0}};
      const nfds_t count = input_fd >= 0 ? 2 : 1;
      auto wait = remaining;
      if (cancelled) wait = std::min(wait, kCancelPoll);
      int ready = ::poll(waiters, count, static_cast<int>(wait.count()));
      if (ready < 0) {
        if (errno == EINTR) continue;
        return stop(true, WorkerOutcome::Kind::kCrash, "poll failed");
      }
      if (ready == 0) continue;
      if (input_fd >= 0 && waiters[1].revents != 0) {
        while (written < stdin_bytes.size()) {
          ssize_t wrote = ::write(input_fd, stdin_bytes.data() + written,
                                  stdin_bytes.size() - written);
          if (wrote < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            // EPIPE: the worker died before consuming the upload. The exit
            // status tells the real story; stop feeding.
            written = stdin_bytes.size();
            break;
          }
          written += static_cast<size_t>(wrote);
        }
        if (written >= stdin_bytes.size()) {
          ::close(input_fd);
          input_fd = -1;
        }
      }
      if (waiters[0].revents == 0) continue;
      // A bounded batch per wake-up, so a worker that never pauses its
      // output still has the deadline and the probe checked between batches.
      for (int batch = 0; batch < 16; batch++) {
        ssize_t got = ::read(output_fd, buffer, sizeof buffer);
        if (got < 0) {
          if (errno == EINTR) continue;
          if (errno == EAGAIN || errno == EWOULDBLOCK) break;
          throw std::runtime_error(std::string("frame read failed: ")
                                   + std::strerror(errno));
        }
        if (got == 0) {
          if (reader.mid_frame()) {
            throw std::runtime_error("torn frame: stream ended mid-frame");
          }
          return stop(false, WorkerOutcome::Kind::kOk, "");
        }
        if (!reader.feed(buffer, static_cast<size_t>(got), on_frame)) {
          return stop(true, WorkerOutcome::Kind::kAborted, "consumer aborted");
        }
      }
    }
  } catch (const std::exception& torn) {
    return stop(true, WorkerOutcome::Kind::kCrash, torn.what());
  }
}

}  // namespace grlibre
