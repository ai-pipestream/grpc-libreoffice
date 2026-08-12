// End-to-end service test over localhost. Protocol error paths run
// everywhere; the happy path needs LibreOffice and is skipped without it.

#include <grpcpp/grpcpp.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
                          int page_quality = 0) {
  auto stub = officev1::OfficeRenderService::NewStub(channel);
  grpc::ClientContext context;
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
    require(png.first_page_bytes.rfind("\x89PNG", 0) == 0, "PNG magic");

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
                && webp.first_page_bytes.compare(0, 4, "RIFF") == 0
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

  std::cout << "render-service-test passed\n";
  return 0;
}
