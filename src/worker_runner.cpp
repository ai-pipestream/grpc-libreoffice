#include "worker_runner.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "event_frame.h"
#include "lok_engine.h"

namespace grlibre {

namespace {

void write_input(int fd, const std::string& bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t wrote = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (wrote < 0) {
      if (errno == EINTR) continue;
      // EPIPE: the worker died before consuming the upload. The exit status
      // tells the real story; stop feeding.
      break;
    }
    offset += static_cast<size_t>(wrote);
  }
}

WorkerOutcome finish(pid_t pid, bool kill_first, WorkerOutcome::Kind kind_on_exit_ok,
                     const std::string& detail) {
  if (kill_first) ::kill(pid, SIGKILL);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  WorkerOutcome outcome;
  outcome.detail = detail;
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
                         const std::function<bool(std::string&&)>& on_frame) {
  int to_child[2];
  int from_child[2];
  if (::pipe(to_child) != 0 || ::pipe(from_child) != 0) {
    return {WorkerOutcome::Kind::kCrash, "pipe creation failed"};
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    return {WorkerOutcome::Kind::kCrash, "fork failed"};
  }
  if (pid == 0) {
    ::dup2(to_child[0], STDIN_FILENO);
    ::dup2(from_child[1], STDOUT_FILENO);
    ::close(to_child[0]);
    ::close(to_child[1]);
    ::close(from_child[0]);
    ::close(from_child[1]);
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    ::execv(args[0], args.data());
    ::_exit(kExitRenderFailure);
  }

  ::close(to_child[0]);
  ::close(from_child[1]);

  write_input(to_child[1], stdin_bytes);
  ::close(to_child[1]);

  auto end_time = std::chrono::steady_clock::now() + deadline;
  try {
    for (;;) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - std::chrono::steady_clock::now());
      if (remaining.count() <= 0) {
        ::close(from_child[0]);
        return finish(pid, true, WorkerOutcome::Kind::kTimeout, "worker deadline elapsed");
      }
      struct pollfd waiter = {from_child[0], POLLIN, 0};
      int ready = ::poll(&waiter, 1, static_cast<int>(remaining.count()));
      if (ready < 0) {
        if (errno == EINTR) continue;
        ::close(from_child[0]);
        return finish(pid, true, WorkerOutcome::Kind::kCrash, "poll failed");
      }
      if (ready == 0) {
        ::close(from_child[0]);
        return finish(pid, true, WorkerOutcome::Kind::kTimeout, "worker deadline elapsed");
      }
      std::string payload;
      if (!read_frame(from_child[0], &payload, max_frame_bytes)) break;
      if (!on_frame(std::move(payload))) {
        ::close(from_child[0]);
        return finish(pid, true, WorkerOutcome::Kind::kAborted, "consumer aborted");
      }
    }
  } catch (const std::exception& torn) {
    ::close(from_child[0]);
    return finish(pid, true, WorkerOutcome::Kind::kCrash, torn.what());
  }
  ::close(from_child[0]);
  return finish(pid, false, WorkerOutcome::Kind::kOk, "");
}

}  // namespace grlibre
