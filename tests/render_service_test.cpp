// End-to-end service test over localhost. Protocol error paths run
// everywhere; the happy path needs LibreOffice and is skipped without it.

#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "ai/pipestream/office/v1/office_service.grpc.pb.h"
#include "render_service.h"

namespace {

namespace officev1 = ai::pipestream::office::v1;

void require(bool condition, const std::string& what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
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
                          bool mark_complete, bool allow_package_repair = false) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
  auto stream = stub->StreamPages(&context);
  size_t chunk_size = 64 * 1024;
  for (size_t offset = 0; offset < bytes.size() || offset == 0; offset += chunk_size) {
    officev1::StreamPagesRequest request;
    request.set_allow_package_repair(allow_package_repair);
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
    if (response.has_page_image()) result.pages++;
    if (response.has_paragraph()) result.paragraphs++;
    if (response.has_metadata()) result.got_metadata = true;
    if (response.has_status()) result.got_status = true;
  }
  result.status = stream->Finish();
  return result;
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
    require(info.internal_temp_artifacts_size() == 4,
            "every LibreOffice-internal temp artifact class named");
    require(info.internal_temp_artifacts(0).find("odf-load") != std::string::npos,
            "ODF load residual named");
    require(info.internal_temp_artifacts(1).find("pdf-import") != std::string::npos,
            "PDF import residual named");
    require(info.internal_temp_artifacts(2).find("embedded-media") != std::string::npos,
            "embedded media residual named");
    require(info.internal_temp_artifacts(3).find("pdf-export") != std::string::npos,
            "PDF export residual named");
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

  if (!std::filesystem::exists(config.install_path)) {
    std::cerr << "SKIP remainder: no LibreOffice at " << config.install_path << "\n";
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

  // A repairable broken package (a stored-entry OOXML zip truncated before
  // its central directory) maps to the repair statuses: refusal naming the
  // opt-in by default, UNIMPLEMENTED when opted in, never a silent repair.
  {
    std::string broken(kRepairableDocx, sizeof kRepairableDocx - 1);
    auto refused = stream_pages(channel, broken, "broken.docx", true);
    require(refused.status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
            "broken package without the opt-in is FAILED_PRECONDITION");
    require(refused.status.error_message().find("allow_package_repair") != std::string::npos,
            "refusal names the opt-in field");
    auto opted = stream_pages(channel, broken, "broken.docx", true, true);
    require(opted.status.error_code() == grpc::StatusCode::UNIMPLEMENTED,
            "opted-in repair is UNIMPLEMENTED in this version");
  }

  server->Shutdown();
  std::cout << "render-service-test passed\n";
  return 0;
}
