// End-to-end service test over localhost. Protocol error paths run
// everywhere; the happy path needs LibreOffice and is skipped without it.

#include <grpcpp/grpcpp.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ai/pipestream/office/v1/office_service.grpc.pb.h"
#include "encrypted_docx_fixture.h"
#include "render_service.h"

namespace {

namespace officev1 = ai::pipestream::office::v1;

void require(bool condition, const std::string& what) {
  if (!condition) {
    std::println(stderr, "FAIL: {}", what);
    std::exit(1);
  }
}

struct StreamResult {
  grpc::Status status;
  officev1::DocumentInfo info;
  int pages = 0;
  int paragraphs = 0;
  bool got_metadata = false;
  bool got_status = false;
  int first_page_dpi = 0;
  int first_page_width_px = 0;
  std::vector<int> page_indexes;
  std::string first_page_bytes;
  officev1::PageImageFormat first_page_format =
      officev1::PAGE_IMAGE_FORMAT_UNSPECIFIED;
};

// A stored-entry OOXML zip truncated right before its central directory:
// the office core's broken-ZIP probe classifies it as repairable. The same
// fixture the worker test uses; embedded as bytes because a broken zip
// cannot be a readable fixture.
constexpr char kRepairableDocx[] =
    "\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x72\x52\xf9\x5c\xe6\xb8\x74\x6b"
    "\x62\x00\x00\x00\x62\x00\x00\x00\x13\x00\x00\x00\x5b\x43\x6f\x6e\x74\x65"
    "\x6e\x74\x5f\x54\x79\x70\x65\x73\x5d\x2e\x78\x6d\x6c\x3c\x3f\x78\x6d\x6c"
    "\x20\x76\x65\x72\x73\x69\x6f\x6e\x3d\x22\x31\x2e\x30\x22\x3f\x3e\x3c\x54"
    "\x79\x70\x65\x73\x20\x78\x6d\x6c\x6e\x73\x3d\x22\x68\x74\x74\x70\x3a\x2f"
    "\x2f\x73\x63\x68\x65\x6d\x61\x73\x2e\x6f\x70\x65\x6e\x78\x6d\x6c\x66\x6f"
    "\x72\x6d\x61\x74\x73\x2e\x6f\x72\x67\x2f\x70\x61\x63\x6b\x61\x67\x65\x2f"
    "\x32\x30\x30\x36\x2f\x63\x6f\x6e\x74\x65\x6e\x74\x2d\x74\x79\x70\x65\x73"
    "\x22\x2f\x3e\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x72\x52\xf9\x5c\x93"
    "\x76\x4a\xd5\x8c\x00\x00\x00\x8c\x00\x00\x00\x11\x00\x00\x00\x77\x6f\x72"
    "\x64\x2f\x64\x6f\x63\x75\x6d\x65\x6e\x74\x2e\x78\x6d\x6c\x3c\x3f\x78\x6d"
    "\x6c\x20\x76\x65\x72\x73\x69\x6f\x6e\x3d\x22\x31\x2e\x30\x22\x3f\x3e\x3c"
    "\x77\x3a\x64\x6f\x63\x75\x6d\x65\x6e\x74\x20\x78\x6d\x6c\x6e\x73\x3a\x77"
    "\x3d\x22\x68\x74\x74\x70\x3a\x2f\x2f\x73\x63\x68\x65\x6d\x61\x73\x2e\x6f"
    "\x70\x65\x6e\x78\x6d\x6c\x66\x6f\x72\x6d\x61\x74\x73\x2e\x6f\x72\x67\x2f"
    "\x77\x6f\x72\x64\x70\x72\x6f\x63\x65\x73\x73\x69\x6e\x67\x6d\x6c\x2f\x32"
    "\x30\x30\x36\x2f\x6d\x61\x69\x6e\x22\x3e\x3c\x77\x3a\x62\x6f\x64\x79\x3e"
    "\x3c\x77\x3a\x70\x2f\x3e\x3c\x2f\x77\x3a\x62\x6f\x64\x79\x3e\x3c\x2f\x77"
    "\x3a\x64\x6f\x63\x75\x6d\x65\x6e\x74\x3e";

StreamResult stream_pages(const std::shared_ptr<grpc::Channel>& channel,
                          const std::string& bytes, const std::string& filename,
                          bool mark_complete, bool allow_package_repair = false,
                          int render_dpi = 0, int first_page = 0,
                          int last_page = 0, int page_format = 0,
                          int page_quality = 0,
                          const officev1::StreamOptions* extra = nullptr,
                          const std::vector<std::pair<std::string, std::string>>&
                              metadata = {}) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
  for (const auto& [key, value] : metadata) context.AddMetadata(key, value);
  auto stream = stub->StreamPages(&context);
  size_t chunk_size = 64 * 1024;
  for (size_t offset = 0; offset < bytes.size() || offset == 0; offset += chunk_size) {
    officev1::StreamPagesRequest request;
    request.set_allow_package_repair(allow_package_repair);
    if (offset == 0 && render_dpi != 0) {
      request.mutable_options()->set_render_dpi(render_dpi);
    }
    if (offset == 0 && (first_page != 0 || last_page != 0)) {
      request.mutable_options()->set_first_page(first_page);
      request.mutable_options()->set_last_page(last_page);
    }
    if (offset == 0 && (page_format != 0 || page_quality != 0)) {
      request.mutable_options()->set_page_format(
          static_cast<officev1::PageImageFormat>(page_format));
      request.mutable_options()->set_page_quality(page_quality);
    }
    if (offset == 0 && extra != nullptr) {
      request.mutable_options()->MergeFrom(*extra);
    }
    officev1::DocumentChunk* chunk = request.mutable_chunk();
    chunk->set_document_id("test-doc");
    chunk->set_filename(filename);
    if (offset < bytes.size()) {
      chunk->set_data(bytes.substr(offset, chunk_size));
    }
    chunk->set_complete(mark_complete && offset + chunk_size >= bytes.size());
    if (!stream->Write(request)) break;
    if (bytes.empty()) break;
  }
  stream->WritesDone();
  StreamResult result;
  officev1::StreamPagesResponse response;
  while (stream->Read(&response)) {
    if (response.has_document_info()) result.info = response.document_info();
    if (response.has_page_image()) {
      if (result.pages == 0) {
        result.first_page_dpi = response.page_image().dpi();
        result.first_page_width_px = response.page_image().width_px();
        result.first_page_bytes = response.page_image().png();
        result.first_page_format = response.page_image().format();
      }
      result.page_indexes.push_back(response.page_image().index());
      result.pages++;
    }
    if (response.has_paragraph()) result.paragraphs++;
    if (response.has_metadata()) result.got_metadata = true;
    if (response.has_status()) result.got_status = true;
  }
  result.status = stream->Finish();
  return result;
}

