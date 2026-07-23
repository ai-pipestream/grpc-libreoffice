// Runs the real grlibre-worker binary against a headless LibreOffice.
// Skips (exit 77) when soffice is not installed.

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ai/pipestream/office/v1/office_service.pb.h"
#include "worker_runner.h"

namespace {

namespace officev1 = ai::pipestream::office::v1;

void require(bool condition, const std::string& what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}

std::string worker_path() {
  const char* path = std::getenv("GRLIBRE_WORKER");
  require(path != nullptr, "GRLIBRE_WORKER must point at the worker binary");
  return path;
}

std::string lo_install_path() {
  const char* configured = std::getenv("GRLIBRE_LO_PATH");
  return configured != nullptr ? configured : "/usr/lib/libreoffice/program";
}

std::string make_work_dir() {
  const char* base = std::getenv("TMPDIR");
  std::string pattern = std::string(base != nullptr ? base : "/tmp") + "/grlibre-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  require(::mkdtemp(buffer.data()) != nullptr, "mkdtemp");
  return buffer.data();
}

grlibre::WorkerOutcome run(const std::string& mode, const std::string& extension,
                           const std::string& document,
                           std::vector<std::string>* payloads) {
  std::string work_dir = make_work_dir();
  std::vector<std::string> argv = {
      worker_path(), mode, extension, "96", "2048", work_dir, lo_install_path()};
  grlibre::WorkerOutcome outcome = grlibre::run_worker(
      argv, document, std::chrono::milliseconds(120000), 256u * 1024 * 1024,
      [&](std::string&& payload) {
        payloads->push_back(std::move(payload));
        return true;
      });
  std::error_code ignored;
  std::filesystem::remove_all(work_dir, ignored);
  return outcome;
}

void verify_text_pages() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "txt", "Hello from the render worker.\n", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "txt pages render ok: " + outcome.detail);
  require(payloads.size() >= 3, "info + page + status events");
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "first event parses");
  require(first.has_document_info(), "first event is DocumentInfo");
  require(first.document_info().document_type() == "text", "text document type");
  require(first.document_info().page_count() >= 1, "at least one page");
  officev1::StreamPagesResponse page;
  require(page.ParseFromString(payloads[1]), "second event parses");
  require(page.has_page_image(), "second event is a page");
  const std::string& png = page.page_image().png();
  require(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "page is PNG");
  require(page.page_image().width_px() > 0 && page.page_image().height_px() > 0,
          "page has dimensions");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "last event parses");
  require(last.has_status(), "last event is status");
  require(last.status().state() == officev1::RenderStatus::STATE_OK, "status ok");
  require(last.status().output_bytes() > 0, "output bytes counted");
}

void verify_csv_is_spreadsheet() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "csv", "a,b,c\n1,2,3\n4,5,6\n", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "csv renders ok: " + outcome.detail);
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "csv info parses");
  require(first.document_info().document_type() == "spreadsheet", "csv is spreadsheet");
}

void verify_pdf_mode() {
  std::vector<std::string> payloads;
  auto outcome = run("pdf", "txt", "PDF output please.\n", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "pdf mode ok: " + outcome.detail);
  std::string pdf;
  officev1::ConvertToPdfResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "pdf event parses");
    if (event.has_pdf_chunk()) pdf.append(event.pdf_chunk().data());
  }
  require(pdf.size() > 500, "PDF has substance");
  require(pdf.compare(0, 5, "%PDF-") == 0, "PDF magic");
}

void verify_corrupt_zip_is_load_failure() {
  // Plain ASCII garbage would not do here: the office core content-sniffs
  // it as text and loads it. A broken zip container is genuinely unloadable
  // and, before the Batch load option, hung on the repair interaction.
  std::string corrupt = "PK\x03\x04";
  for (int i = 0; i < 4096; i++) corrupt.push_back(static_cast<char>(i * 131 % 251));
  std::vector<std::string> payloads;
  auto outcome = run("pages", "docx", corrupt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kLoadFailure,
          "corrupt docx is a load failure, got detail: " + outcome.detail);
}

}  // namespace

int main() {
  if (!std::filesystem::exists(lo_install_path())) {
    std::cerr << "SKIP: no LibreOffice at " << lo_install_path() << "\n";
    return 77;
  }
  verify_text_pages();
  verify_csv_is_spreadsheet();
  verify_pdf_mode();
  verify_corrupt_zip_is_load_failure();
  std::cout << "worker-render-test passed\n";
  return 0;
}
