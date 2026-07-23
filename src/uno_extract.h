#ifndef GRLIBRE_UNO_EXTRACT_H
#define GRLIBRE_UNO_EXTRACT_H

#include <functional>
#include <string>
#include <vector>

namespace google {
namespace protobuf {
class MessageLite;
}
}  // namespace google

namespace grlibre {

// Emits typed content events (DocumentMetadata, Paragraph, TableData,
// EmbeddedImage) for the document currently loaded in this process's office
// core, by attaching to the same in-process UNO model LibreOfficeKit loaded.
// Metadata is emitted for every document type; paragraphs, tables, and
// images only for text documents. Each event is handed to emit_fn the moment
// it is extracted.
//
// Error policy: pages have already streamed when this runs, so extraction
// problems never fail the render. Instead every problem is appended to
// warnings with enough context to locate it, and mirrored to stderr so it
// lands in the server log. Returns false only when emit_fn itself fails,
// which means the parent is gone.
bool emit_typed_content(
    const std::function<bool(const google::protobuf::MessageLite&)>& emit_fn,
    std::vector<std::string>* warnings);

}  // namespace grlibre

#endif
