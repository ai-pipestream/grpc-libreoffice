#ifndef GRLIBRE_UNO_EXTRACT_H
#define GRLIBRE_UNO_EXTRACT_H

#include <functional>
#include <string>
#include <vector>

#include "lok_engine.h"

namespace google {
namespace protobuf {
class MessageLite;
}
}  // namespace google

namespace grlibre {

// One page rectangle in document-absolute twips, used to resolve which page
// a measured line rectangle sits on.
struct PageBox {
  // Left edge of the page in document twips.
  long x = 0;
  // Top edge of the page in document twips.
  long y = 0;
  // Page width in twips.
  long width = 0;
  // Page height in twips.
  long height = 0;
};

// The live text-selection measurement channel for per-line rectangles. The
// engine registers a LibreOfficeKit callback that copies every
// LOK_CALLBACK_TEXT_SELECTION payload into last_payload, and provides a
// synchronous flush that drains the queued callback events. Extraction
// selects a text range, flushes, and parses last_payload into LineBox
// rectangles. A null probe (or a null flush) disables measurement.
struct SelectionProbe {
  // The most recent text-selection payload: "x, y, w, h; x, y, w, h; ..."
  // in document twips, one rectangle per laid-out line.
  std::string last_payload;
  // Processes one pending event and reports whether it did; resolved from
  // the office core at runtime. Extraction pumps it in a bounded loop, so a
  // self-rescheduling idle job in the office core cannot spin it forever.
  bool (*reschedule)(bool all_events) = nullptr;
  // Acquires the office core's solar mutex n times; the event pump requires
  // it held, exactly as every LibreOfficeKit entry point holds it.
  void (*acquire_solar_mutex)(unsigned int count) = nullptr;
  // Releases every held solar mutex lock and returns how many were held.
  unsigned int (*release_solar_mutex)() = nullptr;
  // The page rectangles of the laid-out document, for page resolution.
  std::vector<PageBox> pages;

