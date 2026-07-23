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

// A flat ODT with a heading, a bold span, a 2x2 table, and an embedded 1x1
// PNG. Flat XML keeps the fixture diskless and reviewable.
const char kTypedFodt[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:automatic-styles>
  <style:style style:name="B1" style:family="text">
   <style:text-properties fo:font-weight="bold"/>
  </style:style>
  <style:page-layout style:name="pm1">
   <style:page-layout-properties fo:page-width="21cm" fo:page-height="29.7cm"
    fo:margin-top="2cm" fo:margin-bottom="2cm" fo:margin-left="2cm" fo:margin-right="2cm"/>
  </style:page-layout>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Standard" style:page-layout-name="pm1">
   <style:header><text:p>Header text</text:p></style:header>
  </style:master-page>
 </office:master-styles>
 <office:body><office:text>
  <text:h text:outline-level="1">Heading One</text:h>
  <text:p>Body <text:span text:style-name="B1">bold</text:span> text.<text:note text:note-class="footnote" text:id="ftn1"><text:note-citation>1</text:note-citation><text:note-body><text:p>Footnote text.</text:p></text:note-body></text:note></text:p>
  <table:table table:name="T1">
   <table:table-column table:number-columns-repeated="2"/>
   <table:table-row>
    <table:table-cell><text:p>A1v</text:p></table:table-cell>
    <table:table-cell><text:p>B1v</text:p></table:table-cell>
   </table:table-row>
   <table:table-row>
    <table:table-cell><text:p>A2v</text:p></table:table-cell>
    <table:table-cell><text:p>B2v</text:p></table:table-cell>
   </table:table-row>
  </table:table>
  <text:p><draw:frame draw:name="Img1" svg:width="1cm" svg:height="1cm"><draw:image><office:binary-data>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==</office:binary-data></draw:image></draw:frame></text:p>
 </office:text></office:body>
</office:document>
)";

void verify_typed_content() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodt", kTypedFodt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "typed fodt renders ok: " + outcome.detail);
  int pages = 0;
  int metadata = 0;
  int tables = 0;
  int images = 0;
  bool saw_heading = false;
  bool saw_bold_run = false;
  bool table_ok = false;
  bool image_ok = false;
  bool footnote_ok = false;
  bool header_ok = false;
  bool page_style_ok = false;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "typed event parses");
    switch (event.event_case()) {
      case officev1::StreamPagesResponse::kPageImage:
        pages++;
        break;
      case officev1::StreamPagesResponse::kMetadata:
        metadata++;
        break;
      case officev1::StreamPagesResponse::kParagraph: {
        const officev1::Paragraph& para = event.paragraph();
        if (!para.runs().empty() && para.runs(0).text() == "Heading One") {
          saw_heading = para.outline_level() == 1 && para.page_index() == 0 &&
                        !para.runs(0).font().empty() && para.runs(0).size_pt() > 0;
        }
        for (const officev1::TextRun& text_run : para.runs()) {
          if (text_run.text() == "bold" && text_run.weight() >= 150.0f &&
              text_run.char_offset() == 17 && text_run.char_length() == 4) {
            saw_bold_run = true;
          }
        }
        break;
      }
      case officev1::StreamPagesResponse::kFootnote: {
        const officev1::Footnote& note = event.footnote();
        std::string text;
        for (const officev1::TextRun& text_run : note.runs()) {
          text += text_run.text();
        }
        footnote_ok = !note.endnote() && note.label() == "1" &&
                      note.page_index() == 0 && text == "Footnote text." &&
                      (note.runs().empty() || note.runs(0).char_offset() == -1);

        break;
      }
      case officev1::StreamPagesResponse::kHeaderFooter: {
        const officev1::HeaderFooter& header = event.header_footer();
        header_ok = !header.footer() && header.page_style() == "Standard" &&
                    header.paragraphs_size() == 1 &&
                    !header.paragraphs(0).runs().empty() &&
                    header.paragraphs(0).runs(0).text() == "Header text" &&
                    header.paragraphs(0).runs(0).char_offset() == -1;
        break;
      }
      case officev1::StreamPagesResponse::kPageStyle: {
        const officev1::PageStyleInfo& style = event.page_style();
        page_style_ok = style.name() == "Standard" &&
                        style.width_twips() > 10000 &&
                        style.height_twips() > style.width_twips() &&
                        style.margin_left_twips() > 1000 && style.columns() == 1;
        break;
      }
      case officev1::StreamPagesResponse::kTable: {
        tables++;
        const officev1::TableData& table = event.table();
        bool cells_ok = table.cells_size() == 4;
        for (const officev1::TableCellData& cell : table.cells()) {
          if (cell.name() == "B2") {
            cells_ok = cells_ok && cell.row() == 1 && cell.column() == 1 &&
                       cell.text() == "B2v";
          }
        }
        table_ok = table.rows() == 2 && table.columns() == 2 && cells_ok &&
                   table.page_index() == 0 && table.start().y() > 0;
        break;
      }
      case officev1::StreamPagesResponse::kEmbeddedImage: {
        images++;
        const officev1::EmbeddedImage& image = event.embedded_image();
        const std::string& data = image.data();
        image_ok = data.size() > 8 && data[1] == 'P' && data[2] == 'N' &&
                   data[3] == 'G' && image.width_twips() > 0;
        break;
      }
      default:
        break;
    }
  }
  require(pages >= 1, "typed fodt painted a page");
  require(metadata == 1, "one metadata event");
  require(saw_heading, "heading paragraph with outline level and layout data");
  require(saw_bold_run, "bold run detected via numeric weight");
  require(tables == 1 && table_ok, "2x2 table with named, addressed cells");
  require(images == 1 && image_ok, "embedded PNG extracted with layout size");
  require(footnote_ok, "footnote with label, page, and out-of-body offsets");
  require(header_ok, "header content for the Standard page style");
  require(page_style_ok, "page geometry with margins and column count");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "typed last event parses");
  require(last.has_status(), "typed stream ends with status");
  require(last.status().warnings().empty(),
          "typed extraction produced no warnings");
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
  verify_typed_content();
  verify_corrupt_zip_is_load_failure();
  std::cout << "worker-render-test passed\n";
  return 0;
}
