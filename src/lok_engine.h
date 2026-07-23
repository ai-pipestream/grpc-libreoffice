#pragma once

#include <cstdint>
#include <string>

namespace grlibre {

// Which document parts the caller selected, decoded from the worker's argv
// parts token: "all" selects everything (the wire default), otherwise a
// comma-joined list of ai.pipestream.office.v1.DocumentPart numeric values.
struct PartSelection {
  // True when every part is selected.
  bool all = true;
  // Bitmask keyed by DocumentPart value; bit 0 is never set because
  // DOCUMENT_PART_UNSPECIFIED entries are ignored.
  std::uint32_t mask = 0;

  // Whether the caller asked for the given DocumentPart value.
  bool wants(int part) const {
    return all || (part > 0 && part < 32 && (mask & (1u << part)) != 0);
  }
};

// Everything one worker process needs to render one document.
struct RenderOptions {
  // "pages" emits StreamPagesResponse frames; "pdf" emits ConvertToPdfResponse
  // frames.
  std::string mode;
  // Canonical source extension; also the loaded file's extension.
  std::string extension;
  // Absolute path of the document to load.
  std::string doc_path;
  // Writable per-worker directory (PDF output, office user profile).
  std::string work_dir;
  // LibreOffice installation program directory.
  std::string install_path;
  // Requested render DPI for pages mode.
  int dpi = 144;
  // Per-side pixel bound; a page is downscaled to fit.
  int max_side_px = 4096;
  // Uploaded byte count, echoed into RenderStatus.
  long input_bytes = 0;
  // Which parts to emit; defaults to every part.
  PartSelection parts;
};

// Worker process exit codes, mapped to gRPC status codes by the parent.
inline constexpr int kExitOk = 0;
inline constexpr int kExitLoadFailure = 4;
inline constexpr int kExitRenderFailure = 5;

// Loads the document through LibreOfficeKit and writes framed response
// events to out_fd. Returns a worker exit code; on failure *error names the
// step that failed.
int run_render(const RenderOptions& options, int out_fd, std::string* error);

}  // namespace grlibre
