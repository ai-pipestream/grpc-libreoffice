#include "lok_engine.h"

// The tile-rendering surface (paintTile, parts, page rectangles) is gated
// behind this define; it is the same API Collabora Online builds on.
#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKit.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "ai/pipestream/office/v1/office_service.pb.h"
#include "event_frame.h"
#include "png_encode.h"

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
  if (document == nullptr) {
    char* office_error = office->getError();
    *error = std::string("document load failed: ")
        + (office_error != nullptr ? office_error : "unknown");
    std::free(office_error);
    return kExitLoadFailure;
  }
  document->initializeForRendering(nullptr);

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

  bool bgra = document->getTileMode() == LOK_TILEMODE_BGRA;
  officev1::DocumentInfo info;
  info.set_source_format(options.extension);
  info.set_page_count(static_cast<int>(pages.size()));
  info.set_document_type(doc_type_name(type));

  long output_bytes = 0;
  bool ok = true;
  if (options.mode == "pages") {
    officev1::StreamPagesResponse response;
    *response.mutable_document_info() = info;
    ok = emit(out_fd, response);

    // Two-stage pipeline: this thread paints page N+1 while the encoder
    // thread compresses and emits page N. The queue is bounded so raw
    // pixel buffers never pile up; the FIFO plus single encoder keeps
    // emission in page order.
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
        std::string png = encode_png(raw.pixels.data(), raw.width_px, raw.height_px, bgra);
        if (png.empty()) {
          encoder_ok = false;
          return;
        }
        encoded_bytes += static_cast<long>(png.size());
        officev1::StreamPagesResponse page_event;
        officev1::PageImage* image = page_event.mutable_page_image();
        image->set_index(raw.index);
        image->set_width_px(raw.width_px);
        image->set_height_px(raw.height_px);
        image->set_dpi(raw.dpi);
        image->set_png(std::move(png));
        if (!emit(out_fd, page_event)) {
          encoder_ok = false;
          return;
        }
      }
    });

    for (size_t index = 0; ok && encoder_ok && index < pages.size(); index++) {
      const PageRect& page = pages[index];
      document->setPart(page.part);
      double scale = options.dpi / kTwipsPerInch;
      int effective_dpi = options.dpi;
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
    output_bytes = encoded_bytes.load();
    if (!encoder_ok) {
      *error = "PNG encoding or emission failed";
      delete document;
      return kExitRenderFailure;
    }
    if (ok) {
      officev1::StreamPagesResponse final_event;
      officev1::RenderStatus* status = final_event.mutable_status();
      status->set_state(officev1::RenderStatus::STATE_OK);
      status->set_input_bytes(options.input_bytes);
      status->set_output_bytes(output_bytes);
      status->set_render_millis(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
      ok = emit(out_fd, final_event);
    }
  } else {
    std::string pdf_path = options.work_dir + "/out.pdf";
    std::string pdf_url = "file://" + pdf_path;
    if (!document->saveAs(pdf_url.c_str(), "pdf", nullptr)) {
      char* office_error = office->getError();
      *error = std::string("PDF export failed: ")
          + (office_error != nullptr ? office_error : "unknown");
      std::free(office_error);
      delete document;
      return kExitRenderFailure;
    }
    std::ifstream pdf(pdf_path, std::ios::binary);
    std::string pdf_bytes((std::istreambuf_iterator<char>(pdf)),
                          std::istreambuf_iterator<char>());
    if (pdf_bytes.empty()) {
      *error = "PDF export produced no bytes";
      delete document;
      return kExitRenderFailure;
    }
    officev1::ConvertToPdfResponse response;
    *response.mutable_document_info() = info;
    ok = emit(out_fd, response);
    for (size_t offset = 0; ok && offset < pdf_bytes.size(); offset += kPdfChunkBytes) {
      officev1::ConvertToPdfResponse chunk_event;
      chunk_event.mutable_pdf_chunk()->set_data(
          pdf_bytes.substr(offset, kPdfChunkBytes));
      ok = emit(out_fd, chunk_event);
    }
    output_bytes = static_cast<long>(pdf_bytes.size());
    if (ok) {
      officev1::ConvertToPdfResponse final_event;
      officev1::RenderStatus* status = final_event.mutable_status();
      status->set_state(officev1::RenderStatus::STATE_OK);
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