// Uploads one complete document to ConvertToPdf and returns the finish
// status; pdf_out collects the chunk bytes when non-null.
grpc::Status convert_to_pdf(const std::shared_ptr<grpc::Channel>& channel,
                            const std::string& bytes,
                            const std::string& filename, int first_page,
                            int last_page, std::string* pdf_out = nullptr,
                            int timeout_seconds = 0) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
  auto stream = stub->ConvertToPdf(&context);
  officev1::ConvertToPdfRequest request;
  request.set_first_page(first_page);
  request.set_last_page(last_page);
  request.set_timeout_seconds(timeout_seconds);
  officev1::DocumentChunk* chunk = request.mutable_chunk();
  chunk->set_document_id("pdf-doc");
  chunk->set_filename(filename);
  chunk->set_data(bytes);
  chunk->set_complete(true);
  stream->Write(request);
  stream->WritesDone();
  officev1::ConvertToPdfResponse response;
  while (stream->Read(&response)) {
    if (pdf_out != nullptr && response.has_pdf_chunk()) {
      *pdf_out += response.pdf_chunk().data();
    }
  }
  return stream->Finish();
}

// Uploads one complete document to ToDocument with the given options.
grpc::Status to_document(const std::shared_ptr<grpc::Channel>& channel,
                         const std::string& bytes, const std::string& filename,
                         const officev1::StreamOptions* options,
                         officev1::ToDocumentResponse* mapped) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
  auto writer = stub->ToDocument(&context, mapped);
  officev1::StreamPagesRequest request;
  if (options != nullptr) *request.mutable_options() = *options;
  officev1::DocumentChunk* chunk = request.mutable_chunk();
  chunk->set_document_id("to-doc");
  chunk->set_filename(filename);
  chunk->set_data(bytes);
  chunk->set_complete(true);
  writer->Write(request);
  writer->WritesDone();
  return writer->Finish();
}

// A stand-in worker for the admission and cancellation tests: it marks
// that it started (the script's path plus ".started"), drains its upload,
// then hangs the way a wedged office core would, for longer than any of
// those tests may take. Written next to the test binaries, the one place
// sure to allow exec (container tmpfs mounts are noexec).
std::string write_hanging_worker() {
  std::string path = (std::filesystem::current_path()
                      / ("grlibre-test-hanging-worker-"
                         + std::to_string(::getpid())))
                         .string();
  {
    std::ofstream out(path);
    out << "#!/bin/sh\n: > \"$0.started\"\ncat >/dev/null\nexec sleep 60\n";
  }
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  return path;
}

// Waits until the stand-in worker at path has started, for at most ten
// seconds, then clears its marker for the next run.
bool hanging_worker_started(const std::string& path) {
  const std::string marker = path + ".started";
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < end) {
    if (std::filesystem::exists(marker)) {
      std::filesystem::remove(marker);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

// Starts an in-process server for service on an ephemeral port.
std::unique_ptr<grpc::Server> start_server(grlibre::RenderServiceImpl* service,
                                           std::shared_ptr<grpc::Channel>* channel) {
  int port = 0;
  grpc::ServerBuilder builder;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(service);
  auto server = builder.BuildAndStart();
  require(server != nullptr, "server starts");
  *channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                 grpc::InsecureChannelCredentials());
  return server;
}

// Sends one complete small upload on an already created context.
grpc::Status upload_with(const std::shared_ptr<grpc::Channel>& channel,
                         grpc::ClientContext* context) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  auto stream = stub->StreamPages(context);
  officev1::StreamPagesRequest request;
  request.mutable_chunk()->set_filename("hang.txt");
  request.mutable_chunk()->set_data("hang");
  request.mutable_chunk()->set_complete(true);
  stream->Write(request);
  stream->WritesDone();
  officev1::StreamPagesResponse response;
  while (stream->Read(&response)) {}
  return stream->Finish();
}

