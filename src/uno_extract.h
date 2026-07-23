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

}  // namespace grlibre

#endif