  // Drains the pending event queue with a hard bound. The selection
  // callback rides a posted user event, so a few iterations deliver it;
  // the cap only guards against busy idle jobs that requeue themselves.
  void flush() {
    acquire_solar_mutex(1);
    for (int i = 0; i < 100; i++) {
      if (!reschedule(false)) break;
    }
    release_solar_mutex();
  }
};

// Emits typed content events (DocumentMetadata, Paragraph, TableData,
// EmbeddedImage, DrawingShape, and the rest) for the document currently
// loaded in this process's office core, by attaching to the same in-process
// UNO model LibreOfficeKit loaded. Metadata is emitted for every document
// type; the other events depend on the document class. Each event is handed
// to emit_fn the moment it is extracted. parts selects which event classes
// are emitted; the extraction work behind an unselected part is skipped, not
// just its emission.
//
// Error policy: pages have already streamed when this runs, so extraction
// problems never fail the render. Instead every problem is appended to
// warnings with enough context to locate it, and mirrored to stderr so it
// lands in the server log. Returns false only when emit_fn itself fails,
// which means the parent is gone.
// probe, when non-null, enables per-line rectangle measurement for the
// LINE_RECTS part; pass null when the part is unselected or the flush
// primitive is unavailable.
bool emit_typed_content(
    const PartSelection& parts, SelectionProbe* probe,
    const std::function<bool(const google::protobuf::MessageLite&)>& emit_fn,
    std::vector<std::string>* warnings);

// Exports the document currently loaded in this process's office core to
// PDF through an in-memory output stream: no PDF ever exists as a service
// file or as one whole buffer. filter_name selects the export filter
// (writer_pdf_Export and friends, keyed on the document class by the
// caller). Bytes are handed to emit_chunk in order, at most chunk_limit per
// call; the last chunk flushes on the filter's completion signal. On
// success *total_bytes is the exported PDF size. On failure returns false
// with *error set; no bytes reach emit_chunk before an export failure, so
// there is no partial output to clean up.
//
// One LibreOffice-internal temp file remains: the pdf filter renders into a
// named temp file (vcl's PDF writer is file-backed, structural) and copies
// it to the output stream, unlinking it right after. It lives under the
// worker's TMPDIR, which the worker pins inside the tmpfs work dir.
// pdf.first_page / last_page become FilterData PageRange when set.
struct PdfExportOptions {
  // First page, 1-based inclusive; 0 means from the start.
  int first_page = 0;
  // Last page, 1-based inclusive; 0 means through the end.
  int last_page = 0;
  // Pins ExportHiddenSlides=false in FilterData. The impress pdf filter
  // omits hidden slides by default, but that default is installation
  // configuration; only an explicit FilterData entry guarantees it. Calc
  // needs no equivalent: its pdf filter never exports hidden sheets.
  bool skip_hidden = false;
};

bool export_pdf_stream(const std::string& filter_name, size_t chunk_limit,
                       const std::function<bool(std::string&&)>& emit_chunk,
                       long* total_bytes, std::string* error,
                       const PdfExportOptions& pdf = {});

// Whether these document bytes are a broken ZIP package the office core
// could only open through its repair path. Runs the same ZipPackage probe
// LibreOffice's type detection runs before offering repair: a plain open
// throws for a broken ZIP, and a RepairPackage-mode reopen that yields
// content proves repairability. False for healthy packages, for non-ZIP
// bytes, and for damage beyond repair.
bool is_repairable_broken_package(const std::string& bytes);

// The name file-name fields print: the upload's own name. The worker loads
// one document per process, so it is set once before extraction.
void set_source_name(const std::string& name);

// Applies tracked-change display and form fills to the document currently
// loaded in this process. Problems append to warnings and never fail the
// render.
void apply_document_options(const RenderOptions& options,
                            std::vector<std::string>* warnings);

// Outcome of applying a request's redaction to the loaded document.
struct RedactionResult {
  // False when the redaction was refused; nothing may then be painted,
  // exported, or extracted.
  bool ok = true;
  // Why the redaction was refused, for the caller's status. It names
  // regions and offsets, never document text.
  std::string refusal;
  // The redacted strings, UTF-8, for guarding everything emitted later.
  std::vector<std::string> redacted;
};

// Redacts the request's spans in the model of the document currently
// loaded in this process, before anything is laid out, painted, exported,
// or extracted. Each span resolves, through the same portion walk the
// Paragraph events come from, to the text it covers in the annotation text
// space (split per paragraph and trimmed of surrounding whitespace). That
// text, and every other occurrence of it anywhere in the document, is
// replaced with U+2588 FULL BLOCK glyphs, one per code point, drawn black
// on black: body, tables, headers and footers of every page style,
// footnotes and endnotes, text frames, drawing shapes and alt text,
// comments, fields (turned into literal text), content controls, hyperlink
// targets, index entries, user-defined style names, and the document
// properties; tracked-change recording is switched off first so nothing
// survives as a deletion, and generated indexes are rebuilt from the
// rewritten headings. The model is then walked again, and the document is
// extracted once more the way the typed events would carry it. The
// redaction is refused, fail closed, when a span does not resolve (only
// text documents carry an annotation text space), when the document embeds
// an object the service cannot inspect or one that carries the text, or
// when the text survives anywhere those checks can see. Matching is exact
// and case-sensitive.
RedactionResult apply_redaction(const RenderOptions& options,
                                std::vector<std::string>* warnings);

// True when any string of message, or the joined text of any of its run
// lists, carries one of the redacted strings; *where names the field.
bool carries_redacted_text(const google::protobuf::MessageLite& message,
                           const std::vector<std::string>& redacted,
                           std::string* where);

// text with every occurrence of a redacted string replaced by redaction
// glyphs.
std::string mask_redacted(const std::string& text,
                          const std::vector<std::string>& redacted);

// Per-part visibility and used-range size for spreadsheet/presentation
// page filtering. Index matches LibreOfficeKit part ordinal.
struct PartLayout {
  // True when the sheet or slide is shown.
  bool visible = true;
  // Left edge of the used range in twips (visible columns before it);
  // 0 when unknown or not a sheet.
  long used_x = 0;
  // Top edge of the used range in twips (visible rows above it); 0 when
  // unknown or not a sheet.
  long used_y = 0;
  // Used-range width in twips, visible columns only; 0 when unknown or
  // not a sheet.
  long used_width = 0;
  // Used-range height in twips, visible rows only; 0 when unknown or not
  // a sheet.
  long used_height = 0;
};

// Fills one PartLayout per sheet or slide of the loaded document. Writer
// documents leave *parts empty. Problems append to warnings.
void describe_parts(std::vector<PartLayout>* parts,
                    std::vector<std::string>* warnings);

// Names the page style in force on each page of the loaded document, in
// page order, reading the laid-out document the page images are painted
// from. A text document is walked with the view cursor's page cursor, one
// jump per page, reading the cursor's own PageStyleName: that is the style
// the layout put on that page, not the style a header block belongs to. A
// spreadsheet names each sheet's page style and a presentation or drawing
// each page's master, both indexed by part ordinal, so the caller indexes
// those by part rather than by emitted page. Entries are empty where the
// office core names nothing. Problems append to warnings and leave the
// remaining pages unnamed rather than failing the render.
void describe_page_styles(std::vector<std::string>* styles,
                          std::vector<std::string>* warnings);

// Exports one page of the loaded document as SVG through the UNO graphic
// export filter. page_number is 1-based. Returns empty on failure, silently:
// document classes without an SVG store filter fail on every page, and the
// caller's raster fallback is the designed handling.
std::string export_page_svg_uno(int page_number);

}  // namespace grlibre

#endif
