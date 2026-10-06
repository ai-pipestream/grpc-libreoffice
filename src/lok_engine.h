#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "png_encode.h"

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

  // Whether the caller listed the given DocumentPart value explicitly. The
  // "all" default does not count: parts whose cost must be opted into
  // (DOCUMENT_PART_CELL_LINE_RECTS) gate on this instead of wants().
  bool explicit_wants(int part) const {
    return part > 0 && part < 32 && (mask & (1u << part)) != 0;
  }
};

// Everything one worker process needs to render one document.
struct RenderOptions {
  // "pages" emits StreamPagesResponse frames; "pdf" emits ConvertToPdfResponse
  // frames.
  std::string mode;
  // Canonical source extension; also the loaded file's extension.
  std::string extension;
  // Absolute path of the document to load, inside work_dir. The engine
  // unlinks it as soon as the load completes.
  std::string doc_path;
  // Writable per-worker directory on tmpfs (document staging, office user
  // profile, the core's TMPDIR spills). Uploaded bytes never exist outside
  // RAM; the produced PDF streams from the export filter and is never
  // staged here.
  std::string work_dir;
  // LibreOffice installation program directory.
  std::string install_path;
  // Requested render DPI for pages mode.
  int dpi = 144;
  // Per-side pixel bound; a page is downscaled to fit.
  int max_side_px = 4096;
  // 1-based inclusive page range for pages mode; 0 means unbounded on that
  // side. Pages outside the range are never painted. Emitted PageImage
  // indexes stay document-absolute.
  int first_page = 0;
  int last_page = 0;
  // Page image encoding and lossy quality for pages mode. Quality is
  // 1..100 and ignored for PNG.
  ImageFormat image_format = ImageFormat::kPng;
  int image_quality = 85;
  // Fit-to-width in pixels; 0 means use dpi. Still clamped by max_side_px.
  int max_width_px = 0;
  // The upload's file name, without directories, as the caller sent it;
  // empty when it sent none. File-name fields print it in place of the
  // work-dir copy's path.
  std::string source_name;
  // Convert page rasters to grayscale before encoding.
  bool grayscale = false;
  // TrackedChangeDisplay wire value; 0 means leave the document as stored.
  int tracked_changes = 0;
  // PageVectorFormat wire value; 0/1 means raster, 2 means SVG.
  int vector_format = 0;
  // Omit hidden sheets and hidden slides from page images.
  bool skip_hidden = false;
  // Crop spreadsheet page images to the used cell range.
  bool paint_used_range = false;
  // Append each slide's notes page as an extra page image.
  bool include_notes_pages = false;
  // Form field writes applied after load, before paint/export.
  std::vector<std::pair<std::string, std::string>> form_values;
  // Annotation-space spans whose text is redacted from the document model
  // before paint, export, or extraction (see apply_redaction).
  std::vector<std::pair<std::int64_t, std::int64_t>> redact_spans;
  // Uploaded byte count, echoed into RenderStatus.
  long input_bytes = 0;
  // Which parts to emit; defaults to every part.
  PartSelection parts;
  // Caller opt-in for the office core's broken-package repair path, the one
  // document path that stages a temp copy of the document. Off by default;
  // a repair-needing document then fails naming the opt-in.
  bool allow_package_repair = false;
};

// Worker process exit codes, mapped to gRPC status codes by the parent.
inline constexpr int kExitOk = 0;
inline constexpr int kExitLoadFailure = 4;
inline constexpr int kExitRenderFailure = 5;
// The package is broken but repairable, and the caller did not opt into the
// rewriting repair path.
inline constexpr int kExitRepairNeedsOptIn = 6;
// Exit code 7 once meant "repair unimplemented"; the worker now retries
// with RepairPackage=true and reports a load failure if the package still
// will not open, and the server and worker always ship together.
// The work dir handed to the worker is not on tmpfs; the worker refuses to
// stage the upload rather than write document bytes to disk.
inline constexpr int kExitWorkDirNotTmpfs = 8;
// The requested redaction could not be applied to the whole document; the
// worker emitted nothing that could carry the text, and leaves the reason
// in kRefusalFile under its work dir.
inline constexpr int kExitRedactionRefused = 9;
// Where a refusing worker leaves its human-readable reason, relative to
// the work dir, for the parent to put in the call's status.
inline constexpr char kRefusalFile[] = "refusal.txt";

// Loads the document through LibreOfficeKit and writes framed response
// events to out_fd. Returns a worker exit code; on failure *error names the
// step that failed.
int run_render(const RenderOptions& options, int out_fd, std::string* error);

}  // namespace grlibre
