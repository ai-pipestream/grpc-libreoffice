#include "lok_engine.h"

// The tile-rendering surface (paintTile, parts, page rectangles) is gated
// behind this define; it is the same API Collabora Online builds on.
#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKit.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "ai/pipestream/office/v1/office_service.pb.h"
#include "event_frame.h"
#include "png_encode.h"
#include "uno_extract.h"

namespace grlibre {

namespace {

namespace officev1 = ai::pipestream::office::v1;

constexpr double kTwipsPerInch = 1440.0;
constexpr size_t kPdfChunkBytes = 256 * 1024;

struct PageRect {
  int part = 0;
  long x = 0;
  long y = 0;
  long width = 0;
  long height = 0;
  bool notes = false;
};

std::string doc_type_name(int type) {
  switch (type) {
    case LOK_DOCTYPE_TEXT: return "text";
    case LOK_DOCTYPE_SPREADSHEET: return "spreadsheet";
    case LOK_DOCTYPE_PRESENTATION: return "presentation";
    case LOK_DOCTYPE_DRAWING: return "drawing";
    default: return "other";
  }
}

// Writer reports its pages as "x, y, w, h; x, y, w, h; ..." in twips.
std::vector<PageRect> parse_page_rectangles(const char* rectangles) {
  std::vector<PageRect> pages;
  if (rectangles == nullptr) return pages;
  std::stringstream stream(rectangles);
  std::string entry;
  while (std::getline(stream, entry, ';')) {
    PageRect rect;
    if (std::sscanf(entry.c_str(), "%ld , %ld , %ld , %ld",
                    &rect.x, &rect.y, &rect.width, &rect.height) == 4
        && rect.width > 0 && rect.height > 0) {
      pages.push_back(rect);
    }
  }
  return pages;
}

bool emit(int fd, const google::protobuf::MessageLite& message) {
  std::string serialized;
  if (!message.SerializeToString(&serialized)) return false;
  return write_frame(fd, serialized);
}

// Copies every text-selection payload into the probe; all other callback
// types are ignored. Runs on the flush inside the extraction pass, never
// concurrently with it.
void selection_callback(int type, const char* payload, void* data) {
  if (type != LOK_CALLBACK_TEXT_SELECTION || data == nullptr) return;
  static_cast<SelectionProbe*>(data)->last_payload =
      payload != nullptr ? payload : "";
}

// Resolves the office core's event drain. The callback that carries
// selection rectangles is posted to the event queue, and the worker pumps
// no loop, so extraction must drain it explicitly. Application::Reschedule
// processes the currently pending events and returns; the idle-draining
// alternative never settles under a loaded Writer document, whose layout
// idle jobs reschedule themselves forever. Resolved by symbol like
// process_context resolves the service factory, avoiding a hard link
// against the VCL library.
bool (*resolve_reschedule())(bool) {
  return reinterpret_cast<bool (*)(bool)>(
      dlsym(RTLD_DEFAULT, "_ZN11Application10RescheduleEb"));
}

// Paints an opaque black rectangle onto a 32-bit pixel buffer, clamped to
// the buffer bounds; alpha is left untouched.
void blackout_rect(std::vector<unsigned char>* pixels, int width_px, int height_px,
                   int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > width_px) x1 = width_px;
  if (y1 > height_px) y1 = height_px;
  if (x1 <= x0 || y1 <= y0) return;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      unsigned char* p =
          pixels->data() + (static_cast<size_t>(y) * width_px + x) * 4;
      p[0] = 0;
      p[1] = 0;
      p[2] = 0;
    }
  }
}

std::string base64_encode(const std::string& in) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    unsigned n = (static_cast<unsigned char>(in[i]) << 16)
        | (static_cast<unsigned char>(in[i + 1]) << 8)
        | static_cast<unsigned char>(in[i + 2]);
    out.push_back(kTable[(n >> 18) & 63]);
    out.push_back(kTable[(n >> 12) & 63]);
    out.push_back(kTable[(n >> 6) & 63]);
    out.push_back(kTable[n & 63]);
    i += 3;
  }
  if (i < in.size()) {
    unsigned n = static_cast<unsigned char>(in[i]) << 16;
    if (i + 1 < in.size()) n |= static_cast<unsigned char>(in[i + 1]) << 8;
    out.push_back(kTable[(n >> 18) & 63]);
    out.push_back(kTable[(n >> 12) & 63]);
    out.push_back(i + 1 < in.size() ? kTable[(n >> 6) & 63] : '=');
    out.push_back('=');
  }
  return out;
}

