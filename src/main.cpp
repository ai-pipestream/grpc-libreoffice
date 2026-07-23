// grpc-libreoffice server: gRPC front over per-document LibreOfficeKit
// worker processes. All writable paths live under TMPDIR; the container
// runs read-only with a tmpfs at /tmp.

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <signal.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "render_service.h"

namespace {

int int_from_env(const char* name, int fallback, int min, int max) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || *raw == '\0') return fallback;
  char* end = nullptr;
  long value = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0') {
    throw std::invalid_argument(std::string(name) + " must be an integer, got \"" + raw + "\"");
  }
  if (value < min || value > max) {
    throw std::invalid_argument(std::string(name) + " must be between "
        + std::to_string(min) + " and " + std::to_string(max)
        + ", got " + std::to_string(value));
  }
  return static_cast<int>(value);
}

std::string sibling_binary(const char* name) {
  char self[PATH_MAX];
  ssize_t length = ::readlink("/proc/self/exe", self, sizeof self - 1);
  if (length <= 0) return name;
  self[length] = '\0';
  std::string path(self);
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? name : path.substr(0, slash + 1) + name;
}

std::string detect_libreoffice_version() {
  FILE* pipe = ::popen("soffice --version 2>/dev/null", "r");
  if (pipe == nullptr) return "unknown";
  char line[256] = {0};
  if (std::fgets(line, sizeof line, pipe) == nullptr) {
    ::pclose(pipe);
    return "unknown";
  }
  ::pclose(pipe);
  std::string version(line);
  while (!version.empty() && (version.back() == '\n' || version.back() == '\r')) {
    version.pop_back();
  }
  return version.empty() ? "unknown" : version;
}

std::unique_ptr<grpc::Server> g_server;

void handle_shutdown(int) {
  if (g_server != nullptr) g_server->Shutdown();
}

}  // namespace

int main() {
  ::signal(SIGPIPE, SIG_IGN);

  grlibre::ServiceConfig config;
  int port;
  int metrics_interval;
  try {
    port = int_from_env("GRLIBRE_PORT", 50053, 1, 65535);
    config.max_document_bytes =
        static_cast<long>(int_from_env("GRLIBRE_MAX_DOCUMENT_MIB", 100, 1, 2048)) << 20;
    config.max_concurrent_documents = int_from_env("GRLIBRE_MAX_CONCURRENT_DOCUMENTS", 2, 1, 64);
    config.task_deadline = std::chrono::milliseconds(
        1000L * int_from_env("GRLIBRE_TASK_TIMEOUT_SECONDS", 120, 5, 3600));
    config.render_dpi = int_from_env("GRLIBRE_RENDER_DPI", 144, 24, 600);
    config.max_side_px = int_from_env("GRLIBRE_MAX_PAGE_PIXELS", 4096, 256, 16384);
    metrics_interval = int_from_env("GRLIBRE_METRICS_INTERVAL_SECONDS", 60, 0, 86400);
  } catch (const std::exception& bad_config) {
    std::cerr << "Startup failed: " << bad_config.what() << "\n";
    return 1;
  }
  const char* lo_path = std::getenv("GRLIBRE_LO_PATH");
  config.install_path = lo_path != nullptr ? lo_path : "/usr/lib/libreoffice/program";
  const char* worker = std::getenv("GRLIBRE_WORKER");
  config.worker_path = worker != nullptr ? worker : sibling_binary("grlibre-worker");
  config.libreoffice_version = detect_libreoffice_version();

  grlibre::RenderServiceImpl service(config);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  grpc::ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:" + std::to_string(port),
                           grpc::InsecureServerCredentials());
  builder.SetMaxReceiveMessageSize(
      static_cast<int>(std::min<long>(INT_MAX, config.max_document_bytes + (1 << 20))));
  builder.RegisterService(&service);
  g_server = builder.BuildAndStart();
  if (g_server == nullptr) {
    std::cerr << "Startup failed: cannot listen on port " << port << "\n";
    return 1;
  }
  std::cout << "grpc-libreoffice listening on " << port
            << " workers=" << config.max_concurrent_documents
            << " dpi=" << config.render_dpi
            << " cap=" << config.max_document_bytes << "B"
            << " core=\"" << config.libreoffice_version << "\"" << std::endl;

  ::signal(SIGINT, handle_shutdown);
  ::signal(SIGTERM, handle_shutdown);

  std::thread metrics;
  if (metrics_interval > 0) {
    metrics = std::thread([&service, metrics_interval] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(metrics_interval));
        std::cout << "grlibre metrics: docs{rendered=" << service.rendered.load()
                  << ",rejected=" << service.rejected.load()
                  << ",failed=" << service.failed.load() << "}" << std::endl;
      }
    });
    metrics.detach();
  }

  g_server->Wait();
  return 0;
}
