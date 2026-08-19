#include "render_service.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "docling_map.h"
#include "worker_runner.h"

namespace grlibre {

namespace {

namespace officev1 = ai::pipestream::office::v1;

// Canonical extensions the office core loads; the advertised format list.
const std::vector<std::string> kExtensions = {
    "doc", "docx", "dot", "dotx", "rtf", "txt", "html", "odt", "ott", "fodt", "wpd",
    "xls", "xlsx", "xlt", "xltx", "csv", "tsv", "ods", "ots", "fods",
    "ppt", "pptx", "pot", "potx", "odp", "otp", "fodp",
    "odg", "fodg", "vsd", "vsdx", "pdf"};

// Fallback resolution when the filename has no usable extension.
const std::unordered_map<std::string, std::string> kContentTypes = {
    {"application/pdf", "pdf"},
    {"application/msword", "doc"},
    {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "docx"},
    {"application/rtf", "rtf"},
    {"text/rtf", "rtf"},
    {"text/plain", "txt"},
    {"text/html", "html"},
    {"application/vnd.oasis.opendocument.text", "odt"},
    {"application/vnd.ms-excel", "xls"},
    {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "xlsx"},
    {"text/csv", "csv"},
    {"application/vnd.oasis.opendocument.spreadsheet", "ods"},
    {"application/vnd.ms-powerpoint", "ppt"},
    {"application/vnd.openxmlformats-officedocument.presentationml.presentation", "pptx"},
    {"application/vnd.oasis.opendocument.presentation", "odp"},
    {"application/vnd.oasis.opendocument.graphics", "odg"},
    {"application/vnd.visio", "vsd"}};

std::string lowercase(std::string value) {
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

// Resolves the canonical source extension; empty when unresolvable.
std::string resolve_extension(const std::string& filename, const std::string& content_type) {
  size_t dot = filename.rfind('.');
  if (dot != std::string::npos && dot + 1 < filename.size()) {
    std::string extension = lowercase(filename.substr(dot + 1));
    for (const std::string& known : kExtensions) {
      if (known == extension) return extension;
    }
  }
  std::string bare = lowercase(content_type.substr(0, content_type.find(';')));
  while (!bare.empty() && bare.back() == ' ') bare.pop_back();
  auto found = kContentTypes.find(bare);
  return found != kContentTypes.end() ? found->second : "";
}

// The part selector rides the worker argv as one token: "all" for the wire
// default, otherwise the selected DocumentPart numeric values joined by
// commas. Only StreamPagesRequest carries options; the first request whose
// parts list is non-empty wins, matching the chunk identity fields.
// DOCUMENT_PART_UNSPECIFIED entries are dropped, so a list of only zeros
// counts as empty.
void capture_parts(const officev1::StreamPagesRequest& request,
                   std::string* token) {
  if (!token->empty()) return;
  std::string joined;
  for (int part : request.options().parts()) {
    if (part <= 0) continue;
    if (!joined.empty()) joined += ",";
    joined += std::to_string(part);
  }
  *token = joined;
}

// PDF mode emits no typed content, so its request carries no selector.
void capture_parts(const officev1::ConvertToPdfRequest&, std::string*) {}

// The per-request DPI override rides StreamOptions; the first nonzero value
// wins, matching how the parts list resolves. PDF mode does not rasterize
// pages, so its request carries no override.
void capture_dpi(const officev1::StreamPagesRequest& request, int* dpi) {
  if (*dpi == 0) *dpi = request.options().render_dpi();
}

void capture_dpi(const officev1::ConvertToPdfRequest&, int*) {}

// The page range rides StreamOptions and resolves as one pair: the first
// request with either bound nonzero wins. Validated after upload; the
// worker receives it as one "first:last" argv token. PDF mode always
// renders the whole document.
struct PageRange {
  int first = 0;
  int last = 0;
};

void capture_range(const officev1::StreamPagesRequest& request,
                   PageRange* range) {
  if (range->first == 0 && range->last == 0) {
    range->first = request.options().first_page();
    range->last = request.options().last_page();
  }
}

void capture_range(const officev1::ConvertToPdfRequest& request,
                   PageRange* range) {
  if (range->first == 0 && range->last == 0) {
    range->first = request.first_page();
    range->last = request.last_page();
  }
}

void merge_stream_options(const officev1::StreamOptions& incoming,
                          officev1::StreamOptions* dest) {
  if (dest->parts_size() == 0 && incoming.parts_size() > 0) {
    dest->mutable_parts()->CopyFrom(incoming.parts());
  }
  if (dest->render_dpi() == 0) dest->set_render_dpi(incoming.render_dpi());
  if (dest->first_page() == 0 && dest->last_page() == 0
      && (incoming.first_page() != 0 || incoming.last_page() != 0)) {
    dest->set_first_page(incoming.first_page());
    dest->set_last_page(incoming.last_page());
  }
  if (dest->page_format() == 0 && dest->page_quality() == 0) {
    dest->set_page_format(incoming.page_format());
    dest->set_page_quality(incoming.page_quality());
  }
  if (dest->max_width_px() == 0) dest->set_max_width_px(incoming.max_width_px());
  dest->set_grayscale(dest->grayscale() || incoming.grayscale());
  if (dest->timeout_seconds() == 0) {
    dest->set_timeout_seconds(incoming.timeout_seconds());
  }
  if (dest->tracked_changes() == 0) {
    dest->set_tracked_changes(incoming.tracked_changes());
  }
  if (dest->vector_format() == 0) {
    dest->set_vector_format(incoming.vector_format());
  }
  if (incoming.page_format() == officev1::PAGE_IMAGE_FORMAT_SVG
      && dest->vector_format() == 0) {
    dest->set_vector_format(officev1::PAGE_VECTOR_FORMAT_SVG);
  }
  dest->set_skip_hidden(dest->skip_hidden() || incoming.skip_hidden());
  dest->set_paint_used_range(dest->paint_used_range()
                             || incoming.paint_used_range());
  dest->set_include_notes_pages(dest->include_notes_pages()
                                || incoming.include_notes_pages());
  if (dest->form_values_size() == 0 && incoming.form_values_size() > 0) {
    dest->mutable_form_values()->CopyFrom(incoming.form_values());
  }
  if (dest->redact_spans_size() == 0 && incoming.redact_spans_size() > 0) {
    dest->mutable_redact_spans()->CopyFrom(incoming.redact_spans());
  }
}

void capture_extras(const officev1::StreamPagesRequest& request,
                    officev1::StreamOptions* extras) {
  merge_stream_options(request.options(), extras);
}

void capture_extras(const officev1::ConvertToPdfRequest& request,
                    officev1::StreamOptions* extras) {
  officev1::StreamOptions incoming;
  incoming.set_timeout_seconds(request.timeout_seconds());
  incoming.set_tracked_changes(request.tracked_changes());
  incoming.set_skip_hidden(request.skip_hidden());
  incoming.set_first_page(request.first_page());
  incoming.set_last_page(request.last_page());
  incoming.mutable_form_values()->CopyFrom(request.form_values());
  incoming.mutable_redact_spans()->CopyFrom(request.redact_spans());
  merge_stream_options(incoming, extras);
}

// The page image encoding rides StreamOptions and resolves as one pair with
// its quality: the first request with either field nonzero wins. PDF mode
// emits no page images.
struct ImageEncoding {
  int format = 0;   // officev1::PageImageFormat wire value.
  int quality = 0;  // 0 means the server default.
};

void capture_encoding(const officev1::StreamPagesRequest& request,
                      ImageEncoding* encoding) {
  if (encoding->format == 0 && encoding->quality == 0) {
    encoding->format = request.options().page_format();
    encoding->quality = request.options().page_quality();
  }
}

void capture_encoding(const officev1::ConvertToPdfRequest&, ImageEncoding*) {}

// The encoding's worker argv token: the format name, with the lossy quality
// appended as ":Q". Returns an empty string for an unknown format value.
std::string encoding_token(const ImageEncoding& encoding) {
  int quality = encoding.quality == 0 ? 85 : encoding.quality;
  switch (encoding.format) {
    case officev1::PAGE_IMAGE_FORMAT_UNSPECIFIED:
    case officev1::PAGE_IMAGE_FORMAT_PNG:
      return "png";
    case officev1::PAGE_IMAGE_FORMAT_JPEG:
      return "jpeg:" + std::to_string(quality);
    case officev1::PAGE_IMAGE_FORMAT_WEBP:
      return "webp:" + std::to_string(quality);
    case officev1::PAGE_IMAGE_FORMAT_SVG:
      // Raster argv is unused; vector_format rides options.pb.
      return "png";
    default:
      return "";
  }
}

// The repair opt-in may ride any request of the upload stream; true on any
// request enables it, mirroring how the chunk identity fields resolve.
void capture_repair(const officev1::StreamPagesRequest& request, bool* repair) {
  *repair = *repair || request.allow_package_repair();
}

void capture_repair(const officev1::ConvertToPdfRequest& request, bool* repair) {
  *repair = *repair || request.allow_package_repair();
}

// A unique 0700 directory for one worker on the service's tmpfs, removed on
// destruction. The removal is what reclaims the RAM even when the worker
// was killed mid-render, so it must stay on the parent side of the process
// boundary.
class ScopedWorkDir {
 public:
  explicit ScopedWorkDir(const std::string& base) {
    std::string pattern = base + "/grlibre-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) != nullptr) path_ = buffer.data();
  }
  ~ScopedWorkDir() {
    if (!path_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace

class RenderServiceImpl::SlotGuard {
 public:
  SlotGuard(RenderServiceImpl& service) : service_(service) {
    std::unique_lock<std::mutex> lock(service_.slots_mutex_);
    service_.slots_available_.wait(lock, [&] {
      return service_.busy_slots_ < service_.config_.max_concurrent_documents;
    });
    service_.busy_slots_++;
  }
  ~SlotGuard() {
    {
      std::lock_guard<std::mutex> lock(service_.slots_mutex_);
      service_.busy_slots_--;
    }
    service_.slots_available_.notify_one();
  }

 private:
  RenderServiceImpl& service_;
};

RenderServiceImpl::RenderServiceImpl(ServiceConfig config)
    : config_(std::move(config)), supported_formats_(kExtensions) {}

template <typename Response, typename Request, typename In>
grpc::Status RenderServiceImpl::render(
    const char* mode, In* in, const std::function<bool(Response&&)>& write,
    const char* default_parts) {
  std::string bytes;
  std::string document_id;
  std::string filename;
  std::string content_type;
  std::string parts_token;
  int requested_dpi = 0;
  PageRange page_range;
  ImageEncoding encoding;
  officev1::StreamOptions extras;
  bool allow_package_repair = false;
  bool saw_complete = false;

  Request request;
  while (in->Read(&request)) {
    const officev1::DocumentChunk& chunk = request.chunk();
    if (document_id.empty()) document_id = chunk.document_id();
    if (filename.empty()) filename = chunk.filename();
    if (content_type.empty()) content_type = chunk.content_type();
    capture_parts(request, &parts_token);
    capture_dpi(request, &requested_dpi);
    capture_range(request, &page_range);
    capture_encoding(request, &encoding);
    capture_extras(request, &extras);
    capture_repair(request, &allow_package_repair);
    if (static_cast<long>(bytes.size() + chunk.data().size()) > config_.max_document_bytes) {
      rejected++;
      return {grpc::StatusCode::RESOURCE_EXHAUSTED,
              "document exceeds " + std::to_string(config_.max_document_bytes) + " bytes"};
    }
    bytes.append(chunk.data());
    if (chunk.complete()) saw_complete = true;
  }
  if (bytes.empty()) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT, "no document bytes received"};
  }
  if (!saw_complete) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "stream ended without a chunk marked complete"};
  }
  std::string extension = resolve_extension(filename, content_type);
  if (extension.empty()) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "cannot determine source format from filename \"" + filename
                + "\" or content type \"" + content_type + "\""};
  }
  if (page_range.first < 0 || page_range.last < 0
      || (page_range.first > 0 && page_range.last > 0
          && page_range.first > page_range.last)) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "invalid page range: first_page " + std::to_string(page_range.first)
                + ", last_page " + std::to_string(page_range.last)};
  }
  std::string format_token = encoding_token(encoding);
  if (format_token.empty()) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "unsupported page_format value "
                + std::to_string(encoding.format)};
  }
  if (encoding.quality < 0 || encoding.quality > 100) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "page_quality must be within [0, 100], got "
                + std::to_string(encoding.quality)};
  }
  if (extras.max_width_px() < 0 || extras.max_width_px() > kMaxWidthPx) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "max_width_px must be within [0, " + std::to_string(kMaxWidthPx)
                + "], got " + std::to_string(extras.max_width_px())};
  }
  if (extras.timeout_seconds() < 0
      || extras.timeout_seconds() > kMaxTimeoutSeconds) {
    rejected++;
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "timeout_seconds must be within [0, "
                + std::to_string(kMaxTimeoutSeconds) + "], got "
                + std::to_string(extras.timeout_seconds())};
  }
  for (const auto& span : extras.redact_spans()) {
    if (span.char_start() < 0 || span.char_end() < span.char_start()) {
      rejected++;
      return {grpc::StatusCode::INVALID_ARGUMENT,
              "redact span char_end must be >= char_start"};
    }
  }

  SlotGuard slot(*this);
  ScopedWorkDir work_dir(config_.tmpfs_dir);
  if (work_dir.path().empty()) {
    failed++;
    return {grpc::StatusCode::INTERNAL, "cannot create worker directory"};
  }

  {
    std::ofstream extras_out(work_dir.path() + "/options.pb", std::ios::binary);
    if (!extras.SerializeToOstream(&extras_out)) {
      failed++;
      return {grpc::StatusCode::INTERNAL, "cannot write worker options"};
    }
  }
  int render_dpi = requested_dpi != 0
                       ? std::clamp(requested_dpi, kMinRenderDpi, kMaxRenderDpi)
                       : config_.render_dpi;
  auto deadline = config_.task_deadline;
  if (extras.timeout_seconds() > 0) {
    deadline = std::chrono::milliseconds(extras.timeout_seconds() * 1000);
  }
  std::vector<std::string> argv = {
      config_.worker_path, mode, extension,
      std::to_string(render_dpi), std::to_string(config_.max_side_px),
      work_dir.path(), config_.install_path,
      parts_token.empty() ? default_parts : parts_token,
      allow_package_repair ? "repair" : "no-repair",
      std::to_string(page_range.first) + ":" + std::to_string(page_range.last),
      format_token};
  // Frames can carry a full page PNG; bound generously above the pixel cap.
  std::uint32_t max_frame = 256u * 1024 * 1024;
  WorkerOutcome outcome = run_worker(
      argv, bytes, deadline, max_frame, [&](std::string&& payload) {
        Response response;
        if (!response.ParseFromString(payload)) return false;
        if (response.has_document_info()) {
          response.mutable_document_info()->set_document_id(document_id);
        }
        return write(std::move(response));
      });

  switch (outcome.kind) {
    case WorkerOutcome::Kind::kOk:
      rendered++;
      return grpc::Status::OK;
    case WorkerOutcome::Kind::kLoadFailure:
      rejected++;
      return {grpc::StatusCode::INVALID_ARGUMENT, outcome.detail};
    case WorkerOutcome::Kind::kRepairNeedsOptIn:
      rejected++;
      return {grpc::StatusCode::FAILED_PRECONDITION, outcome.detail};
    case WorkerOutcome::Kind::kRepairUnimplemented:
      rejected++;
      return {grpc::StatusCode::UNIMPLEMENTED, outcome.detail};
    case WorkerOutcome::Kind::kWorkDirNotTmpfs:
      failed++;
      return {grpc::StatusCode::FAILED_PRECONDITION, outcome.detail};
    case WorkerOutcome::Kind::kTimeout:
      failed++;
      return {grpc::StatusCode::DEADLINE_EXCEEDED,
              "render exceeded the per-document timeout"};
    case WorkerOutcome::Kind::kAborted:
      failed++;
      return grpc::Status::CANCELLED;
    case WorkerOutcome::Kind::kCrash:
    default:
      failed++;
      return {grpc::StatusCode::INTERNAL, outcome.detail};
  }
}

