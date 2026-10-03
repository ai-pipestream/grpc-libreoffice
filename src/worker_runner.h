#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace grlibre {

// Outcome of one worker process run.
struct WorkerOutcome {
  enum class Kind {
    kOk,                    // exit 0, all frames delivered
    kLoadFailure,           // office core could not load the document
    kRepairNeedsOptIn,      // broken package; repair not opted into
    kRepairUnimplemented,   // older worker exit 7; current workers do not emit this
    kWorkDirNotTmpfs,       // deployment error: work dir is not tmpfs
    kTimeout,               // deadline elapsed; worker was killed
    kAborted,               // the frame consumer declined further frames
    kCancelled,             // the caller went away; worker was killed
    kCrash,                 // any other exit, including signals
  };
  Kind kind = Kind::kCrash;
  std::string detail;
};

// Spawns argv[0] with the given arguments, streams stdin_bytes to it, and
// delivers each framed stdout payload to on_frame (return false to abort).
// The deadline covers the whole run, the stdin copy included; on expiry the
// worker is killed. cancelled, when set, is polled while the worker runs;
// once it reports true the worker is killed and the run ends as kCancelled.
WorkerOutcome run_worker(const std::vector<std::string>& argv,
                         const std::string& stdin_bytes,
                         std::chrono::milliseconds deadline,
                         std::uint32_t max_frame_bytes,
                         const std::function<bool(std::string&&)>& on_frame,
                         const std::function<bool()>& cancelled = {});

}  // namespace grlibre
