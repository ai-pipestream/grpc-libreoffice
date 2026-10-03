// Process-supervision tests for run_worker over /bin/sh stub workers: exit
// code mapping, stdin delivery, frame flow, the deadline kill, consumer
// aborts, and oversized frames. No LibreOffice involved; the real-worker
// integration lives in worker_render_test.cpp.

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <thread>
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

// Two workers in flight at once, as the server's concurrency gate allows.
// A's upload outgrows the pipe buffer and its stub drains stdin only after
// a pause, so A's parent sits mid-copy holding its stdin write end while B
// forks; B's stub then sleeps for seconds. A pipe end that leaked into B's
// worker would keep A's worker from seeing EOF on its upload until B's
// worker exited. Close-on-exec pipes keep A finishing right after its own
// pause.
void verify_concurrent_workers_do_not_share_pipes() {
  const std::string upload(4u << 20, 'x');
  std::vector<std::string> payloads;
  grlibre::WorkerOutcome first;
  long first_ms = 0;
  std::thread runner([&] {
    const auto begin = std::chrono::steady_clock::now();
    first = run_stub("sleep 1; cat >/dev/null; printf '\\002\\000\\000\\000ok'",
                     upload, &payloads);
    first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - begin)
                   .count();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto second = run_stub("exec sleep 4", "", nullptr);
  runner.join();
  require(first.kind == grlibre::WorkerOutcome::Kind::kOk,
          "the uploading worker finishes ok, got: " + first.detail);
  require(payloads.size() == 1 && payloads[0] == "ok",
          "the uploading worker's frame arrived");
  require(second.kind == grlibre::WorkerOutcome::Kind::kOk,
          "the sleeping worker finishes ok, got: " + second.detail);
  require(first_ms < 3000,
          "the uploading worker saw EOF without waiting for its sibling, took "
              + std::to_string(first_ms) + "ms");
}

// The caller's cancellation probe is polled while the worker runs: once it
// reports true the worker is killed and the run ends as kCancelled,
// promptly rather than at the stub's own pace.
void verify_cancellation_kills_worker() {
  const auto begin = std::chrono::steady_clock::now();
  std::vector<std::string> argv = {"/bin/sh", "-c", "exec sleep 60"};
  auto outcome = grlibre::run_worker(
      argv, "", std::chrono::milliseconds(30000), 1u << 20,
      [](std::string&&) { return true; },
      [&] {
        return std::chrono::steady_clock::now() - begin
            > std::chrono::milliseconds(300);
      });
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCancelled,
          "a cancelled caller kills the worker, got: " + outcome.detail);
  require(elapsed.count() < 5000,
          "the cancellation kill happens promptly, took "
              + std::to_string(elapsed.count()) + "ms");
}

// The deadline covers the stdin copy, not just the render after it: a
// worker that never reads an upload larger than the pipe buffer must still
// die at the deadline instead of parking the parent in a blocking write.
void verify_deadline_covers_stdin_copy() {
  const std::string upload(4u << 20, 'x');
  const auto begin = std::chrono::steady_clock::now();
  auto outcome = run_stub("exec sleep 60", upload, nullptr,
                          std::chrono::milliseconds(500));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kTimeout,
          "a worker ignoring its upload times out, got: " + outcome.detail);
  require(elapsed.count() < 10000,
          "the stdin copy honours the deadline, took "
              + std::to_string(elapsed.count()) + "ms");
}

// Frames split across many reads, a zero-length frame, and a frame that
// lands in the same read as the next one's header all reassemble intact.
void verify_frames_reassemble_across_reads() {
  std::vector<std::string> payloads;
  // A 200000-byte frame (larger than one read), an empty frame, then a
  // short one; printf emits each piece in its own write.
  auto outcome = run_stub(
      "printf '\\100\\015\\003\\000'; head -c 200000 /dev/zero | tr '\\0' a; "
      "printf '\\000\\000\\000\\000\\002\\000\\000\\000ok'",
      "", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "split frames stream ok, got: " + outcome.detail);
  require(payloads.size() == 3, "three frames arrived, got "
                                    + std::to_string(payloads.size()));
  require(payloads[0] == std::string(200000, 'a'),
          "the large frame reassembled intact");
  require(payloads[1].empty(), "the empty frame arrived empty");
  require(payloads[2] == "ok", "the short frame arrived intact");
}

}  // namespace

int main() {
  // The server ignores SIGPIPE; the runner under test assumes the same, so
  // an upload raced against a dead stub must not kill this test.
  ::signal(SIGPIPE, SIG_IGN);
  // Every case here finishes in seconds; a runner that wedges (a blocking
  // write the deadline cannot interrupt) fails the test instead of hanging
  // it.
  ::alarm(120);
  verify_ok_stream_echoes_stdin();
  verify_exit_code_mapping();
  verify_deadline_kill_is_timeout();
  verify_consumer_abort_kills_worker();
  verify_oversized_frame_is_crash();
  verify_torn_frame_is_crash();
  verify_signal_death_names_signal();
  verify_exec_failure_is_crash();
  verify_concurrent_workers_do_not_share_pipes();
  verify_cancellation_kills_worker();
  verify_deadline_covers_stdin_copy();
  verify_frames_reassemble_across_reads();
  std::println("worker-runner-test passed");
  return 0;
}
