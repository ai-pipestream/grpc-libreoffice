// grpc-libreoffice server: gRPC front over per-document LibreOfficeKit
// worker processes. Every per-document writable path lives under a
// RAM-backed tmpfs (GRLIBRE_TMPFS_DIR, default /dev/shm): uploaded
// document bytes never reach disk, and the server refuses to start
// without a real tmpfs rather than fall back.

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <linux/magic.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/vfs.h>
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

// The handler only pokes an eventfd: grpc::Server::Shutdown allocates and
// takes locks, none of which is async-signal-safe, so the actual shutdown
// runs on a dedicated thread woken by this write.
int g_shutdown_fd = -1;

void handle_shutdown(int) {
  uint64_t one = 1;
  [[maybe_unused]] ssize_t wrote = ::write(g_shutdown_fd, &one, sizeof one);
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
    config.render_dpi = int_from_env("GRLIBRE_RENDER_DPI", 144,
                                     grlibre::kMinRenderDpi,
                                     grlibre::kMaxRenderDpi);
    config.max_side_px = int_from_env("GRLIBRE_MAX_PAGE_PIXELS", 4096, 256, 16384);
    metrics_interval = int_from_env("GRLIBRE_METRICS_INTERVAL_SECONDS", 60, 0, 86400);
  } catch (const std::exception& bad_config) {
    std::cerr << "Startup failed: " << bad_config.what() << "\n";
    return 1;
  }
  const char* lo_path = std::getenv("GRLIBRE_LO_PATH");
  config.install_path = lo_path != nullptr ? lo_path : "/usr/lib/libreoffice/program";
  const char* tmpfs_dir = std::getenv("GRLIBRE_TMPFS_DIR");
  if (tmpfs_dir != nullptr && *tmpfs_dir != '\0') config.tmpfs_dir = tmpfs_dir;
  struct statfs tmpfs_stat;
  if (::statfs(config.tmpfs_dir.c_str(), &tmpfs_stat) != 0
      || tmpfs_stat.f_type != TMPFS_MAGIC) {
    std::cerr << "Startup failed: " << config.tmpfs_dir
              << " is not a tmpfs. Uploaded documents must stay in RAM; "
                 "mount a tmpfs there or point GRLIBRE_TMPFS_DIR at one.\n";
    return 1;
  }
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
            << " tmpfs=" << config.tmpfs_dir
            << " core=\"" << config.libreoffice_version << "\"" << std::endl;

  g_shutdown_fd = ::eventfd(0, EFD_CLOEXEC);
  if (g_shutdown_fd < 0) {
    std::cerr << "Startup failed: cannot create the shutdown eventfd\n";
    return 1;
  }
  std::thread shutdown_thread([] {
    uint64_t value = 0;
    while (::read(g_shutdown_fd, &value, sizeof value) < 0 && errno == EINTR) {}
    g_server->Shutdown();
  });
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
  // Wait() only returns after Shutdown(), so the thread has finished its
  // one read-then-shutdown pass by now.
  shutdown_thread.join();
  return 0;
}