std::string svg_from_png(const std::string& png, int width, int height) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\""
      + std::to_string(width) + "\" height=\"" + std::to_string(height)
      + "\" viewBox=\"0 0 " + std::to_string(width) + " "
      + std::to_string(height) + "\">\n<image width=\""
      + std::to_string(width) + "\" height=\"" + std::to_string(height)
      + "\" href=\"data:image/png;base64," + base64_encode(png)
      + "\"/>\n</svg>\n";
}

std::string export_page_svg(lok::Document* document, const PageRect& page,
                            const std::string& work_dir) {
  document->setPartMode(page.notes ? LOK_PARTMODE_NOTES : LOK_PARTMODE_SLIDES);
  document->setPart(page.part);
  const std::string path = work_dir + "/page.svg";
  const std::string url = "file://" + path;
  ::unlink(path.c_str());
  if (!document->saveAs(url.c_str(), "svg", nullptr)
      && !document->saveAs(url.c_str(), "svg:draw_svg_Export", nullptr)) {
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  in.close();
  ::unlink(path.c_str());
  return bytes;
}

// Paints every page and streams PageImage events. Two-stage pipeline: this
// thread paints page N+1 while the encoder thread compresses and emits page
// N. The queue is bounded so raw pixel buffers never pile up; the FIFO plus
// single encoder keeps emission in page order. Adds the emitted PNG bytes to
// *output_bytes; on failure sets *error and returns false.
bool paint_pages(lok::Document* document, const RenderOptions& options,
                 const std::vector<PageRect>& pages, bool bgra, int out_fd,
                 long* output_bytes, std::string* error,
                 const std::vector<RedactBox>& redact) {
  struct RawPage {
    int index;
    int width_px;
    int height_px;
    int dpi;
    std::vector<unsigned char> pixels;
  };
  std::mutex queue_mutex;
  std::condition_variable queue_changed;
  std::deque<RawPage> queue;
  bool paint_done = false;
  std::atomic<bool> encoder_ok{true};
  std::atomic<long> encoded_bytes{0};
  constexpr size_t kMaxQueued = 2;

  std::thread encoder([&] {
    for (;;) {
      RawPage raw;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_changed.wait(lock, [&] { return !queue.empty() || paint_done; });
        if (queue.empty()) return;
        raw = std::move(queue.front());
        queue.pop_front();
      }
      queue_changed.notify_one();
      std::string encoded = encode_image(raw.pixels.data(), raw.width_px,
                                         raw.height_px, bgra,
                                         options.image_format,
                                         options.image_quality);
      if (encoded.empty()) {
        encoder_ok = false;
        return;
      }
      encoded_bytes += static_cast<long>(encoded.size());
      officev1::StreamPagesResponse page_event;
      officev1::PageImage* image = page_event.mutable_page_image();
      image->set_index(raw.index);
      image->set_width_px(raw.width_px);
      image->set_height_px(raw.height_px);
      image->set_dpi(raw.dpi);
      image->set_png(std::move(encoded));
      switch (options.image_format) {
        case ImageFormat::kPng:
          image->set_format(officev1::PAGE_IMAGE_FORMAT_PNG);
          break;
        case ImageFormat::kJpeg:
          image->set_format(officev1::PAGE_IMAGE_FORMAT_JPEG);
          break;
        case ImageFormat::kWebp:
          image->set_format(officev1::PAGE_IMAGE_FORMAT_WEBP);
          break;
      }
      if (!emit(out_fd, page_event)) {
        encoder_ok = false;
        return;
      }
    }
  });

  for (size_t index = 0; encoder_ok && index < pages.size(); index++) {
    int page_number = static_cast<int>(index) + 1;
    if (options.first_page > 0 && page_number < options.first_page) continue;
    if (options.last_page > 0 && page_number > options.last_page) continue;
    const PageRect& page = pages[index];
    document->setPartMode(page.notes ? LOK_PARTMODE_NOTES : LOK_PARTMODE_SLIDES);
    document->setPart(page.part);
    if (options.vector_format == officev1::PAGE_VECTOR_FORMAT_SVG) {
      std::string svg = export_page_svg(document, page, options.work_dir);
      if (svg.empty()) {
        svg = export_page_svg_uno(page_number, nullptr);
      }
      if (svg.empty() || svg.find("<svg") == std::string::npos) {
        // Writer (and some other classes) have no SVG store filter.
        // Paint the page and wrap the PNG in an SVG so the wire format
        // stays PAGE_IMAGE_FORMAT_SVG.
        double scale = options.dpi / kTwipsPerInch;
        if (options.max_width_px > 0 && page.width > 0) {
          scale = static_cast<double>(options.max_width_px) / page.width;
        }
        int width_px = std::max(1, static_cast<int>(std::lround(page.width * scale)));
        int height_px = std::max(1, static_cast<int>(std::lround(page.height * scale)));
        std::vector<unsigned char> pixels(
            static_cast<size_t>(width_px) * height_px * 4);
        document->paintTile(pixels.data(), width_px, height_px,
                            static_cast<int>(page.x), static_cast<int>(page.y),
                            static_cast<int>(page.width),
                            static_cast<int>(page.height));
        std::string png = encode_png(pixels.data(), width_px, height_px, bgra);
        svg = svg_from_png(png, width_px, height_px);
      }
      if (svg.empty() || svg.find("<svg") == std::string::npos) {
        encoder_ok = false;
        *error = "SVG export failed";
        break;
      }
      encoded_bytes += static_cast<long>(svg.size());
      officev1::StreamPagesResponse page_event;
      officev1::PageImage* image = page_event.mutable_page_image();
      image->set_index(static_cast<int>(index));
      image->set_width_px(0);
      image->set_height_px(0);
      image->set_dpi(0);
      image->set_png(std::move(svg));
      image->set_format(officev1::PAGE_IMAGE_FORMAT_SVG);
      if (!emit(out_fd, page_event)) {
        encoder_ok = false;
        break;
      }
      continue;
    }
    double scale = options.dpi / kTwipsPerInch;
    int effective_dpi = options.dpi;
    if (options.max_width_px > 0 && page.width > 0) {
      scale = static_cast<double>(options.max_width_px) / page.width;
      effective_dpi = std::max(1, static_cast<int>(scale * kTwipsPerInch));
    }
    long side = std::max(page.width, page.height);
    if (side * scale > options.max_side_px) {
      scale = static_cast<double>(options.max_side_px) / side;
      effective_dpi = std::max(1, static_cast<int>(scale * kTwipsPerInch));
    }
    int width_px = std::max(1, static_cast<int>(std::lround(page.width * scale)));
    int height_px = std::max(1, static_cast<int>(std::lround(page.height * scale)));
    RawPage raw;
    raw.index = static_cast<int>(index);
    raw.width_px = width_px;
    raw.height_px = height_px;
    raw.dpi = effective_dpi;
    raw.pixels.resize(static_cast<size_t>(width_px) * height_px * 4);
    document->paintTile(raw.pixels.data(), width_px, height_px,
                        static_cast<int>(page.x), static_cast<int>(page.y),
                        static_cast<int>(page.width), static_cast<int>(page.height));
    for (const RedactBox& box : redact) {
      if (box.page_index != raw.index) continue;
      const double px = scale;
      const int x0 = static_cast<int>((box.x_twips - page.x) * px);
      const int y0 = static_cast<int>((box.y_twips - page.y) * px);
      const int x1 = static_cast<int>(
          (box.x_twips + box.width_twips - page.x) * px);
      const int y1 = static_cast<int>(
          (box.y_twips + box.height_twips - page.y) * px);
      blackout_rect(&raw.pixels, width_px, height_px, x0, y0, x1, y1);
    }
    if (options.grayscale) {
      grayscale_pixels(raw.pixels.data(), width_px, height_px, bgra);
    }
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      queue_changed.wait(lock, [&] { return queue.size() < kMaxQueued || !encoder_ok; });
      if (!encoder_ok) break;
      queue.push_back(std::move(raw));
    }
    queue_changed.notify_one();
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    paint_done = true;
  }
  queue_changed.notify_one();
  encoder.join();
  *output_bytes += encoded_bytes.load();
  if (!encoder_ok) {
    *error = "PNG encoding or emission failed";
    return false;
  }
  return true;
}

}  // namespace