// Polls counter until it exceeds before, for at most the given time.
bool counter_moves(const std::atomic<long>& counter, long before,
                   std::chrono::milliseconds within) {
  const auto end = std::chrono::steady_clock::now() + within;
  while (std::chrono::steady_clock::now() < end) {
    if (counter.load() > before) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return counter.load() > before;
}

// A caller that goes away stops costing anything: its deadline caps the
// worker's, a queued call gives up at its deadline instead of waiting for
// the slot, and cancelling a running call (ToDocument included, which
// writes nothing until the end) kills its worker. The stand-in worker
// would otherwise hold the single slot for a minute; the server's own
// counters show each request end within seconds.
void verify_callers_that_leave_stop_costing() {
  grlibre::ServiceConfig config;
  config.worker_path = write_hanging_worker();
  config.install_path = "/nonexistent";
  config.max_document_bytes = 1 << 20;
  config.max_concurrent_documents = 1;
  config.task_deadline = std::chrono::milliseconds(60000);
  grlibre::RenderServiceImpl service(config);
  std::shared_ptr<grpc::Channel> channel;
  auto server = start_server(&service, &channel);

  require(channel->WaitForConnected(std::chrono::system_clock::now()
                                    + std::chrono::seconds(10)),
          "the admission test server accepts connections");

  // The caller's deadline caps the worker's.
  {
    long before = service.failed.load();
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now()
                         + std::chrono::milliseconds(3000));
    grpc::Status status = upload_with(channel, &context);
    require(status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED,
            "a short client deadline ends the call");
    require(hanging_worker_started(config.worker_path),
            "the call got as far as its worker");
    require(counter_moves(service.failed, before, std::chrono::seconds(10)),
            "the worker died at the caller's deadline, not the task deadline");
  }

  // A queued caller gives up at its deadline; cancelling the running call
  // kills its worker and frees the slot.
  {
    long failed_before = service.failed.load();
    grpc::ClientContext running;
    grpc::Status running_status;
    std::thread holder([&] { running_status = upload_with(channel, &running); });
    require(hanging_worker_started(config.worker_path),
            "the running call holds the only slot");
    long rejected_before = service.rejected.load();
    grpc::ClientContext queued;
    queued.set_deadline(std::chrono::system_clock::now()
                        + std::chrono::milliseconds(1000));
    grpc::Status queued_status = upload_with(channel, &queued);
    require(queued_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED,
            "the queued call ends at its deadline");
    require(counter_moves(service.rejected, rejected_before,
                          std::chrono::seconds(10)),
            "the slot wait gave up at the queued caller's deadline");
    running.TryCancel();
    holder.join();
    require(running_status.error_code() == grpc::StatusCode::CANCELLED,
            "the cancelled call ends cancelled");
    require(counter_moves(service.failed, failed_before,
                          std::chrono::seconds(10)),
            "cancelling the running call killed its worker");
  }

  // ToDocument only writes at the end, so only the run's cancellation
  // probe can notice the caller leaving.
  {
    long before = service.failed.load();
    grpc::ClientContext context;
    officev1::ToDocumentResponse mapped;
    grpc::Status status;
    std::thread caller([&] {
      auto stub = officev1::OfficeRenderService::NewStub(channel);
      auto writer = stub->ToDocument(&context, &mapped);
      officev1::StreamPagesRequest request;
      request.mutable_chunk()->set_filename("hang.txt");
      request.mutable_chunk()->set_data("hang");
      request.mutable_chunk()->set_complete(true);
      writer->Write(request);
      writer->WritesDone();
      status = writer->Finish();
    });
    require(hanging_worker_started(config.worker_path),
            "the ToDocument call got as far as its worker");
    const auto cancelled_at = std::chrono::steady_clock::now();
    context.TryCancel();
    caller.join();
    require(status.error_code() == grpc::StatusCode::CANCELLED,
            "the cancelled ToDocument call ends cancelled");
    require(counter_moves(service.failed, before, std::chrono::seconds(10)),
            "cancelling ToDocument killed its worker");
    require(std::chrono::steady_clock::now() - cancelled_at
                < std::chrono::seconds(10),
            "ToDocument's worker died promptly after the cancel");
  }
  server->Shutdown();
  std::filesystem::remove(config.worker_path);
  std::filesystem::remove(config.worker_path + ".started");
}

// Upload bytes held by in-flight requests count against one server-wide
// buffer: while one upload holds most of it, another is refused with
// RESOURCE_EXHAUSTED before it is buffered, and the bytes come back when
// the holder's request ends.
void verify_upload_buffer_is_capped() {
  grlibre::ServiceConfig config;
  config.worker_path = "/nonexistent/grlibre-worker";
  config.install_path = "/nonexistent";
  config.max_document_bytes = 1 << 20;
  config.max_buffered_upload_bytes = 1 << 20;
  config.max_concurrent_documents = 2;
  grlibre::RenderServiceImpl service(config);
  std::shared_ptr<grpc::Channel> channel;
  auto server = start_server(&service, &channel);
  const std::string chunk(700 * 1024, 'x');

  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext holder_context;
  auto holder = stub->StreamPages(&holder_context);
  officev1::StreamPagesRequest first;
  first.mutable_chunk()->set_filename("held.zzz");
  first.mutable_chunk()->set_data(chunk);
  require(holder->Write(first), "the holder's first chunk is sent");
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (service.buffered_upload_bytes.load() < static_cast<long>(chunk.size())
         && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  require(service.buffered_upload_bytes.load() == static_cast<long>(chunk.size()),
          "the holder's chunk counts against the buffer");

  auto refused = stream_pages(channel, chunk, "second.zzz", true);
  require(refused.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED,
          "an upload past the shared buffer is RESOURCE_EXHAUSTED");
  require(refused.status.error_message().contains("upload buffer"),
          "the refusal names the upload buffer, got: "
              + refused.status.error_message());

  officev1::StreamPagesRequest last;
  last.mutable_chunk()->set_complete(true);
  holder->Write(last);
  holder->WritesDone();
  officev1::StreamPagesResponse ignored;
  while (holder->Read(&ignored)) {}
  require(holder->Finish().error_code() == grpc::StatusCode::INVALID_ARGUMENT,
          "the holder ends on its unresolvable format");

  require(service.buffered_upload_bytes.load() == 0,
          "every request returned its buffered bytes");
  auto after = stream_pages(channel, chunk, "third.zzz", true);
  require(after.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
          "the buffer is released when the holder's request ends");
  server->Shutdown();
}

}  // namespace

