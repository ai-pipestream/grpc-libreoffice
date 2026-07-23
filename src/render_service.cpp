#include "render_service.h"

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

// A unique writable directory for one worker, removed on destruction.
class ScopedWorkDir {
 public:
  ScopedWorkDir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base != nullptr ? base : "/tmp") + "/grlibre-XXXXXX";
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
  bool saw_complete = false;

  Request request;
  while (stream->Read(&request)) {
    const officev1::DocumentChunk& chunk = request.chunk();
    if (document_id.empty()) document_id = chunk.document_id();
    if (filename.empty()) filename = chunk.filename();
    if (content_type.empty()) content_type = chunk.content_type();
    capture_parts(request, &parts_token);
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

  SlotGuard slot(*this);
  ScopedWorkDir work_dir;
  if (work_dir.path().empty()) {
    failed++;
    return {grpc::StatusCode::INTERNAL, "cannot create worker directory"};
  }

  std::vector<std::string> argv = {
      config_.worker_path, mode, extension,
      std::to_string(config_.render_dpi), std::to_string(config_.max_side_px),
      work_dir.path(), config_.install_path,
      parts_token.empty() ? "all" : parts_token};
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
  return grpc::Status::OK;
}

}  // namespace grlibre
