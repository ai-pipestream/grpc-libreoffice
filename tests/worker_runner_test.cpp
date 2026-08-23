// Process-supervision tests for run_worker over /bin/sh stub workers: exit
// code mapping, stdin delivery, frame flow, the deadline kill, consumer
// aborts, and oversized frames. No LibreOffice involved; the real-worker
// integration lives in worker_render_test.cpp.

#include <signal.h>

#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

#include "lok_engine.h"
#include "worker_runner.h"

namespace {

void require(bool condition, const std::string& what) {
  if (!condition) {
    std::println(stderr, "FAIL: {}", what);
    std::exit(1);
  }
}

// Long-lived stub tails use "exec sleep" so the runner's SIGKILL hits the
// sleeper itself: a forked sleep would survive as an orphan holding the
// test's stderr open, and ctest waits for that pipe's EOF.
grlibre::WorkerOutcome run_stub(const std::string& script,
                                const std::string& stdin_bytes,
                                std::vector<std::string>* payloads,
                                std::chrono::milliseconds deadline =
                                    std::chrono::milliseconds(30000),
                                std::uint32_t max_frame = 1u << 20,
                                bool abort_on_frame = false) {
  std::vector<std::string> argv = {"/bin/sh", "-c", script};
  return grlibre::run_worker(
      argv, stdin_bytes, deadline, max_frame, [&](std::string&& payload) {
        if (payloads != nullptr) payloads->push_back(std::move(payload));
        return !abort_on_frame;
      });
}

// The happy path: the stub consumes stdin, echoes it back as one frame,
// and exits 0. Proves stdin delivery, frame framing, and the kOk mapping.
void verify_ok_stream_echoes_stdin() {
  std::vector<std::string> payloads;
  auto outcome = run_stub(
      "data=$(cat); printf '\\005\\000\\000\\000%s' \"$data\"", "hello",
      &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "echo stub exits ok, got detail: " + outcome.detail);
  require(payloads.size() == 1, "exactly one frame arrived");
  require(payloads[0] == "hello", "the frame carries the stdin bytes");
}

// Every documented worker exit code maps to its outcome kind; an unknown
// nonzero code is a crash naming the code.
void verify_exit_code_mapping() {
  struct Case {
    int code;
    grlibre::WorkerOutcome::Kind kind;
  };
  const std::vector<Case> cases = {
      {grlibre::kExitLoadFailure, grlibre::WorkerOutcome::Kind::kLoadFailure},
      {grlibre::kExitRepairNeedsOptIn,
       grlibre::WorkerOutcome::Kind::kRepairNeedsOptIn},
      {grlibre::kExitRepairUnimplemented,
       grlibre::WorkerOutcome::Kind::kRepairUnimplemented},
      {grlibre::kExitWorkDirNotTmpfs,
       grlibre::WorkerOutcome::Kind::kWorkDirNotTmpfs},
      {9, grlibre::WorkerOutcome::Kind::kCrash},
  };
  for (const auto& [code, kind] : cases) {
    auto outcome = run_stub("cat >/dev/null; exit " + std::to_string(code),
                            "ignored", nullptr);
    require(outcome.kind == kind,
            "exit " + std::to_string(code) + " maps to its outcome kind, got "
                + outcome.detail);
  }
  auto unknown = run_stub("cat >/dev/null; exit 9", "ignored", nullptr);
  require(unknown.detail.contains("worker exited with code 9"),
          "an unknown exit code lands in the crash detail");
}

// A worker that produces nothing past the deadline is killed and reported
// as a timeout, promptly rather than at the stub's own pace.
void verify_deadline_kill_is_timeout() {
  const auto begin = std::chrono::steady_clock::now();
  auto outcome = run_stub("exec sleep 60", "", nullptr,
                          std::chrono::milliseconds(300));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kTimeout,
          "deadline elapse is a timeout, got: " + outcome.detail);
  require(elapsed.count() < 10000,
          "the kill happens near the deadline, took "
              + std::to_string(elapsed.count()) + "ms");
}

// A consumer refusing a frame kills the worker and reports the abort; the
// stub would otherwise live for a minute.
void verify_consumer_abort_kills_worker() {
  const auto begin = std::chrono::steady_clock::now();
  auto outcome = run_stub("printf '\\005\\000\\000\\000hello'; exec sleep 60", "",
                          nullptr, std::chrono::milliseconds(30000), 1u << 20,
                          /*abort_on_frame=*/true);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kAborted,
          "a refused frame is an abort, got: " + outcome.detail);
  require(elapsed.count() < 10000, "the abort kills the worker promptly");
}

// A frame header above the bound is a protocol violation: the worker dies
// and the outcome names the bound.
void verify_oversized_frame_is_crash() {
  // Length 256 against a 16-byte bound.
  auto outcome = run_stub("printf '\\000\\001\\000\\000'; exec sleep 60", "",
                          nullptr, std::chrono::milliseconds(30000), 16);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "an oversized frame is a crash, got: " + outcome.detail);
  require(outcome.detail.contains("frame exceeds"),
          "the crash detail names the frame bound, got: " + outcome.detail);
}

// A header with no payload behind it is a torn frame, not a clean EOF.
void verify_torn_frame_is_crash() {
  auto outcome = run_stub("printf '\\005\\000\\000\\000he'", "", nullptr);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "a torn frame is a crash, got: " + outcome.detail);
  require(outcome.detail.contains("torn frame"),
          "the crash detail names the torn frame, got: " + outcome.detail);
}

// Signal death maps to a crash naming the signal.
void verify_signal_death_names_signal() {
  auto outcome = run_stub("cat >/dev/null; kill -9 $$", "ignored", nullptr);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "signal death is a crash, got: " + outcome.detail);
  require(outcome.detail.contains("signal"),
          "the crash detail names the signal, got: " + outcome.detail);
}

// An unrunnable worker path surfaces as the exec-failure exit code, not a
// hang or a false ok.
void verify_exec_failure_is_crash() {
  std::vector<std::string> argv = {"/nonexistent/grlibre-worker"};
  auto outcome = grlibre::run_worker(argv, "", std::chrono::milliseconds(30000),
                                     1u << 20,
                                     [](std::string&&) { return true; });
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "an unrunnable worker is a crash, got: " + outcome.detail);
  require(outcome.detail.contains(
              std::to_string(grlibre::kExitRenderFailure)),
          "the crash names the exec-failure exit code, got: "
              + outcome.detail);
}

}  // namespace

int main() {
  // The server ignores SIGPIPE; the runner under test assumes the same, so
  // an upload raced against a dead stub must not kill this test.
  ::signal(SIGPIPE, SIG_IGN);
  verify_ok_stream_echoes_stdin();
  verify_exit_code_mapping();
  verify_deadline_kill_is_timeout();
  verify_consumer_abort_kills_worker();
  verify_oversized_frame_is_crash();
  verify_torn_frame_is_crash();
  verify_signal_death_names_signal();
  verify_exec_failure_is_crash();
  std::println("worker-runner-test passed");
  return 0;
}