int main() {
  const char* worker = std::getenv("GRLIBRE_WORKER");
  require(worker != nullptr, "GRLIBRE_WORKER must point at the worker binary");

  grlibre::ServiceConfig config;
  config.worker_path = worker;
  config.install_path = "/usr/lib/libreoffice/program";
  config.max_document_bytes = 1 << 20;
  config.max_concurrent_documents = 2;
  config.task_deadline = std::chrono::milliseconds(120000);
  config.render_dpi = 96;
  grlibre::RenderServiceImpl service(config);

  int port = 0;
  grpc::ServerBuilder builder;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();
  require(server != nullptr, "server starts");
  auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                     grpc::InsecureChannelCredentials());

  // Capability discovery works without LibreOffice.
  {
    auto stub = officev1::OfficeRenderService::NewStub(channel);
    grpc::ClientContext context;
    officev1::GetServiceInfoResponse info;
    require(stub->GetServiceInfo(&context, officev1::GetServiceInfoRequest(), &info).ok(),
            "GetServiceInfo ok");
    require(info.max_document_bytes() == (1 << 20), "cap reported");
    require(info.render_dpi() == 96, "dpi reported");
    require(info.supported_formats_size() > 20, "formats reported");
    require(info.diskless_documents(), "diskless posture advertised");
    require(info.internal_temp_artifacts_size() == 3,
            "every LibreOffice-internal temp artifact class named");
    require(info.internal_temp_artifacts(0).contains("odf-load"),
            "ODF load residual named");
    require(info.internal_temp_artifacts(1).contains("embedded-media"),
            "embedded media residual named");
    require(info.internal_temp_artifacts(2).contains("pdf-export"),
            "PDF export residual named");
    require(!std::ranges::contains(info.supported_formats(), std::string("pdf")),
            "PDF is not advertised as a source format");
    require(info.document_mapping(), "ToDocument advertised");
    require(info.package_repair(), "package repair advertised");
    require(info.service_version() == GRLIBRE_VERSION,
            "service version is the build version");
    require(info.ui().title() == "LibreOffice", "ui title advertised");
    require(info.ui().path() == "/ui/libreoffice", "ui path advertised");
    require(info.ui().description() ==
                "Renders office documents via LibreOfficeKit; pages out as PNG",
            "ui description advertised");
  }

  // PDF input is refused before any worker spawns, recognized by the
  // filename extension, by the content type when the filename has no
  // known extension, and by the %PDF- signature whatever the name says.
  {
    const std::string pdf_bytes = "%PDF-1.7\n%fake body\n";
    auto by_name = stream_pages(channel, "not really a pdf", "report.PDF", true);
    require(by_name.status.error_code() == grpc::StatusCode::UNIMPLEMENTED,
            "a .pdf filename is UNIMPLEMENTED");
    require(by_name.status.error_message().contains("PDF input is not supported"),
            "the refusal says PDF input is not supported");
    auto by_signature = stream_pages(channel, pdf_bytes, "disguised.docx", true);
    require(by_signature.status.error_code() == grpc::StatusCode::UNIMPLEMENTED,
            "PDF bytes behind an office extension are UNIMPLEMENTED");
    auto stub = officev1::OfficeRenderService::NewStub(channel);
    grpc::ClientContext context;
    auto stream = stub->ConvertToPdf(&context);
    officev1::ConvertToPdfRequest request;
    request.mutable_chunk()->set_filename("upload");
    request.mutable_chunk()->set_content_type("application/pdf; charset=binary");
    request.mutable_chunk()->set_data("not really a pdf");
    request.mutable_chunk()->set_complete(true);
    stream->Write(request);
    stream->WritesDone();
    officev1::ConvertToPdfResponse ignored;
    while (stream->Read(&ignored)) {}
    require(stream->Finish().error_code() == grpc::StatusCode::UNIMPLEMENTED,
            "an application/pdf content type is UNIMPLEMENTED");
  }

  // Protocol error paths, no office core involved.
  {
    auto result = stream_pages(channel, "data", "mystery.zzz", true);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "unknown format is INVALID_ARGUMENT");
  }
  {
    auto result = stream_pages(channel, "data", "a.txt", false);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "missing complete flag is INVALID_ARGUMENT");
  }
  {
    auto result = stream_pages(channel, "", "a.txt", true);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "empty stream is INVALID_ARGUMENT");
  }
  {
    std::string oversize((1 << 20) + 1, 'x');
    auto result = stream_pages(channel, oversize, "big.txt", true);
    require(result.status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED,
            "oversize is RESOURCE_EXHAUSTED");
  }
  {
    officev1::StreamOptions extra;
    extra.set_timeout_seconds(601);
    auto result = stream_pages(channel, "data", "a.txt", true, false, 0, 0, 0,
                               0, 0, &extra);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "timeout_seconds over 600 is INVALID_ARGUMENT");
  }
  {
    officev1::StreamOptions extra;
    extra.set_max_width_px(9000);
    auto result = stream_pages(channel, "data", "a.txt", true, false, 0, 0, 0,
                               0, 0, &extra);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "max_width_px over 8192 is INVALID_ARGUMENT");
  }
  {
    officev1::StreamOptions extra;
    officev1::TextSpan* span = extra.add_redact_spans();
    span->set_char_start(10);
    span->set_char_end(5);
    auto result = stream_pages(channel, "data", "a.txt", true, false, 0, 0, 0,
                               0, 0, &extra);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "backwards redact span is INVALID_ARGUMENT");
  }
  {
    officev1::StreamOptions extra;
    officev1::TextSpan* span = extra.add_redact_spans();
    span->set_char_start(-1);
    span->set_char_end(5);
    auto result = stream_pages(channel, "data", "a.txt", true, false, 0, 0, 0,
                               0, 0, &extra);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "negative redact span start is INVALID_ARGUMENT");
  }
  {
    // The PDF page range shares the StreamPages validation.
    auto status = convert_to_pdf(channel, "data", "a.txt", 3, 1);
    require(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "backwards pdf page range is INVALID_ARGUMENT");
  }
  {
    // ToDocument shares the option validation too.
    officev1::StreamOptions options;
    options.set_timeout_seconds(601);
    officev1::ToDocumentResponse mapped;
    auto status = to_document(channel, "data", "a.txt", &options, &mapped);
    require(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "ToDocument rejects timeout_seconds over 600");
  }
  {
    auto status = convert_to_pdf(channel, "data", "a.txt", 0, 0, nullptr, 601);
    require(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "ConvertToPdf timeout_seconds over 600 is INVALID_ARGUMENT");
  }

  verify_callers_that_leave_stop_costing();
  verify_upload_buffer_is_capped();

  if (!std::filesystem::exists(config.install_path)) {
    std::println(stderr, "SKIP remainder: no LibreOffice at {}",
                 config.install_path);
    server->Shutdown();
    return 77;
  }

  // Happy path through a real worker and office core.
  {
    auto result = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true);
    require(result.status.ok(), "txt renders: " + result.status.error_message());
    require(result.info.document_id() == "test-doc", "document id echoed");
    require(result.info.document_type() == "text", "document type");
    require(result.pages >= 1, "pages emitted");
    require(result.got_metadata, "metadata event relayed through the service");
    require(result.paragraphs >= 1, "paragraph events relayed through the service");
    require(result.got_status, "final status emitted");
  }

  // Per-request document passwords ride the call metadata. Over-bound
  // metadata is refused before any upload is buffered; an encrypted docx
  // with only wrong candidates is a load failure that says how many were
  // tried and never which; the right candidate, on either key, renders it.
  {
    std::vector<std::pair<std::string, std::string>> too_many;
    for (int i = 0; i < 17; i++) {
      too_many.emplace_back("document-password", "p" + std::to_string(i));
    }
    auto result = stream_pages(channel, "Hello.\n", "a.txt", true, false, 0, 0, 0, 0, 0,
                               nullptr, too_many);
    require(result.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "more than 16 passwords is INVALID_ARGUMENT");
    require(result.status.error_message().find("p16") == std::string::npos,
            "the bound error carries no candidate");
    auto too_long = stream_pages(channel, "Hello.\n", "a.txt", true, false, 0, 0, 0, 0, 0,
                                 nullptr, {{"document-password-bin", std::string(1025, 'x')}});
    require(too_long.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "a password over 1024 bytes is INVALID_ARGUMENT");
  }
  {
    const std::string locked(kEncryptedDocx, sizeof kEncryptedDocx - 1);
    auto wrong = stream_pages(channel, locked, "locked.docx", true, false, 0, 0, 0, 0, 0,
                              nullptr,
                              {{"document-password", "wrong-one"},
                               {"document-password-bin", "wr\xC3\xB6ng-two"}});
    require(wrong.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "wrong passwords are INVALID_ARGUMENT: " + wrong.status.error_message());
    const std::string& message = wrong.status.error_message();
    require(message.find("password-protected") != std::string::npos &&
                message.find("none of the 2 supplied passwords") != std::string::npos,
            "the refusal names the attempt: " + message);
    require(message.find("wrong-one") == std::string::npos &&
                message.find("ng-two") == std::string::npos,
            "the refusal carries no candidate");
    require(wrong.pages == 0, "no pages before a password refusal");
    auto right = stream_pages(channel, locked, "locked.docx", true, false, 0, 0, 0, 0, 0,
                              nullptr,
                              {{"document-password", "wrong-one"},
                               {"document-password-bin", kEncryptedDocxPassword}});
    require(right.status.ok(), "the right password renders: " + right.status.error_message());
    require(right.pages >= 1 && right.paragraphs >= 1, "decrypted pages and paragraphs");
  }

  // The per-request DPI override: a 48-dpi render of the same document must
  // report 48 in PageImage.dpi and paint half the pixels per side of the
  // server's 96-dpi default. Out-of-range values clamp to [24, 600] instead
  // of failing or being forwarded raw.
  {
    auto base = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true);
    require(base.status.ok(), "dpi baseline renders");
    require(base.first_page_dpi == 96, "baseline uses the configured dpi");

    auto half = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                             false, 48);
    require(half.status.ok(), "dpi override renders");
    require(half.first_page_dpi == 48, "override dpi reported per page");
    require(half.first_page_width_px * 2 == base.first_page_width_px,
            "48-dpi page paints half the pixels per side of the 96-dpi page");

    auto low = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                            false, 1);
    require(low.status.ok(), "under-range dpi renders");
    require(low.first_page_dpi == 24, "under-range dpi clamps to the floor");

    auto high = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                             false, 100000);
    require(high.status.ok(), "over-range dpi renders");
    require(high.first_page_dpi <= 600,
            "over-range dpi clamps to the ceiling (pixel bound may lower it)");
    require(high.first_page_width_px > base.first_page_width_px,
            "clamped high dpi still paints more pixels than the default");
  }

  // The page range: a three-page text document (form feeds break pages in
  // the Writer text import) painted with first_page/last_page restrictions.
  // The range trims only PageImage events; DocumentInfo keeps the full
  // count, emitted indexes stay document-absolute, a range past the end is
  // an empty but successful render, and a backwards range is rejected
  // before any worker spawns.
  {
    const std::string three_pages = "Page one.\fPage two.\fPage three.\n";
    auto all = stream_pages(channel, three_pages, "multi.txt", true);
    require(all.status.ok(), "three-page baseline renders");
    require(all.pages == 3, "baseline paints every page");
    require(all.info.page_count() == 3, "baseline page count");

    auto middle = stream_pages(channel, three_pages, "multi.txt", true, false,
                               0, 2, 2);
    require(middle.status.ok(), "ranged render ok");
    require(middle.pages == 1, "range 2:2 paints exactly one page");
    require(middle.page_indexes == std::vector<int>{1},
            "ranged page keeps its document-absolute index");
    require(middle.info.page_count() == 3,
            "ranged DocumentInfo keeps the full page count");
    require(middle.paragraphs == all.paragraphs,
            "typed content is unaffected by the page range");

    auto tail = stream_pages(channel, three_pages, "multi.txt", true, false,
                             0, 2, 0);
    require(tail.status.ok(), "open-ended range ok");
    require(tail.page_indexes == (std::vector<int>{1, 2}),
            "open-ended range paints from first_page to the end");

    auto beyond = stream_pages(channel, three_pages, "multi.txt", true, false,
                               0, 7, 9);
    require(beyond.status.ok(), "past-the-end range is not an error");
    require(beyond.pages == 0, "past-the-end range paints nothing");
    require(beyond.got_status, "past-the-end range still ends with status");

    auto backwards = stream_pages(channel, three_pages, "multi.txt", true,
                                  false, 0, 3, 2);
    require(backwards.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "backwards range is INVALID_ARGUMENT");
  }

  // Page image format selection: the default is PNG and says so, JPEG and
  // WebP carry their magic bytes and name themselves, and the bad-input
  // doors (out-of-range quality, unknown format value) reject before any
  // worker spawns.
  {
    auto png = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true);
    require(png.first_page_format == officev1::PAGE_IMAGE_FORMAT_PNG,
            "default format names itself PNG");
    require(png.first_page_bytes.starts_with("\x89PNG"), "PNG magic");

    auto jpeg = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                             false, 0, 0, 0, officev1::PAGE_IMAGE_FORMAT_JPEG);
    require(jpeg.status.ok(), "jpeg renders: " + jpeg.status.error_message());
    require(jpeg.first_page_format == officev1::PAGE_IMAGE_FORMAT_JPEG,
            "jpeg format named");
    require(jpeg.first_page_bytes.size() > 2
                && static_cast<unsigned char>(jpeg.first_page_bytes[0]) == 0xff
                && static_cast<unsigned char>(jpeg.first_page_bytes[1]) == 0xd8,
            "JPEG magic");

    auto webp = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                             false, 0, 0, 0, officev1::PAGE_IMAGE_FORMAT_WEBP,
                             60);
    require(webp.status.ok(), "webp renders: " + webp.status.error_message());
    require(webp.first_page_format == officev1::PAGE_IMAGE_FORMAT_WEBP,
            "webp format named");
    require(webp.first_page_bytes.size() > 12
                && webp.first_page_bytes.starts_with("RIFF")
                && webp.first_page_bytes.compare(8, 4, "WEBP") == 0,
            "WebP magic");
    require(webp.first_page_bytes.size() < png.first_page_bytes.size(),
            "lossy page is smaller than the PNG page");

    auto bad_quality = stream_pages(channel, "Hello over gRPC.\n", "hello.txt",
                                    true, false, 0, 0, 0,
                                    officev1::PAGE_IMAGE_FORMAT_JPEG, 101);
    require(bad_quality.status.error_code()
                == grpc::StatusCode::INVALID_ARGUMENT,
            "quality over 100 is INVALID_ARGUMENT");

    auto bad_format = stream_pages(channel, "Hello over gRPC.\n", "hello.txt",
                                   true, false, 0, 0, 0, 99);
    require(bad_format.status.error_code()
                == grpc::StatusCode::INVALID_ARGUMENT,
            "unknown format value is INVALID_ARGUMENT");
  }

  // An HTML upload once failed at the finish line: LibreOffice's exit-time
  // teardown crashed after every HTML render, mapping a complete stream to
  // INTERNAL. The worker's _exit teardown keeps the RPC OK with the full
  // stream.
  {
    auto result = stream_pages(
        channel, "<html><body><h1>T</h1><p>Hello over HTML.</p></body></html>\n",
        "page.html", true);
    require(result.status.ok(), "html renders: " + result.status.error_message());
    require(result.info.document_type() == "text", "html document type");
    require(result.pages >= 1, "html pages emitted");
    require(result.got_status, "html final status emitted");
  }

  // Fit-to-width, grayscale, and SVG vector pages. max_width_px scales the
  // page to that many pixels (still PNG), grayscale keeps PNG magic, and
  // PAGE_IMAGE_FORMAT_SVG puts an SVG document in the png field.
  {
    officev1::StreamOptions width;
    width.set_max_width_px(200);
    auto fitted = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                               false, 0, 0, 0, 0, 0, &width);
    require(fitted.status.ok(),
            "fit-to-width renders: " + fitted.status.error_message());
    require(fitted.first_page_width_px > 0
                && fitted.first_page_width_px <= 200,
            "max_width_px 200 paints at most 200 px wide");

    officev1::StreamOptions gray;
    gray.set_grayscale(true);
    auto grey = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                             false, 0, 0, 0, 0, 0, &gray);
    require(grey.status.ok(),
            "grayscale renders: " + grey.status.error_message());
    require(grey.first_page_bytes.starts_with("\x89PNG"),
            "grayscale still encodes PNG");

    officev1::StreamOptions svg;
    svg.set_vector_format(officev1::PAGE_VECTOR_FORMAT_SVG);
    auto vector = stream_pages(channel, "Hello over gRPC.\n", "hello.txt", true,
                               false, 0, 0, 0, 0, 0, &svg);
    require(vector.status.ok(),
            "svg renders: " + vector.status.error_message());
    require(vector.first_page_format == officev1::PAGE_IMAGE_FORMAT_SVG,
            "svg format named");
    require(vector.first_page_bytes.contains("<svg"),
            "svg payload contains an <svg tag");

    // The page_format enum door (what clients send) must take the same
    // SVG path as the dedicated vector_format field.
    auto svg_fmt = stream_pages(channel, "Hello over gRPC.\n", "hello.txt",
                                true, false, 0, 0, 0,
                                officev1::PAGE_IMAGE_FORMAT_SVG);
    require(svg_fmt.status.ok(),
            "page_format SVG renders: " + svg_fmt.status.error_message());
    require(svg_fmt.first_page_format == officev1::PAGE_IMAGE_FORMAT_SVG,
            "page_format SVG names itself");
    require(svg_fmt.first_page_bytes.contains("<svg"),
            "page_format SVG payload is an SVG document");
  }

  // Grayscale of a colored page must change the painted bytes. Black text
  // on white is already gray, so this uses a red HTML paragraph.
  {
    const char* html =
        "<html><body><p style='color:#ff0000;font-size:36pt'>RED</p>"
        "</body></html>\n";
    auto color = stream_pages(channel, html, "red.html", true);
    require(color.status.ok(),
            "color html renders: " + color.status.error_message());
    officev1::StreamOptions gray;
    gray.set_grayscale(true);
    auto grey = stream_pages(channel, html, "red.html", true, false, 0, 0, 0,
                             0, 0, &gray);
    require(grey.status.ok(),
            "grayscale html renders: " + grey.status.error_message());
    require(grey.first_page_bytes.starts_with("\x89PNG"),
            "grayscale html still encodes PNG");
    require(grey.first_page_bytes != color.first_page_bytes,
            "grayscale changes the painted pixels of a colored page");
  }

  // ToDocument folds the same StreamPages event stream into one Document.
  // By default page images stay out of the mapped document; explicitly
  // selecting DOCUMENT_PART_PAGES embeds them as data URIs.
  {
    officev1::ToDocumentResponse mapped;
    grpc::Status status = to_document(channel, "Hello over ToDocument.\n",
                                      "hello.txt", nullptr, &mapped);
    require(status.ok(), "ToDocument ok: " + status.error_message());
    require(mapped.document_info().document_id() == "to-doc",
            "ToDocument echoes document id");
    require(mapped.document().has_body() || mapped.document().pages_size() > 0
                || mapped.document().texts_size() > 0,
            "ToDocument returns a mapped document");
    require(mapped.document().texts_size() > 0,
            "ToDocument maps typed content by default");
    require(mapped.status().state() == officev1::RenderStatus::STATE_OK,
            "ToDocument status is OK");
    for (const auto& entry : mapped.document().pages()) {
      require(entry.second.image().uri().empty(),
              "default ToDocument embeds no page images");
    }

    officev1::StreamOptions with_pages;
    with_pages.add_parts(officev1::DOCUMENT_PART_PAGES);
    officev1::ToDocumentResponse mapped_pages;
    status = to_document(channel, "Hello over ToDocument.\n", "hello.txt",
                         &with_pages, &mapped_pages);
    require(status.ok(),
            "ToDocument with pages ok: " + status.error_message());
    bool embedded = false;
    for (const auto& entry : mapped_pages.document().pages()) {
      if (entry.second.image().uri().starts_with("data:image/png")) {
        embedded = true;
      }
    }
    require(embedded, "explicit DOCUMENT_PART_PAGES embeds page images");
  }

  // Options merge across the upload stream: extras sent on a later chunk
  // still apply (first nonzero / first true wins).
  {
    auto stub = officev1::OfficeRenderService::NewStub(channel);
    grpc::ClientContext context;
    auto stream = stub->StreamPages(&context);
    const std::string text = "Options on the second chunk.\n";
    officev1::StreamPagesRequest first;
    officev1::DocumentChunk* chunk = first.mutable_chunk();
    chunk->set_document_id("late-options");
    chunk->set_filename("late.txt");
    chunk->set_data(text.substr(0, 10));
    require(stream->Write(first), "first chunk writes");
    officev1::StreamPagesRequest second;
    second.mutable_options()->set_max_width_px(200);
    second.mutable_options()->set_grayscale(true);
    chunk = second.mutable_chunk();
    chunk->set_data(text.substr(10));
    chunk->set_complete(true);
    require(stream->Write(second), "second chunk writes");
    stream->WritesDone();
    int first_width = 0;
    officev1::StreamPagesResponse response;
    while (stream->Read(&response)) {
      if (response.has_page_image() && first_width == 0) {
        first_width = response.page_image().width_px();
      }
    }
    grpc::Status status = stream->Finish();
    require(status.ok(), "late options render ok: " + status.error_message());
    require(first_width > 0 && first_width <= 200,
            "max_width_px from a later chunk still applies");
  }

  // Redaction through the service: a refusal reaches the caller as
  // FAILED_PRECONDITION carrying the worker's reason, and a redaction that
  // succeeds leaves the redacted text out of the ToDocument result too.
  {
    officev1::StreamOptions spreadsheet;
    spreadsheet.add_redact_spans()->set_char_end(4);
    auto refused = stream_pages(channel, "a,b\nc,d\n", "sheet.csv", true,
                                false, 0, 0, 0, 0, 0, &spreadsheet);
    require(refused.status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
            "a spreadsheet redaction is FAILED_PRECONDITION");
    require(refused.status.error_message().contains("only text documents"),
            "the refusal carries the worker's reason, got: "
                + refused.status.error_message());
    require(refused.pages == 0 && !refused.got_status,
            "a refused redaction streams nothing");

    const std::string text = "Name: SECRET-VALUE.\nAgain SECRET-VALUE here.\n";
    officev1::StreamOptions redact;
    officev1::TextSpan* span = redact.add_redact_spans();
    span->set_char_start(6);
    span->set_char_end(18);
    officev1::ToDocumentResponse mapped;
    grpc::Status status = to_document(channel, text, "names.txt", &redact, &mapped);
    require(status.ok(), "redacted ToDocument ok: " + status.error_message());
    require(!mapped.document().DebugString().contains("SECRET-VALUE"),
            "the mapped document carries no redacted text");
    require(mapped.document().DebugString().contains("Again"),
            "the mapped document keeps the rest of the text");
  }

  // A repairable broken package (a stored-entry OOXML zip truncated before
  // its central directory) maps to the repair statuses: refusal naming the
  // opt-in by default; opted-in repair is attempted and never UNIMPLEMENTED.
  {
    std::string broken(kRepairableDocx, sizeof kRepairableDocx - 1);
    auto refused = stream_pages(channel, broken, "broken.docx", true);
    require(refused.status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
            "broken package without the opt-in is FAILED_PRECONDITION");
    require(refused.status.error_message().contains("allow_package_repair"),
            "refusal names the opt-in field");
    auto opted = stream_pages(channel, broken, "broken.docx", true, true);
    require(opted.status.error_code() != grpc::StatusCode::UNIMPLEMENTED,
            "opted-in repair is no longer UNIMPLEMENTED");
    require(opted.status.ok()
                || opted.status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
            "opted-in repair either loads or fails as a load error");
  }

  server->Shutdown();

  // SIGTERM must shut the real binary down cleanly: the handler only pokes
  // an eventfd and the actual grpc Shutdown runs on a plain thread, so the
  // exit is orderly (code 0) instead of a signal-context deadlock gamble.
  // Runs against the built server because main() owns the signal wiring.
  {
    const char* server_bin = std::getenv("GRLIBRE_SERVER");
    require(server_bin != nullptr, "GRLIBRE_SERVER must point at the server binary");
    // Grab an ephemeral port and hand it to the child; the close-to-exec
    // reuse window is a benign test-only race.
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    require(probe >= 0, "probe socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(probe, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0,
            "probe bind");
    socklen_t addr_len = sizeof addr;
    require(::getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0,
            "probe getsockname");
    int free_port = ntohs(addr.sin_port);
    ::close(probe);

    pid_t pid = ::fork();
    require(pid >= 0, "fork server");
    if (pid == 0) {
      ::setenv("GRLIBRE_PORT", std::to_string(free_port).c_str(), 1);
      ::setenv("GRLIBRE_METRICS_INTERVAL_SECONDS", "0", 1);
      ::execl(server_bin, server_bin, nullptr);
      ::_exit(127);
    }
    bool up = false;
    for (int attempt = 0; attempt < 300 && !up; attempt++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      int s = ::socket(AF_INET, SOCK_STREAM, 0);
      addr.sin_port = htons(free_port);
      up = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
      ::close(s);
      int status = 0;
      require(::waitpid(pid, &status, WNOHANG) == 0,
              "server stays up while the test waits for its port");
    }
    require(up, "server came up on its port");
    require(::kill(pid, SIGTERM) == 0, "SIGTERM sent");
    int status = 0;
    bool exited = false;
    for (int attempt = 0; attempt < 100 && !exited; attempt++) {
      exited = ::waitpid(pid, &status, WNOHANG) == pid;
      if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) ::kill(pid, SIGKILL);
    require(exited, "server exited within the bound after SIGTERM");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "SIGTERM exit is an orderly code 0");
  }

  std::println("render-service-test passed");
  return 0;
}
