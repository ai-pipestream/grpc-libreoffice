#include "worker_runner.h"

#include <fcntl.h>
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
                         const std::function<bool(std::string&&)>& on_frame) {
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
      struct pollfd waiter{.fd = from_child[0], .events = POLLIN, .revents = 0};
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
