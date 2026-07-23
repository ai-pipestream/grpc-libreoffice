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
  bool got_status = false;
};

StreamResult stream_pages(const std::shared_ptr<grpc::Channel>& channel,
                          const std::string& bytes, const std::string& filename,
                          bool mark_complete) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
  auto stream = stub->StreamPages(&context);
  size_t chunk_size = 64 * 1024;
  for (size_t offset = 0; offset < bytes.size() || offset == 0; offset += chunk_size) {
    officev1::StreamPagesRequest request;
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
    require(result.got_status, "final status emitted");
  }

  server->Shutdown();
  std::cout << "render-service-test passed\n";
  return 0;
}