grpc::Status RenderServiceImpl::StreamPages(
    grpc::ServerContext*,
    grpc::ServerReaderWriter<officev1::StreamPagesResponse,
                             officev1::StreamPagesRequest>* stream) {
  return render<officev1::StreamPagesResponse, officev1::StreamPagesRequest>(
      "pages", stream, [&](officev1::StreamPagesResponse&& response) {
        return stream->Write(response);
      });
}

grpc::Status RenderServiceImpl::ConvertToPdf(
    grpc::ServerContext*,
    grpc::ServerReaderWriter<officev1::ConvertToPdfResponse,
                             officev1::ConvertToPdfRequest>* stream) {
  return render<officev1::ConvertToPdfResponse, officev1::ConvertToPdfRequest>(
      "pdf", stream, [&](officev1::ConvertToPdfResponse&& response) {
        return stream->Write(response);
      });
}

grpc::Status RenderServiceImpl::ToDocument(
    grpc::ServerContext*,
    grpc::ServerReader<officev1::StreamPagesRequest>* reader,
    officev1::ToDocumentResponse* response) {
  DoclingMapper mapper;
  // Page images are omitted unless the caller explicitly selects
  // DOCUMENT_PART_PAGES: the mapper inlines them as data URIs, which would
  // blow the unary response far past typical client message limits.
  grpc::Status status =
      render<officev1::StreamPagesResponse, officev1::StreamPagesRequest>(
          "pages", reader,
          [&](officev1::StreamPagesResponse&& event) {
            if (event.has_document_info()) {
              *response->mutable_document_info() = event.document_info();
            }
            if (event.has_status()) {
              *response->mutable_status() = event.status();
            }
            mapper.consume(event);
            return true;
          },
          "all-but-pages");
  if (status.ok()) {
    *response->mutable_document() = mapper.take();
  }
  return status;
}

