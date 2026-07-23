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
bool emit_typed_content(
    const PartSelection& parts,
    const std::function<bool(const google::protobuf::MessageLite&)>& emit_fn,
    std::vector<std::string>* warnings);

}  // namespace grlibre

#endif
