#include "render_service.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>

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

void capture_range(const officev1::ConvertToPdfRequest&, PageRange*) {}

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

template <typename Response, typename Request>
grpc::Status RenderServiceImpl::render(
    const char* mode, grpc::ServerReaderWriter<Response, Request>* stream) {
  std::string bytes;
  std::string document_id;
  std::string filename;
  std::string content_type;
  std::string parts_token;
  int requested_dpi = 0;
  PageRange page_range;
  bool allow_package_repair = false;
  bool saw_complete = false;

  Request request;
  while (stream->Read(&request)) {
    const officev1::DocumentChunk& chunk = request.chunk();
    if (document_id.empty()) document_id = chunk.document_id();
    if (filename.empty()) filename = chunk.filename();
    if (content_type.empty()) content_type = chunk.content_type();
    capture_parts(request, &parts_token);
    capture_dpi(request, &requested_dpi);
    capture_range(request, &page_range);
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

  SlotGuard slot(*this);
  ScopedWorkDir work_dir(config_.tmpfs_dir);
  if (work_dir.path().empty()) {
    failed++;
    return {grpc::StatusCode::INTERNAL, "cannot create worker directory"};
  }

  int render_dpi = requested_dpi != 0
                       ? std::clamp(requested_dpi, kMinRenderDpi, kMaxRenderDpi)
                       : config_.render_dpi;
  std::vector<std::string> argv = {
      config_.worker_path, mode, extension,
      std::to_string(render_dpi), std::to_string(config_.max_side_px),
      work_dir.path(), config_.install_path,
      parts_token.empty() ? "all" : parts_token,
      allow_package_repair ? "repair" : "no-repair",
      std::to_string(page_range.first) + ":" + std::to_string(page_range.last)};
  // Frames can carry a full page PNG; bound generously above the pixel cap.
  std::uint32_t max_frame = 256u * 1024 * 1024;
  WorkerOutcome outcome = run_worker(
      argv, bytes, config_.task_deadline, max_frame, [&](std::string&& payload) {
        Response response;
        if (!response.ParseFromString(payload)) return false;
        if (response.has_document_info()) {
          response.mutable_document_info()->set_document_id(document_id);
        }
        return stream->Write(response);
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
  return render("pages", stream);
}

grpc::Status RenderServiceImpl::ConvertToPdf(
    grpc::ServerContext*,
    grpc::ServerReaderWriter<officev1::ConvertToPdfResponse,
                             officev1::ConvertToPdfRequest>* stream) {
  return render("pdf", stream);
}

grpc::Status RenderServiceImpl::GetServiceInfo(
    grpc::ServerContext*, const officev1::GetServiceInfoRequest*,
    officev1::GetServiceInfoResponse* response) {
  response->set_service_version("0.3.0");
  response->set_typed_content(true);
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
  return grpc::Status::OK;
}

}  // namespace grlibre