int run_render(const RenderOptions& options, int out_fd, std::string* error) {
  auto started = std::chrono::steady_clock::now();

  std::string profile_url = "file://" + options.work_dir + "/profile";
  lok::Office* office = lok::lok_cpp_init(options.install_path.c_str(), profile_url.c_str());
  if (office == nullptr) {
    *error = "LibreOfficeKit failed to initialize from " + options.install_path;
    return kExitRenderFailure;
  }

  std::string url = "file://" + options.doc_path;
  // Every load gets LOK's Batch option: it installs the non-interactive
  // handler, without which any import interaction (Calc's text-import
  // dialog, the corrupt-document repair prompt) parks the load on a condvar
  // forever. Delimiter formats additionally preset the text-import filter
  // (separator 44 comma / 9 tab, quote 34, charset 76 UTF-8, from row 1).
  const char* filter_options = "Batch=true";
  if (options.extension == "csv") {
    filter_options = "44,34,76,1,,0,false,true,true,false,false,false,Batch=true";
  } else if (options.extension == "tsv") {
    filter_options = "9,34,76,1,,0,false,true,true,false,false,false,Batch=true";
  }
  lok::Document* document = office->documentLoad(url.c_str(), filter_options);
  if (document == nullptr && options.allow_package_repair) {
    // Retry through the core's repair path. Batch stays on so the repair
    // prompt cannot park the load; RepairPackage rebuilds a rewritten copy
    // under TMPDIR (already pinned to this worker's tmpfs).
    std::string repair_options = std::string(filter_options) + ",RepairPackage=true";
    document = office->documentLoad(url.c_str(), repair_options.c_str());
  }
  if (document == nullptr) {
    char* office_error = office->getError();
    std::string detail = office_error != nullptr ? office_error : "unknown";
    std::free(office_error);
    // A refused load may be a broken package the core would only open
    // through its repair path, which rebuilds a rewritten copy of the
    // document (sfx2 stages one whenever RepairPackage is set). That path
    // is opt-in, so classify the refusal with the core's own broken-ZIP
    // probe and report the opt-in instead of a generic load failure. The
    // staged upload still exists here; it is only unlinked after a
    // successful load. Every package format is a ZIP, so four magic bytes
    // decide whether the full re-read for the probe can pay off at all;
    // anything else skips it, keeping garbage uploads at one RAM copy.
    std::string staged_bytes;
    {
      std::ifstream staged(options.doc_path, std::ios::binary);
      char magic[4] = {0, 0, 0, 0};
      staged.read(magic, sizeof magic);
      if (staged.gcount() == sizeof magic && magic[0] == 'P' &&
          magic[1] == 'K') {
        staged.seekg(0);
        staged_bytes.assign(std::istreambuf_iterator<char>(staged),
                            std::istreambuf_iterator<char>());
      }
    }
    if (!staged_bytes.empty() && is_repairable_broken_package(staged_bytes)) {
      if (!options.allow_package_repair) {
        *error = "the document package is broken; the office core can only "
                 "open it through its repair path, which rebuilds a rewritten "
                 "copy of the document and requires the allow_package_repair "
                 "opt-in";
        return kExitRepairNeedsOptIn;
      }
      *error = "document repair was requested but the office core still "
               "could not load the package";
      return kExitLoadFailure;
    }
    *error = "document load failed: " + detail;
    return kExitLoadFailure;
  }
  // The core opened its own descriptors on the document during the load and
  // reads through them from here on, lazy pulls of embedded media included,
  // so the directory entry is already dead weight. Removing it now bounds
  // the upload's presence in the work dir to the load call alone; nothing
  // may recreate it.
  if (::unlink(options.doc_path.c_str()) != 0) {
    *error = "cannot remove " + options.doc_path + " after load: "
        + std::strerror(errno);
    delete document;
    return kExitRenderFailure;
  }
  document->initializeForRendering(nullptr);
  std::vector<std::string> option_warnings;
  apply_document_options(options, &option_warnings);

  int type = document->getDocumentType();
  std::vector<PageRect> pages;
  if (type == LOK_DOCTYPE_TEXT) {
    char* rectangles = document->getPartPageRectangles();
    pages = parse_page_rectangles(rectangles);
    std::free(rectangles);
  }
  if (pages.empty()) {
    int parts = std::max(1, document->getParts());
    for (int part = 0; part < parts; part++) {
      PageRect rect;
      rect.part = part;
      if (parts > 1 || type != LOK_DOCTYPE_TEXT) document->setPart(part);
      document->getDocumentSize(&rect.width, &rect.height);
      if (rect.width > 0 && rect.height > 0) pages.push_back(rect);
    }
  }
  if (pages.empty()) {
    *error = "document has no renderable pages";
    delete document;
    return kExitRenderFailure;
  }

  std::vector<PartLayout> layouts;
  describe_parts(&layouts, &option_warnings);
  if ((options.skip_hidden || options.paint_used_range) && !layouts.empty()
      && type != LOK_DOCTYPE_TEXT) {
    std::vector<PageRect> filtered;
    for (PageRect page : pages) {
      if (page.part < 0 || static_cast<size_t>(page.part) >= layouts.size()) {
        filtered.push_back(page);
        continue;
      }
      const PartLayout& layout = layouts[static_cast<size_t>(page.part)];
      if (options.skip_hidden && !layout.visible) continue;
      if (options.paint_used_range && layout.used_width > 0
          && layout.used_height > 0) {
        page.x = layout.used_x;
        page.y = layout.used_y;
        page.width = layout.used_width;
        page.height = layout.used_height;
      }
      filtered.push_back(page);
    }
    pages.swap(filtered);
  }
  if (options.include_notes_pages && type == LOK_DOCTYPE_PRESENTATION) {
    const size_t slide_count = pages.size();
    for (size_t i = 0; i < slide_count; i++) {
      document->setPartMode(LOK_PARTMODE_NOTES);
      document->setPart(pages[i].part);
      PageRect notes;
      notes.part = pages[i].part;
      notes.notes = true;
      document->getDocumentSize(&notes.width, &notes.height);
      if (notes.width > 0 && notes.height > 0) pages.push_back(notes);
    }
    document->setPartMode(LOK_PARTMODE_SLIDES);
  }
  if (pages.empty()) {
    *error = "document has no renderable pages";
    delete document;
    return kExitRenderFailure;
  }

  bool bgra = document->getTileMode() == LOK_TILEMODE_BGRA;
  officev1::DocumentInfo info;
  info.set_source_format(options.extension);
  info.set_page_count(static_cast<int>(pages.size()));
  info.set_document_type(doc_type_name(type));
  for (const PageRect& page : pages) {
    officev1::PageRect* rect = info.add_page_rects();
    rect->set_x_twips(page.x);
    rect->set_y_twips(page.y);
    rect->set_width_twips(page.width);
    rect->set_height_twips(page.height);
  }

  // Per-line rectangles ride the selection callback. The probe registers
  // before any painting or export: redaction needs the measured line boxes
  // first, in both modes. The probe stays null when nothing needs line
  // rectangles or the event flush is unresolvable.
  std::vector<PageBox> page_boxes;
  for (const PageRect& page : pages) {
    PageBox box;
    box.x = page.x;
    box.y = page.y;
    box.width = page.width;
    box.height = page.height;
    page_boxes.push_back(box);
  }
  SelectionProbe probe;
  SelectionProbe* probe_ptr = nullptr;
  const bool want_line_rects =
      (options.mode == "pages"
       && (options.parts.wants(officev1::DOCUMENT_PART_LINE_RECTS)
           || options.parts.explicit_wants(
                  officev1::DOCUMENT_PART_CELL_LINE_RECTS)))
      || !options.redact_spans.empty();
  if (want_line_rects) {
    probe.reschedule = resolve_reschedule();
    probe.acquire_solar_mutex = reinterpret_cast<void (*)(unsigned int)>(
        dlsym(RTLD_DEFAULT, "_ZN11Application17AcquireSolarMutexEj"));
    probe.release_solar_mutex = reinterpret_cast<unsigned int (*)()>(
        dlsym(RTLD_DEFAULT, "_ZN11Application17ReleaseSolarMutexEv"));
    if (probe.reschedule == nullptr || probe.acquire_solar_mutex == nullptr ||
        probe.release_solar_mutex == nullptr) {
      probe.reschedule = nullptr;
    }
    if (probe.reschedule != nullptr) {
      probe.pages = page_boxes;
      document->registerCallback(&selection_callback, &probe);
      probe_ptr = &probe;
    } else if (options.mode == "pages"
               && options.parts.wants(officev1::DOCUMENT_PART_LINE_RECTS)) {
      option_warnings.push_back(
          "typed content: event flush unavailable, line rectangles omitted");
    }
  }
  std::vector<RedactBox> redact;
  if (!options.redact_spans.empty()) {
    collect_redact_boxes(options, probe_ptr, page_boxes, &redact,
                         &option_warnings);
  }

  long output_bytes = 0;
  bool ok = true;
  if (options.mode == "pages") {
    officev1::StreamPagesResponse response;
    *response.mutable_document_info() = info;
    ok = emit(out_fd, response);

    // The paint/encode pipeline runs only when page images are selected.
    // The layout above (initializeForRendering, page rectangles) always
    // runs: typed anchors read the live layout, and DocumentInfo.page_count
    // must stay correct either way.
    std::vector<std::string> typed_warnings = option_warnings;
    if (ok && options.parts.wants(officev1::DOCUMENT_PART_PAGES)) {
      if (!paint_pages(document, options, pages, bgra, out_fd, &output_bytes,
                       error, redact)) {
        delete document;
        return kExitRenderFailure;
      }
    }
    // Pages have streamed; typed content follows from the same loaded
    // document, each event emitted the moment it is extracted. Extraction
    // problems degrade to status warnings, never a failed render.
    if (ok) {
      ok = emit_typed_content(
          options.parts, probe_ptr,
          [&](const google::protobuf::MessageLite& event) {
            output_bytes += static_cast<long>(event.ByteSizeLong());
            return emit(out_fd, event);
          },
          &typed_warnings);
    }
    if (probe_ptr != nullptr) document->registerCallback(nullptr, nullptr);
    if (ok) {
      officev1::StreamPagesResponse final_event;
      officev1::RenderStatus* status = final_event.mutable_status();
      status->set_state(officev1::RenderStatus::STATE_OK);
      for (const std::string& warning : typed_warnings) {
        status->add_warnings(warning);
      }
      status->set_input_bytes(options.input_bytes);
      status->set_output_bytes(output_bytes);
      status->set_render_millis(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
      ok = emit(out_fd, final_event);
    }
  } else {
    // The export filter is keyed on the loaded document's class, exactly
    // the switch LOK's own saveAs runs; the upload extension is no guide
    // (an .odg can load as an Impress shell). All four names reach the one
    // shared pdf filter implementation.
    const char* pdf_filter = nullptr;
    switch (type) {
      case LOK_DOCTYPE_TEXT: pdf_filter = "writer_pdf_Export"; break;
      case LOK_DOCTYPE_SPREADSHEET: pdf_filter = "calc_pdf_Export"; break;
      case LOK_DOCTYPE_PRESENTATION: pdf_filter = "impress_pdf_Export"; break;
      case LOK_DOCTYPE_DRAWING: pdf_filter = "draw_pdf_Export"; break;
    }
    if (pdf_filter == nullptr) {
      *error = "cannot export this document type to PDF";
      delete document;
      return kExitRenderFailure;
    }
    officev1::ConvertToPdfResponse response;
    *response.mutable_document_info() = info;
    ok = emit(out_fd, response);
    // The PDF streams straight from the export filter's output stream into
    // PdfChunk frames: no out.pdf staging file and no whole-PDF buffer
    // exist in the worker. A filter failure delivers no bytes first, so
    // the stream carries no partial chunks before the error status.
    if (!options.redact_spans.empty()) {
      apply_redact_shapes(redact, page_boxes, &option_warnings);
    }
    if (probe_ptr != nullptr) {
      document->registerCallback(nullptr, nullptr);
      probe_ptr = nullptr;
    }
    PdfExportOptions pdf;
    pdf.first_page = options.first_page;
    pdf.last_page = options.last_page;
    if (ok && !export_pdf_stream(
                  pdf_filter, kPdfChunkBytes,
                  [&](std::string&& chunk) {
                    officev1::ConvertToPdfResponse chunk_event;
                    chunk_event.mutable_pdf_chunk()->set_data(std::move(chunk));
                    return emit(out_fd, chunk_event);
                  },
                  &output_bytes, error, pdf)) {
      delete document;
      return kExitRenderFailure;
    }
    if (ok) {
      officev1::ConvertToPdfResponse final_event;
      officev1::RenderStatus* status = final_event.mutable_status();
      status->set_state(officev1::RenderStatus::STATE_OK);
      for (const std::string& warning : option_warnings) {
        status->add_warnings(warning);
      }
      status->set_input_bytes(options.input_bytes);
      status->set_output_bytes(output_bytes);
      status->set_render_millis(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
      ok = emit(out_fd, final_event);
    }
  }

  delete document;
  if (!ok) {
    *error = "event emission failed (parent gone?)";
    return kExitRenderFailure;
  }
  return kExitOk;
}

}  // namespace grlibre