grpc::Status RenderServiceImpl::GetServiceInfo(
    grpc::ServerContext*, const officev1::GetServiceInfoRequest*,
    officev1::GetServiceInfoResponse* response) {
  response->set_service_version("0.4.0");
  response->set_typed_content(true);
  response->set_document_mapping(true);
  response->set_package_repair(true);
  response->set_libreoffice_version(config_.libreoffice_version);
  response->set_api_version("v1");
  for (const std::string& format : supported_formats_) {
    response->add_supported_formats(format);
  }
  response->set_max_document_bytes(config_.max_document_bytes);
  response->set_max_concurrent_documents(config_.max_concurrent_documents);
  response->set_render_dpi(config_.render_dpi);
  // The diskless posture: document bytes stay in RAM-backed tmpfs, and
  // every LibreOffice-internal temp artifact class is named so callers can
  // reason about their own threat model.
  response->set_diskless_documents(true);
  response->add_internal_temp_artifacts(
      "odf-load: LibreOffice keeps one internal temp copy of an ODF package "
      "in the tmpfs work dir until the document closes");
  response->add_internal_temp_artifacts(
      "pdf-import: LibreOffice's PDF import stages one full copy of the "
      "uploaded document in the tmpfs work dir for the document lifetime");
  response->add_internal_temp_artifacts(
      "embedded-media: raw bytes of embedded objects and derived bitmaps "
      "may spill into the tmpfs work dir during a render");
  response->add_internal_temp_artifacts(
      "pdf-export: LibreOffice renders through one internal temp file in "
      "the tmpfs work dir, unlinked right after the store");
  // Frontend advertisement for the shared demo shell.
  auto* ui = response->mutable_ui();
  ui->set_title("LibreOffice");
  ui->set_path("/ui/libreoffice");
  ui->set_description(
      "Renders office documents via LibreOfficeKit; pages out as PNG");
  return grpc::Status::OK;
}

}  // namespace grlibre
