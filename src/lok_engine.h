#pragma once

#include <string>

namespace grlibre {

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
