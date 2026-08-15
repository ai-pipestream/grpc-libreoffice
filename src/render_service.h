#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ai/pipestream/office/v1/office_service.grpc.pb.h"

namespace grlibre {

// Bounds for the render DPI, shared by the GRLIBRE_RENDER_DPI environment
// default and the per-request StreamOptions.render_dpi override.
inline constexpr int kMinRenderDpi = 24;
inline constexpr int kMaxRenderDpi = 600;
inline constexpr int kMaxTimeoutSeconds = 600;
inline constexpr int kMaxWidthPx = 8192;

// Server-side configuration shared by every request.
struct ServiceConfig {
  std::string worker_path;
  std::string install_path;
  long max_document_bytes = 100L * 1024 * 1024;
  int max_concurrent_documents = 2;
  std::chrono::milliseconds task_deadline{120000};
  int render_dpi = 144;
  int max_side_px = 4096;
  std::string libreoffice_version = "unknown";
  // RAM-backed directory that receives the per-worker work dirs. Uploaded
  // document bytes and the office core's spills live under it and must
  // never reach disk; the worker refuses a work dir that is not tmpfs.
  std::string tmpfs_dir = "/dev/shm";
};

// The gRPC face over the worker processes. Uploads accumulate in memory
// under the byte cap; each completed upload renders in its own worker
// process, bounded by a concurrency gate.
class RenderServiceImpl final
    : public ai::pipestream::office::v1::OfficeRenderService::Service {
 public:
  explicit RenderServiceImpl(ServiceConfig config);

  grpc::Status StreamPages(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<ai::pipestream::office::v1::StreamPagesResponse,
                               ai::pipestream::office::v1::StreamPagesRequest>* stream) override;

  grpc::Status ConvertToPdf(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<ai::pipestream::office::v1::ConvertToPdfResponse,
                               ai::pipestream::office::v1::ConvertToPdfRequest>* stream) override;

  grpc::Status GetServiceInfo(
      grpc::ServerContext* context,
      const ai::pipestream::office::v1::GetServiceInfoRequest* request,
      ai::pipestream::office::v1::GetServiceInfoResponse* response) override;

  grpc::Status ToDocument(
      grpc::ServerContext* context,
      grpc::ServerReader<ai::pipestream::office::v1::StreamPagesRequest>* reader,
      ai::pipestream::office::v1::ToDocumentResponse* response) override;

  // Documents fully rendered / rejected before render / failed in render.
  std::atomic<long> rendered{0};
  std::atomic<long> rejected{0};
  std::atomic<long> failed{0};

 private:
  // Blocks until a render slot frees up; RAII-released.
  class SlotGuard;

  // default_parts is the worker parts token used when no request in the
  // upload stream selected parts: "all" for the streaming RPCs,
  // "all-but-pages" for ToDocument (page images are omitted from the mapped
  // document unless explicitly selected).
  template <typename Response, typename Request, typename In>
  grpc::Status render(const char* mode, In* in,
                      const std::function<bool(Response&&)>& write,
                      const char* default_parts = "all");

  ServiceConfig config_;
  std::vector<std::string> supported_formats_;
  std::mutex slots_mutex_;
  std::condition_variable slots_available_;
  int busy_slots_ = 0;
};

}  // namespace grlibre
