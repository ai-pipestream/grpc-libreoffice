// Runs the real grlibre-worker binary against a headless LibreOffice.
// Skips (exit 77) when soffice is not installed.

#include <linux/magic.h>
#include <sys/vfs.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ai/pipestream/office/v1/office_service.pb.h"
#include "docling_map.h"
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

// Work dirs live on tmpfs, mirroring the service: the worker refuses a
// disk-backed work dir outright.
std::string make_work_dir() {
  const char* base = std::getenv("GRLIBRE_TMPFS_DIR");
  std::string pattern = std::string(base != nullptr && *base != '\0' ? base : "/dev/shm")
      + "/grlibre-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  require(::mkdtemp(buffer.data()) != nullptr, "mkdtemp");
  return buffer.data();
}

// parts_token is the worker's 8th argv token: "all" or comma-joined
// DocumentPart numbers.
grlibre::WorkerOutcome run_with_parts(const std::string& mode,
                                      const std::string& extension,
                                      const std::string& document,
                                      const std::string& parts_token,
                                      std::vector<std::string>* payloads) {
  std::string work_dir = make_work_dir();
  std::vector<std::string> argv = {
      worker_path(), mode, extension, "96", "2048",
      work_dir, lo_install_path(), parts_token};
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

grlibre::WorkerOutcome run(const std::string& mode, const std::string& extension,
                           const std::string& document,
                           std::vector<std::string>* payloads) {
  return run_with_parts(mode, extension, document, "all", payloads);
}

// Runs the worker with per-request StreamOptions staged as options.pb in
// the work dir, the way the service hands extras to the worker.
grlibre::WorkerOutcome run_with_extras(const std::string& mode,
                                       const std::string& extension,
                                       const std::string& document,
                                       const officev1::StreamOptions& extras,
                                       std::vector<std::string>* payloads,
                                       const std::string& parts_token = "all") {
  std::string work_dir = make_work_dir();
  {
    std::ofstream out(work_dir + "/options.pb", std::ios::binary);
    require(extras.SerializeToOstream(&out), "extras serialize");
  }
  std::vector<std::string> argv = {
      worker_path(), mode, extension, "96", "2048",
      work_dir, lo_install_path(), parts_token};
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

// Folds a pages-mode payload list into its typed pieces.
struct PagesRun {
  officev1::DocumentInfo info;
  std::vector<officev1::PageImage> pages;
  std::vector<officev1::Paragraph> paragraphs;
  officev1::RenderStatus status;
  bool got_status = false;
};

PagesRun fold_pages(const std::vector<std::string>& payloads) {
  PagesRun run;
  for (const std::string& payload : payloads) {
    officev1::StreamPagesResponse event;
    require(event.ParseFromString(payload), "pages event parses");
    if (event.has_document_info()) run.info = event.document_info();
    if (event.has_page_image()) run.pages.push_back(event.page_image());
    if (event.has_paragraph()) run.paragraphs.push_back(event.paragraph());
    if (event.has_status()) {
      run.status = event.status();
      run.got_status = true;
    }
  }
  return run;
}

// Concatenates a pdf-mode payload list into the PDF bytes.
std::string fold_pdf(const std::vector<std::string>& payloads) {
  std::string pdf;
  for (const std::string& payload : payloads) {
    officev1::ConvertToPdfResponse event;
    require(event.ParseFromString(payload), "pdf event parses");
    if (event.has_pdf_chunk()) pdf += event.pdf_chunk().data();
  }
  return pdf;
}

// The DocumentInfo event of a pdf-mode payload list.
officev1::DocumentInfo pdf_info(const std::vector<std::string>& payloads) {
  officev1::DocumentInfo info;
  for (const std::string& payload : payloads) {
    officev1::ConvertToPdfResponse event;
    require(event.ParseFromString(payload), "pdf event parses");
    if (event.has_document_info()) info = event.document_info();
  }
  return info;
}

// Rasterizes a PDF with pdftoppm into a scratch dir and returns one P6
// PPM byte buffer per page. Requires poppler-utils, present in CI and the
// dev image alongside soffice.
std::vector<std::string> rasterize_pdf(const std::string& pdf) {
  std::string work_dir = make_work_dir();
  {
    std::ofstream out(work_dir + "/doc.pdf", std::ios::binary);
    out.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
  }
  std::string command = "pdftoppm -r 100 '" + work_dir + "/doc.pdf' '"
      + work_dir + "/page' >/dev/null 2>&1";
  require(std::system(command.c_str()) == 0, "pdftoppm runs");
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(work_dir)) {
    if (entry.path().extension() == ".ppm") names.push_back(entry.path());
  }
  std::sort(names.begin(), names.end());
  std::vector<std::string> pages;
  for (const std::string& name : names) {
    std::ifstream in(name, std::ios::binary);
    pages.emplace_back(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
  }
  std::error_code ignored;
  std::filesystem::remove_all(work_dir, ignored);
  return pages;
}

// The longest horizontal run of near-black pixels in a P6 PPM. Text
// glyph strokes stay short; only a filled redaction box produces a long
// run.
int longest_black_run(const std::string& ppm) {
  size_t pos = 0;
  auto token = [&]() {
    while (pos < ppm.size() && std::isspace(static_cast<unsigned char>(ppm[pos]))) pos++;
    size_t start = pos;
    while (pos < ppm.size() && !std::isspace(static_cast<unsigned char>(ppm[pos]))) pos++;
    return ppm.substr(start, pos - start);
  };
  require(token() == "P6", "ppm magic");
  const int width = std::atoi(token().c_str());
  const int height = std::atoi(token().c_str());
  require(token() == "255", "ppm depth");
  pos++;  // The single whitespace byte after the header.
  require(ppm.size() - pos >= static_cast<size_t>(width) * height * 3,
          "ppm payload complete");
  int longest = 0;
  for (int y = 0; y < height; y++) {
    int current = 0;
    for (int x = 0; x < width; x++) {
      const unsigned char* p = reinterpret_cast<const unsigned char*>(
          ppm.data() + pos + (static_cast<size_t>(y) * width + x) * 3);
      const bool black = p[0] < 40 && p[1] < 40 && p[2] < 40;
      current = black ? current + 1 : 0;
      if (current > longest) longest = current;
    }
  }
  return longest;
}

// Flat-ODF fixtures: text-based stand-ins for real uploads, exercising
// sheet visibility, used ranges, notes pages, and tracked changes without
// binary blobs.
constexpr char kTwoSheetFods[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 office:version="1.2"
 office:mimetype="application/vnd.oasis.opendocument.spreadsheet">
 <office:automatic-styles>
  <style:style style:name="taShown" style:family="table">
   <style:table-properties table:display="true"/>
  </style:style>
  <style:style style:name="taHidden" style:family="table">
   <style:table-properties table:display="false"/>
  </style:style>
 </office:automatic-styles>
 <office:body>
  <office:spreadsheet>
   <table:table table:name="Shown" table:style-name="taShown">
    <table:table-column table:number-columns-repeated="4"/>
    <table:table-row table:number-rows-repeated="4">
     <table:table-cell table:number-columns-repeated="4"/>
    </table:table-row>
    <table:table-row>
     <table:table-cell table:number-columns-repeated="2"/>
     <table:table-cell office:value-type="string"><text:p>offset data</text:p></table:table-cell>
    </table:table-row>
   </table:table>
   <table:table table:name="Hidden" table:style-name="taHidden">
    <table:table-row>
     <table:table-cell office:value-type="string"><text:p>hidden data</text:p></table:table-cell>
    </table:table-row>
   </table:table>
  </office:spreadsheet>
 </office:body>
</office:document>
)";

constexpr char kNotesFodp[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 office:version="1.2"
 office:mimetype="application/vnd.oasis.opendocument.presentation">
 <office:body>
  <office:presentation>
   <draw:page draw:name="page1">
    <draw:frame draw:layer="layout" svg:width="10cm" svg:height="2cm"
     svg:x="1cm" svg:y="1cm">
     <draw:text-box><text:p>Slide one</text:p></draw:text-box>
    </draw:frame>
    <presentation:notes>
     <draw:frame presentation:class="notes" svg:width="10cm" svg:height="5cm"
      svg:x="1cm" svg:y="1cm">
      <draw:text-box><text:p>Speaker notes here</text:p></draw:text-box>
     </draw:frame>
    </presentation:notes>
   </draw:page>
  </office:presentation>
 </office:body>
</office:document>
)";

constexpr char kTrackedChangeFodt[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 office:version="1.2"
 office:mimetype="application/vnd.oasis.opendocument.text">
 <office:body>
  <office:text>
   <text:tracked-changes>
    <text:changed-region xml:id="ct1" text:id="ct1">
     <text:insertion>
      <office:change-info>
       <dc:creator>Tester</dc:creator>
       <dc:date>2024-01-01T00:00:00</dc:date>
      </office:change-info>
     </text:insertion>
    </text:changed-region>
   </text:tracked-changes>
   <text:p>Alpha <text:change-start text:change-id="ct1"/>INSERTED <text:change-end text:change-id="ct1"/>omega.</text:p>
  </office:text>
 </office:body>
</office:document>
)";

// The joined run text of every extracted paragraph.
std::string all_paragraph_text(const PagesRun& run) {
  std::string text;
  for (const officev1::Paragraph& paragraph : run.paragraphs) {
    for (const officev1::TextRun& text_run : paragraph.runs()) {
      text += text_run.text();
    }
    text += "\n";
  }
  return text;
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

void verify_tsv_is_spreadsheet() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "tsv", "a\tb\n1\t2\n", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "tsv renders ok: " + outcome.detail);
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "tsv info parses");
  require(first.document_info().document_type() == "spreadsheet", "tsv is spreadsheet");
}

// The PDF now streams straight from the export filter into PdfChunk
// frames, so the checks are structural: complete PDF envelope, event
// order, chunk bounds, and byte accounting. Byte identity with any other
// export route is deliberately not asserted; the produced PDF differs in
// timestamps and document id on every run.
void verify_pdf_mode() {
  std::vector<std::string> payloads;
  auto outcome = run("pdf", "txt", "PDF output please.\n", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "pdf mode ok: " + outcome.detail);
  require(payloads.size() >= 3, "pdf info + chunk + status events");
  std::string pdf;
  long reported_bytes = -1;
  for (size_t i = 0; i < payloads.size(); i++) {
    officev1::ConvertToPdfResponse event;
    require(event.ParseFromString(payloads[i]), "pdf event parses");
    if (i == 0) require(event.has_document_info(), "first pdf event is DocumentInfo");
    if (i + 1 == payloads.size()) {
      require(event.has_status(), "last pdf event is status");
      reported_bytes = event.status().output_bytes();
    }
    if (event.has_pdf_chunk()) {
      require(event.pdf_chunk().data().size() <= 256 * 1024,
              "pdf chunk within the frame bound");
      pdf.append(event.pdf_chunk().data());
    }
  }
  require(pdf.size() > 500, "PDF has substance");
  require(pdf.compare(0, 5, "%PDF-") == 0, "PDF magic");
  require(pdf.rfind("%%EOF") != std::string::npos &&
              pdf.rfind("%%EOF") + 10 > pdf.size(),
          "PDF ends with its EOF marker");
  require(reported_bytes == static_cast<long>(pdf.size()),
          "status output_bytes matches the streamed PDF size");
}

// A document big enough that the PDF spans several chunks proves the
// streaming aggregation: every chunk but the last is exactly the frame
// bound, and the concatenation is one valid PDF.
void verify_pdf_chunk_streaming() {
  std::string csv;
  for (int row = 0; row < 9000; row++) {
    for (int column = 0; column < 6; column++) {
      csv += "row-" + std::to_string(row) + "-col-" + std::to_string(column);
      csv += column + 1 < 6 ? ',' : '\n';
    }
  }
  std::vector<std::string> payloads;
  auto outcome = run("pdf", "csv", csv, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "big csv pdf ok: " + outcome.detail);
  std::vector<size_t> chunk_sizes;
  std::string pdf;
  for (const std::string& payload : payloads) {
    officev1::ConvertToPdfResponse event;
    require(event.ParseFromString(payload), "big pdf event parses");
    if (event.has_pdf_chunk()) {
      chunk_sizes.push_back(event.pdf_chunk().data().size());
      pdf.append(event.pdf_chunk().data());
    }
  }
  require(chunk_sizes.size() >= 2, "PDF spans several chunks");
  for (size_t i = 0; i + 1 < chunk_sizes.size(); i++) {
    require(chunk_sizes[i] == 256 * 1024, "every full chunk is the frame bound");
  }
  require(pdf.compare(0, 5, "%PDF-") == 0 &&
              pdf.rfind("%%EOF") != std::string::npos,
          "chunk concatenation is one PDF");
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
  <text:p><draw:frame draw:name="Frame1" text:anchor-type="paragraph" svg:width="5cm" svg:height="2cm"><draw:text-box draw:chain-next-name="Frame2"><text:p>frame body with a <text:span text:style-name="B1">bold</text:span> run</text:p></draw:text-box></draw:frame><draw:frame draw:name="Frame2" text:anchor-type="paragraph" svg:x="8cm" svg:width="5cm" svg:height="2cm"><draw:text-box/></draw:frame></text:p>
  <text:p><draw:custom-shape draw:name="Shape1" text:anchor-type="paragraph" svg:width="4cm" svg:height="2cm"><text:p>shape text</text:p><draw:enhanced-geometry draw:type="rectangle"/></draw:custom-shape></text:p>
  <text:p><draw:g draw:name="WPG1" text:anchor-type="paragraph"><draw:custom-shape draw:name="GShape1" svg:x="1cm" svg:y="0cm" svg:width="3cm" svg:height="1cm"><text:p>grouped alpha</text:p><draw:enhanced-geometry draw:type="rectangle"/></draw:custom-shape><draw:custom-shape draw:name="GShape2" svg:x="1cm" svg:y="1.5cm" svg:width="3cm" svg:height="1cm"><text:p>grouped beta</text:p><draw:enhanced-geometry draw:type="rectangle"/></draw:custom-shape></draw:g></text:p>
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
  bool frame1_ok = false;
  bool frame2_ok = false;
  bool shape_ok = false;
  bool frame_leaked_into_shapes = false;
  bool out_of_body_text_in_paragraphs = false;
  bool group_ok = false;
  int group_z = -1;
  std::vector<std::string> child_paths;
  std::set<std::string> child_texts;
  bool children_ok = true;
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
        std::string para_text;
        for (const officev1::TextRun& text_run : para.runs()) {
          para_text += text_run.text();
          if (text_run.text() == "bold" && text_run.weight() >= 150.0f &&
              text_run.char_offset() == 17 && text_run.char_length() == 4) {
            saw_bold_run = true;
          }
        }
        // Frame and shape text is out of the body flow and must not leak
        // into body paragraphs.
        if (para_text.find("frame body") != std::string::npos ||
            para_text.find("shape text") != std::string::npos) {
          out_of_body_text_in_paragraphs = true;
        }
        break;
      }
      case officev1::StreamPagesResponse::kTextFrame: {
        const officev1::TextFrame& frame = event.text_frame();
        if (frame.name() == "Frame1") {
          std::string text;
          bool offsets_ok = true;
          bool bold_ok = false;
          for (const officev1::TextRun& text_run : frame.runs()) {
            text += text_run.text();
            offsets_ok = offsets_ok && text_run.char_offset() == -1;
            if (text_run.text() == "bold" && text_run.weight() >= 150.0f) {
              bold_ok = true;
            }
          }
          frame1_ok = frame.chain_next() == "Frame2" &&
                      frame.width_twips() > 0 && frame.height_twips() > 0 &&
                      text == "frame body with a bold run" && offsets_ok &&
                      bold_ok;
        }
        if (frame.name() == "Frame2") {
          frame2_ok = frame.chain_prev() == "Frame1";
        }
        break;
      }
      case officev1::StreamPagesResponse::kShape: {
        const officev1::Shape& shape = event.shape();
        if (shape.name() == "Frame1" || shape.name() == "Frame2") {
          frame_leaked_into_shapes = true;
        }
        if (shape.name() == "Shape1") {
          std::string text;
          bool offsets_ok = true;
          for (const officev1::TextRun& text_run : shape.runs()) {
            text += text_run.text();
            offsets_ok = offsets_ok && text_run.char_offset() == -1;
          }
          shape_ok = !shape.shape_type().empty() && text == "shape text" &&
                     offsets_ok && shape.width_twips() > 0;
        }
        if (shape.name() == "WPG1") {
          group_ok = shape.is_group() && shape.group_path().empty() &&
                     shape.width_twips() > 0 && shape.height_twips() > 0;
          group_z = shape.z_order();
        }
        if (shape.name() == "GShape1" || shape.name() == "GShape2") {
          std::string text;
          for (const officev1::TextRun& text_run : shape.runs()) {
            text += text_run.text();
          }
          child_texts.insert(text);
          child_paths.push_back(shape.group_path());
          children_ok = children_ok && !shape.is_group() &&
                        shape.has_position() && shape.width_twips() > 0;
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
  require(frame1_ok, "text frame with chain, geometry, and styled runs");
  require(frame2_ok, "chained frame carries its back link");
  require(shape_ok, "text-bearing shape with runs and geometry");
  require(group_ok, "group container emitted as its own Shape event");
  require(child_paths.size() == 2 && children_ok,
          "both grouped shapes surface with position and text");
  for (const std::string& path : child_paths) {
    require(path == std::to_string(group_z),
            "grouped shapes name their container through group_path");
  }
  require(child_texts.count("grouped alpha") == 1 &&
              child_texts.count("grouped beta") == 1,
          "grouped shape text is captured");
  require(!frame_leaked_into_shapes, "frames are not double-counted as shapes");
  require(!out_of_body_text_in_paragraphs,
          "frame and shape text stays out of body paragraphs");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "typed last event parses");
  require(last.has_status(), "typed stream ends with status");
  require(last.status().warnings().empty(),
          "typed extraction produced no warnings");
}

// A flat ODS with two sheets: a data sheet exercising a merged header, a
// numeric cell, a currency-formatted cell, a formula, and a cell comment,
// plus a hidden second sheet. Flat XML keeps the fixture diskless and
// reviewable.
const char kTypedFods[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.spreadsheet">
 <office:automatic-styles>
  <number:currency-style style:name="N107">
   <number:currency-symbol number:language="en" number:country="US">$</number:currency-symbol>
   <number:number number:decimal-places="2" number:min-integer-digits="1"/>
  </number:currency-style>
  <style:style style:name="ce1" style:family="table-cell" style:data-style-name="N107"/>
  <style:style style:name="ta2" style:family="table">
   <style:table-properties table:display="false"/>
  </style:style>
 </office:automatic-styles>
 <office:body><office:spreadsheet>
  <table:table table:name="Data">
   <table:table-row>
    <table:table-cell table:number-columns-spanned="2" office:value-type="string"><text:p>Name</text:p></table:table-cell>
    <table:covered-table-cell/>
    <table:table-cell office:value-type="string"><text:p>Price</text:p></table:table-cell>
   </table:table-row>
   <table:table-row>
    <table:table-cell office:value-type="string"><office:annotation><dc:creator>Kris</dc:creator><text:p>Check stock</text:p></office:annotation><text:p>Widget</text:p></table:table-cell>
    <table:table-cell office:value-type="float" office:value="42"><text:p>42</text:p></table:table-cell>
    <table:table-cell table:style-name="ce1" office:value-type="currency" office:currency="USD" office:value="9.99"><text:p>$9.99</text:p></table:table-cell>
   </table:table-row>
   <table:table-row>
    <table:table-cell/>
    <table:table-cell/>
    <table:table-cell table:formula="of:=[.B2]*[.C2]" office:value-type="float" office:value="419.58"><text:p>419.58</text:p></table:table-cell>
   </table:table-row>
  </table:table>
  <table:table table:name="Hidden" table:style-name="ta2">
   <table:table-row>
    <table:table-cell office:value-type="string"><text:p>secret</text:p></table:table-cell>
   </table:table-row>
  </table:table>
  <table:database-ranges>
   <table:database-range table:name="DBData" table:target-range-address="Data.A1:Data.C3" table:contains-header="true" table:display-filter-buttons="true"/>
  </table:database-ranges>
 </office:spreadsheet></office:body>
</office:document>
)";

std::map<int, int> run_selection(const std::string& extension,
                                 const std::string& document,
                                 const std::string& parts_token,
                                 const std::string& what);

void verify_typed_spreadsheet() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fods", kTypedFods, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "typed fods renders ok: " + outcome.detail);
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "fods info parses");
  require(first.document_info().document_type() == "spreadsheet",
          "fods is a spreadsheet");
  std::vector<officev1::Sheet> sheets;
  std::vector<officev1::SheetRow> rows;
  std::vector<officev1::SheetCellComment> comments;
  std::vector<officev1::SheetDatabaseRange> database_ranges;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "fods event parses");
    if (event.has_sheet()) sheets.push_back(event.sheet());
    if (event.has_sheet_row()) rows.push_back(event.sheet_row());
    if (event.has_sheet_cell_comment()) {
      comments.push_back(event.sheet_cell_comment());
    }
    if (event.has_sheet_database_range()) {
      database_ranges.push_back(event.sheet_database_range());
    }
  }
  require(sheets.size() == 2, "two sheet headers");
  require(sheets[0].index() == 0 && sheets[0].name() == "Data" &&
              sheets[0].visible(),
          "data sheet header with name and visibility");
  require(sheets[1].index() == 1 && sheets[1].name() == "Hidden" &&
              !sheets[1].visible(),
          "hidden sheet detected at index 1");
  require(sheets[0].used_end_row() == 2 && sheets[0].used_end_column() == 2,
          "used bounds cover A1:C3");
  bool merge_ok = false;
  bool value_ok = false;
  bool currency_ok = false;
  bool formula_ok = false;
  bool covered_cell_absent = true;
  for (const officev1::SheetRow& row : rows) {
    require(row.sheet_index() != 0 || row.row() <= 2,
            "rows stay inside the used bounds");
    for (const officev1::SheetCell& cell : row.cells()) {
      if (row.sheet_index() == 0 && row.row() == 0 && cell.column() == 0) {
        merge_ok = cell.merged_columns() == 2 && cell.merged_rows() == 1 &&
                   cell.type() == officev1::SHEET_CELL_TYPE_TEXT &&
                   cell.display() == "Name";
      }
      if (row.sheet_index() == 0 && row.row() == 0 && cell.column() == 1) {
        covered_cell_absent = false;
      }
      if (row.sheet_index() == 0 && row.row() == 1 && cell.column() == 1) {
        value_ok = cell.type() == officev1::SHEET_CELL_TYPE_VALUE &&
                   cell.number() == 42.0;
      }
      if (row.sheet_index() == 0 && row.row() == 1 && cell.column() == 2) {
        currency_ok = cell.type() == officev1::SHEET_CELL_TYPE_VALUE &&
                      cell.number() == 9.99 && cell.number_format() != 0 &&
                      !cell.number_format_string().empty();
      }
      if (row.sheet_index() == 0 && row.row() == 2 && cell.column() == 2) {
        formula_ok = cell.type() == officev1::SHEET_CELL_TYPE_FORMULA &&
                     !cell.formula().empty() &&
                     cell.number() > 419.57 && cell.number() < 419.59;
      }
    }
  }
  require(merge_ok, "merge anchor carries its span");
  require(covered_cell_absent, "covered merge cells are absent");
  require(value_ok, "numeric cell keeps its number");
  require(currency_ok, "currency cell carries its number format code");
  require(formula_ok, "formula cell keeps formula and computed number");
  require(comments.size() == 1 && comments[0].sheet_index() == 0 &&
              comments[0].row() == 1 && comments[0].column() == 0 &&
              comments[0].author() == "Kris" &&
              comments[0].text() == "Check stock",
          "cell comment with author, position, and text");
  require(database_ranges.size() == 1, "one database range event");
  require(database_ranges[0].name() == "DBData" &&
              database_ranges[0].sheet_index() == 0 &&
              database_ranges[0].range().start_row() == 0 &&
              database_ranges[0].range().start_column() == 0 &&
              database_ranges[0].range().end_row() == 2 &&
              database_ranges[0].range().end_column() == 2,
          "database range covers Data.A1:C3 by name");
  require(database_ranges[0].contains_header() &&
              database_ranges[0].auto_filter() &&
              !database_ranges[0].totals_row(),
          "database range keeps header and filter flags");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "fods last event parses");
  require(last.has_status(), "fods stream ends with status");
  require(last.status().state() == officev1::RenderStatus::STATE_OK,
          "fods status ok");
  require(last.status().warnings().empty(),
          "fods extraction produced no warnings");

  // Born gated: SHEETS only emits the sheet family and nothing else.
  std::map<int, int> counts =
      run_selection("fods", kTypedFods, "10", "sheets-only");
  require(counts[officev1::StreamPagesResponse::kSheet] == 2,
          "sheets-only emits sheet headers");
  require(counts[officev1::StreamPagesResponse::kSheetRow] > 0,
          "sheets-only emits rows");
  require(counts[officev1::StreamPagesResponse::kPageImage] == 0 &&
              counts[officev1::StreamPagesResponse::kMetadata] == 0,
          "sheets-only emits no pages or metadata");
}

// A flat ODG with, on one page: a rectangle with text, an ellipse, a text
// box with a bold span, a line, a group of two shapes, and an embedded 1x1
// PNG. Flat XML keeps the fixture diskless and reviewable.
const char kDrawFodg[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.graphics">
 <office:automatic-styles>
  <style:style style:name="B1" style:family="text">
   <style:text-properties fo:font-weight="bold"/>
  </style:style>
  <style:page-layout style:name="PM0">
   <style:page-layout-properties fo:page-width="21cm" fo:page-height="29.7cm"
    fo:margin-top="1cm" fo:margin-bottom="1cm" fo:margin-left="1cm" fo:margin-right="1cm"/>
  </style:page-layout>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Default" style:page-layout-name="PM0"/>
 </office:master-styles>
 <office:body><office:drawing>
  <draw:page draw:name="page1" draw:master-page-name="Default">
   <draw:rect draw:name="Rect1" svg:x="1cm" svg:y="1cm" svg:width="4cm" svg:height="2cm">
    <text:p>Rect text</text:p>
   </draw:rect>
   <draw:ellipse draw:name="Ell1" svg:x="6cm" svg:y="1cm" svg:width="3cm" svg:height="3cm"/>
   <draw:frame draw:name="Text1" svg:x="1cm" svg:y="5cm" svg:width="5cm" svg:height="2cm">
    <draw:text-box><text:p>Plain <text:span text:style-name="B1">bold</text:span></text:p></draw:text-box>
   </draw:frame>
   <draw:line draw:name="Line1" svg:x1="1cm" svg:y1="8cm" svg:x2="8cm" svg:y2="9cm"/>
   <draw:g draw:name="Group1">
    <draw:rect draw:name="GRect" svg:x="10cm" svg:y="1cm" svg:width="2cm" svg:height="1cm"/>
    <draw:ellipse draw:name="GEll" svg:x="10cm" svg:y="3cm" svg:width="2cm" svg:height="1cm"/>
   </draw:g>
   <draw:frame draw:name="Img1" svg:x="10cm" svg:y="6cm" svg:width="1cm" svg:height="1cm">
    <draw:image><office:binary-data>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==</office:binary-data></draw:image>
   </draw:frame>
  </draw:page>
 </office:drawing></office:body>
</office:document>
)";

void verify_draw_shapes() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodg", kDrawFodg, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "draw fodg renders ok: " + outcome.detail);
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "draw info parses");
  require(first.document_info().document_type() == "drawing",
          "fodg is a drawing document");
  std::vector<officev1::DrawingShape> shapes;
  int metadata = 0;
  bool image_ok = false;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "draw event parses");
    if (event.has_metadata()) metadata++;
    if (event.has_drawing_shape()) shapes.push_back(event.drawing_shape());
    if (event.has_embedded_image()) {
      const officev1::EmbeddedImage& image = event.embedded_image();
      const std::string& data = image.data();
      image_ok = !image.mime_type().empty() && data.size() > 8 &&
                 data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
                 image.page_index() == 0 && image.width_twips() > 0;
    }
  }
  require(metadata == 1, "one draw metadata event");
  // Six top-level shapes, one of them a group node with two children.
  require(shapes.size() == 8, "eight drawing shapes, got " +
                                  std::to_string(shapes.size()));
  int group_index = -1;
  std::vector<int> top_level_orders;
  for (const officev1::DrawingShape& shape : shapes) {
    require(shape.page_index() == 0, "shape on page 0");
    if (shape.group_path().empty()) {
      top_level_orders.push_back(shape.z_order());
      if (shape.is_group()) {
        require(shape.shape_type() == "com.sun.star.drawing.GroupShape",
                "group node has the group shape type");
        require(shape.name() == "Group1", "group node keeps its name");
        group_index = shape.z_order();
      }
    }
  }
  require(group_index >= 0, "group node emitted");
  require(top_level_orders == std::vector<int>({0, 1, 2, 3, 4, 5}),
          "top-level z order contiguous in paint order");
  std::vector<int> child_orders;
  for (const officev1::DrawingShape& shape : shapes) {
    if (shape.group_path() == std::to_string(group_index)) {
      child_orders.push_back(shape.z_order());
      require(!shape.is_group(), "group children are leaves");
    } else {
      require(shape.group_path().empty(), "no unexpected nesting");
    }
  }
  require(child_orders == std::vector<int>({0, 1}),
          "two group children with restarted z order");
  bool rect_ok = false;
  bool ellipse_ok = false;
  bool text_ok = false;
  bool line_ok = false;
  bool graphic_ok = false;
  for (const officev1::DrawingShape& shape : shapes) {
    if (shape.name() == "Rect1") {
      std::string text;
      for (const officev1::TextRun& text_run : shape.runs()) text += text_run.text();
      rect_ok = shape.shape_type() == "com.sun.star.drawing.RectangleShape" &&
                shape.has_text() && text == "Rect text" &&
                shape.position().x() > 0 && shape.position().y() > 0 &&
                shape.width_twips() > 0 && shape.height_twips() > 0;
    }
    if (shape.name() == "Ell1") {
      ellipse_ok = shape.shape_type() == "com.sun.star.drawing.EllipseShape" &&
                   shape.width_twips() > 0 && shape.height_twips() > 0;
    }
    if (shape.name() == "Text1") {
      bool bold_run = false;
      for (const officev1::TextRun& text_run : shape.runs()) {
        require(text_run.char_offset() == -1,
                "shape runs are outside the annotation space");
        if (text_run.text() == "bold" && text_run.weight() >= 150.0f) {
          bold_run = true;
        }
      }
      text_ok = shape.shape_type() == "com.sun.star.drawing.TextShape" &&
                shape.has_text() && bold_run;
    }
    if (shape.name() == "Line1") {
      line_ok = shape.shape_type() == "com.sun.star.drawing.LineShape";
    }
    if (shape.name() == "Img1") {
      graphic_ok =
          shape.shape_type() == "com.sun.star.drawing.GraphicObjectShape";
    }
  }
  require(rect_ok, "rectangle with text, geometry, and shape type");
  require(ellipse_ok, "ellipse with geometry");
  require(text_ok, "text box with a bold run and out-of-body offsets");
  require(line_ok, "line shape emitted");
  require(graphic_ok, "graphic object shape emitted");
  require(image_ok, "image shape bytes extracted with page index and size");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "draw last event parses");
  require(last.has_status(), "draw stream ends with status");
  require(last.status().state() == officev1::RenderStatus::STATE_OK,
          "draw status ok");
  require(last.status().warnings().empty(),
          "draw extraction produced no warnings");
}

// A flat ODP with three slides: a title slide (title + subtitle
// placeholders), a content slide (title, two-depth outline, and an embedded
// 1x1 PNG), and a slide carrying speaker notes. Flat XML keeps the fixture
// diskless and reviewable.
const char kTypedFodp[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.presentation">
 <office:automatic-styles>
  <style:page-layout style:name="PM0">
   <style:page-layout-properties fo:page-width="28cm" fo:page-height="21cm"
    fo:margin-top="0cm" fo:margin-bottom="0cm" fo:margin-left="0cm" fo:margin-right="0cm"/>
  </style:page-layout>
  <style:style style:name="pr1" style:family="presentation"/>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Default" style:page-layout-name="PM0"/>
 </office:master-styles>
 <office:body><office:presentation>
  <draw:page draw:name="TitleSlide" draw:master-page-name="Default">
   <draw:frame presentation:style-name="pr1" presentation:class="title" presentation:placeholder="false" svg:x="2cm" svg:y="2cm" svg:width="20cm" svg:height="3cm">
    <draw:text-box><text:p>Deck Title</text:p></draw:text-box>
   </draw:frame>
   <draw:frame presentation:style-name="pr1" presentation:class="subtitle" presentation:placeholder="false" svg:x="2cm" svg:y="6cm" svg:width="20cm" svg:height="3cm">
    <draw:text-box><text:p>Deck subtitle</text:p></draw:text-box>
   </draw:frame>
  </draw:page>
  <draw:page draw:name="ContentSlide" draw:master-page-name="Default">
   <draw:frame presentation:style-name="pr1" presentation:class="title" presentation:placeholder="false" svg:x="2cm" svg:y="1cm" svg:width="20cm" svg:height="2cm">
    <draw:text-box><text:p>Agenda</text:p></draw:text-box>
   </draw:frame>
   <draw:frame presentation:style-name="pr1" presentation:class="outline" presentation:placeholder="false" svg:x="2cm" svg:y="4cm" svg:width="18cm" svg:height="10cm">
    <draw:text-box>
     <text:list><text:list-item><text:p>First point</text:p>
      <text:list><text:list-item><text:p>Nested detail</text:p></text:list-item></text:list>
     </text:list-item></text:list>
    </draw:text-box>
   </draw:frame>
   <draw:frame draw:name="Pic1" svg:x="24cm" svg:y="4cm" svg:width="1cm" svg:height="1cm">
    <draw:image><office:binary-data>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==</office:binary-data></draw:image>
   </draw:frame>
  </draw:page>
  <draw:page draw:name="NotesSlide" draw:master-page-name="Default">
   <draw:frame presentation:style-name="pr1" presentation:class="title" presentation:placeholder="false" svg:x="2cm" svg:y="1cm" svg:width="20cm" svg:height="2cm">
    <draw:text-box><text:p>Wrap up</text:p></draw:text-box>
   </draw:frame>
   <presentation:notes>
    <draw:frame presentation:style-name="pr1" presentation:class="notes" presentation:placeholder="false" svg:x="2cm" svg:y="12cm" svg:width="16cm" svg:height="8cm">
     <draw:text-box><text:p>Remember the demo.</text:p></draw:text-box>
    </draw:frame>
   </presentation:notes>
  </draw:page>
 </office:presentation></office:body>
</office:document>
)";

std::map<int, int> run_selection(const std::string& extension,
                                 const std::string& document,
                                 const std::string& parts_token,
                                 const std::string& what);

void verify_typed_presentation() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodp", kTypedFodp, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "typed fodp renders ok: " + outcome.detail);
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "fodp info parses");
  require(first.document_info().document_type() == "presentation",
          "fodp is a presentation");
  require(first.document_info().page_count() == 3, "three slides painted");
  std::vector<officev1::Slide> slides;
  std::vector<officev1::SlideShape> shapes;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "fodp event parses");
    if (event.has_slide()) slides.push_back(event.slide());
    if (event.has_slide_shape()) shapes.push_back(event.slide_shape());
  }
  require(slides.size() == 3, "one Slide header per slide");
  for (int i = 0; i < 3; i++) {
    require(slides[i].index() == i, "slide indexes are in slide order");
    require(!slides[i].name().empty(), "slide keeps its name");
    require(!slides[i].master_page_name().empty(),
            "slide resolves its master page");
  }
  bool title_ok = false;
  bool subtitle_ok = false;
  bool outline_ok = false;
  bool graphic_ok = false;
  bool notes_ok = false;
  for (const officev1::SlideShape& shape : shapes) {
    if (!shape.notes()) {
      require(shape.width_twips() > 0 && shape.height_twips() > 0,
              "slide shape has real geometry");
    }
    if (shape.slide_index() == 0 &&
        shape.placeholder_role() == officev1::PLACEHOLDER_ROLE_TITLE) {
      std::string text;
      for (const officev1::SlideTextParagraph& para : shape.paragraphs()) {
        for (const officev1::TextRun& text_run : para.runs()) {
          text += text_run.text();
          require(text_run.char_offset() == -1,
                  "slide runs are outside the annotation space");
        }
      }
      title_ok = shape.is_placeholder() && text == "Deck Title";
    }
    if (shape.slide_index() == 0 &&
        shape.placeholder_role() == officev1::PLACEHOLDER_ROLE_SUBTITLE) {
      subtitle_ok = shape.is_placeholder();
    }
    if (shape.slide_index() == 1 &&
        shape.placeholder_role() == officev1::PLACEHOLDER_ROLE_OUTLINE) {
      outline_ok = shape.paragraphs_size() == 2 &&
                   shape.paragraphs(0).outline_depth() == 0 &&
                   shape.paragraphs(1).outline_depth() == 1 &&
                   !shape.paragraphs(0).runs().empty() &&
                   shape.paragraphs(0).runs(0).text() == "First point" &&
                   !shape.paragraphs(1).runs().empty() &&
                   shape.paragraphs(1).runs(0).text() == "Nested detail";
    }
    if (shape.slide_index() == 1 &&
        shape.shape_type() == "com.sun.star.drawing.GraphicObjectShape") {
      // Header only: image bytes for slide shapes belong to the
      // embedded-objects work.
      graphic_ok = shape.width_twips() > 0;
    }
    if (shape.notes()) {
      std::string text;
      for (const officev1::SlideTextParagraph& para : shape.paragraphs()) {
        for (const officev1::TextRun& text_run : para.runs()) {
          text += text_run.text();
        }
      }
      notes_ok = shape.slide_index() == 2 &&
                 shape.placeholder_role() == officev1::PLACEHOLDER_ROLE_NOTES &&
                 text == "Remember the demo.";
    }
  }
  require(title_ok, "title placeholder with role and text");
  require(subtitle_ok, "subtitle placeholder with role");
  require(outline_ok, "outline placeholder keeps outline depths");
  require(graphic_ok, "graphic shape header emitted with geometry");
  require(notes_ok, "speaker notes extracted from the notes page");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "fodp last event parses");
  require(last.has_status(), "fodp stream ends with status");
  require(last.status().state() == officev1::RenderStatus::STATE_OK,
          "fodp status ok");
  require(last.status().warnings().empty(),
          "fodp extraction produced no warnings");

  // Born gated: SLIDES only emits slide events and nothing else.
  std::map<int, int> counts =
      run_selection("fodp", kTypedFodp, "11", "slides-only");
  require(counts[officev1::StreamPagesResponse::kSlide] == 3,
          "slides-only emits slide headers");
  require(counts[officev1::StreamPagesResponse::kSlideShape] > 0,
          "slides-only emits slide shapes");
  require(counts[officev1::StreamPagesResponse::kPageImage] == 0 &&
              counts[officev1::StreamPagesResponse::kMetadata] == 0,
          "slides-only emits no pages or metadata");
}

// Counts events by case for one selection run and checks the envelope:
// DocumentInfo first, RenderStatus STATE_OK last, no warnings.
std::map<int, int> run_selection(const std::string& extension,
                                 const std::string& document,
                                 const std::string& parts_token,
                                 const std::string& what) {
  std::vector<std::string> payloads;
  auto outcome = run_with_parts("pages", extension, document, parts_token,
                                &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          what + " renders ok: " + outcome.detail);
  std::map<int, int> counts;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), what + " event parses");
    counts[event.event_case()]++;
  }
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), what + " first parses");
  require(first.has_document_info(), what + " starts with DocumentInfo");
  require(first.document_info().page_count() >= 1,
          what + " keeps a page count");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), what + " last parses");
  require(last.has_status(), what + " ends with RenderStatus");
  require(last.status().state() == officev1::RenderStatus::STATE_OK,
          what + " status ok");
  require(last.status().warnings().empty(), what + " produced no warnings");
  return counts;
}

void verify_part_selection() {
  using Response = officev1::StreamPagesResponse;
  // METADATA only: no pages painted, no text content walked.
  std::map<int, int> counts =
      run_selection("fodt", kTypedFodt, "2", "metadata-only");
  require(counts[Response::kMetadata] == 1, "metadata-only emits metadata");
  require(counts[Response::kPageImage] == 0, "metadata-only paints no pages");
  require(counts[Response::kParagraph] == 0 && counts[Response::kTable] == 0 &&
              counts[Response::kEmbeddedImage] == 0 &&
              counts[Response::kFootnote] == 0 &&
              counts[Response::kHeaderFooter] == 0 &&
              counts[Response::kPageStyle] == 0,
          "metadata-only emits no text content");

  // PAGES only: images but zero typed content.
  counts = run_selection("fodt", kTypedFodt, "1", "pages-only");
  require(counts[Response::kPageImage] >= 1, "pages-only paints pages");
  require(counts[Response::kMetadata] == 0 &&
              counts[Response::kParagraph] == 0 &&
              counts[Response::kTable] == 0 &&
              counts[Response::kEmbeddedImage] == 0,
          "pages-only emits no typed content");

  // PARAGRAPHS plus TABLES: text flow only, offsets intact.
  std::vector<std::string> payloads;
  auto outcome = run_with_parts("pages", "fodt", kTypedFodt, "3,4", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "paragraphs+tables renders ok: " + outcome.detail);
  bool saw_heading = false;
  bool saw_bold_run = false;
  bool table_ok = false;
  int forbidden = 0;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "selection event parses");
    switch (event.event_case()) {
      case Response::kParagraph: {
        const officev1::Paragraph& para = event.paragraph();
        if (!para.runs().empty() && para.runs(0).text() == "Heading One") {
          saw_heading = para.outline_level() == 1;
        }
        for (const officev1::TextRun& text_run : para.runs()) {
          if (text_run.text() == "bold" && text_run.weight() >= 150.0f &&
              text_run.char_offset() == 17) {
            saw_bold_run = true;
          }
        }
        break;
      }
      case Response::kTable:
        table_ok = event.table().rows() == 2 && event.table().columns() == 2;
        break;
      case Response::kEmbeddedImage:
      case Response::kFootnote:
      case Response::kHeaderFooter:
      case Response::kPageStyle:
      case Response::kMetadata:
        forbidden++;
        break;
      default:
        break;
    }
  }
  require(saw_heading, "selected paragraphs keep the heading");
  require(saw_bold_run, "selected paragraphs keep stable char offsets");
  require(table_ok, "selected tables keep the 2x2 grid");
  require(forbidden == 0, "unselected parts stay silent");

  // Draw gating: SHAPES without IMAGES emits shape nodes but no bytes.
  counts = run_selection("fodg", kDrawFodg, "12", "shapes-only");
  require(counts[Response::kDrawingShape] == 8, "shapes-only emits all shapes");
  require(counts[Response::kEmbeddedImage] == 0,
          "shapes-only skips image bytes");
  // IMAGES without SHAPES still finds the image shape.
  counts = run_selection("fodg", kDrawFodg, "5", "draw-images-only");
  require(counts[Response::kDrawingShape] == 0,
          "draw-images-only emits no shape nodes");
  require(counts[Response::kEmbeddedImage] == 1,
          "draw-images-only extracts the image");
}

// A flat ODT on a tiny page so content wraps and paginates: a one-line
// heading, a long paragraph that wraps and straddles the page break, a 2x2
// table, and an as-char image whose anchor line box is meaningful.
const char kLineRectsFodt[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:automatic-styles>
  <style:page-layout style:name="pm1">
   <style:page-layout-properties fo:page-width="10cm" fo:page-height="6cm"
    fo:margin-top="1cm" fo:margin-bottom="1cm" fo:margin-left="1cm" fo:margin-right="1cm"/>
  </style:page-layout>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Standard" style:page-layout-name="pm1"/>
 </office:master-styles>
 <office:body><office:text>
  <text:h text:outline-level="1">Head</text:h>
  <text:p>wrap the quick brown fox jumps over the lazy dog the quick brown
   fox jumps over the lazy dog the quick brown fox jumps over the lazy dog
   the quick brown fox jumps over the lazy dog the quick brown fox jumps
   over the lazy dog the quick brown fox jumps over the lazy dog the quick
   brown fox jumps over the lazy dog the quick brown fox jumps over the
   lazy dog the quick brown fox jumps over the lazy dog the quick brown fox
   jumps over the lazy dog the quick brown fox jumps over the lazy dog the
   quick brown fox jumps over the lazy dog</text:p>
  <table:table table:name="LT">
   <table:table-column table:number-columns-repeated="2"/>
   <table:table-row>
    <table:table-cell><text:p>A1x</text:p></table:table-cell>
    <table:table-cell><text:p>B1x</text:p></table:table-cell>
   </table:table-row>
   <table:table-row>
    <table:table-cell><text:p>A2x</text:p></table:table-cell>
    <table:table-cell><text:p>B2x</text:p></table:table-cell>
   </table:table-row>
  </table:table>
  <text:p>Image line <draw:frame draw:name="LImg" text:anchor-type="as-char" svg:width="1cm" svg:height="1cm"><draw:image><office:binary-data>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==</office:binary-data></draw:image></draw:frame> anchors here.</text:p>
 </office:text></office:body>
</office:document>
)";

void verify_line_rects() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodt", kLineRectsFodt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "line rects fodt renders ok: " + outcome.detail);
  bool heading_ok = false;
  bool wrap_ok = false;
  bool same_space_ok = false;
  bool table_ok = false;
  bool image_ok = false;
  std::set<int> pages_seen;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "line rects event parses");
    if (event.has_paragraph()) {
      const officev1::Paragraph& para = event.paragraph();
      std::string text;
      for (const officev1::TextRun& text_run : para.runs()) {
        text += text_run.text();
      }
      for (const officev1::LineBox& box : para.line_rects()) {
        require(box.page_index() >= 0, "line box resolves its page");
        require(box.width_twips() > 0 && box.height_twips() > 0,
                "line box has real extent");
        pages_seen.insert(box.page_index());
      }
      if (text == "Head") {
        heading_ok = para.line_rects_size() == 1 &&
                     para.line_rects(0).height_twips() > 0 &&
                     para.line_rects(0).char_start() == 0 &&
                     para.line_rects(0).char_end() == 4;
      }
      if (text.compare(0, 4, "wrap") == 0) {
        wrap_ok = para.line_rects_size() > 3;
        // Within one page, line boxes descend in reading order, and the
        // measured character boundaries tile the paragraph: the first line
        // starts at 0, boundaries advance monotonically (a soft wrap may
        // consume its whitespace), and the last line ends at the
        // paragraph's code-point length.
        for (int i = 0; i < para.line_rects_size(); i++) {
          const officev1::LineBox& box = para.line_rects(i);
          wrap_ok = wrap_ok && box.char_start() >= 0 &&
                    box.char_end() > box.char_start();
          if (i == 0) {
            wrap_ok = wrap_ok && box.char_start() == 0;
            continue;
          }
          // Consecutive boxes tile the text: at most the soft wrap's one
          // consumed whitespace character may separate them.
          wrap_ok = wrap_ok &&
                    box.char_start() >= para.line_rects(i - 1).char_end() &&
                    box.char_start() <= para.line_rects(i - 1).char_end() + 1;
          if (box.page_index() == para.line_rects(i - 1).page_index()) {
            wrap_ok = wrap_ok &&
                      box.y_twips() > para.line_rects(i - 1).y_twips();
          }
        }
        wrap_ok = wrap_ok &&
                  para.line_rects(para.line_rects_size() - 1).char_end() ==
                      static_cast<int64_t>(text.size());
        // The caret start and the first line box describe the same spot in
        // the same document-absolute space.
        // The caret round-trips through 1/100 mm inside the office core, so
        // allow a few twips of conversion slack.
        const officev1::LineBox& first = para.line_rects(0);
        same_space_ok =
            std::llabs(first.x_twips() - para.start().x()) <= 60 &&
            para.start().y() >= first.y_twips() - 30 &&
            para.start().y() <= first.y_twips() + first.height_twips();
      }
    }
    if (event.has_table()) {
      const officev1::TableData& table = event.table();
      table_ok = table.line_rects_size() >= 1 &&
                 table.line_rects(0).page_index() >= 0;
      // The per-cell part is never implied by the "all" default.
      for (const officev1::TableCellData& cell : table.cells()) {
        table_ok = table_ok && cell.line_rects_size() == 0;
      }
    }
    if (event.has_embedded_image()) {
      const officev1::EmbeddedImage& image = event.embedded_image();
      image_ok = image.line_rects_size() >= 1 &&
                 image.line_rects(0).page_index() == image.page_index();
    }
  }
  require(heading_ok, "single-line heading yields exactly one line box");
  require(wrap_ok, "wrapping paragraph yields ordered per-line boxes");
  require(pages_seen.size() >= 2,
          "line boxes span at least two pages across the document");
  require(same_space_ok, "caret start and first line box share one space");
  require(table_ok, "table carries its per-table line box union");
  require(image_ok, "as-char image carries its anchor line box");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "line rects last parses");
  require(last.has_status() &&
              last.status().state() == officev1::RenderStatus::STATE_OK,
          "line rects stream ends ok");
  require(last.status().warnings().empty(),
          "line rects extraction produced no warnings");

  // The explicit per-cell part: cells carry their own rectangles, the
  // table-level pool stays keyed to LINE_RECTS, and paragraph measurement
  // does not ride along.
  std::vector<std::string> cell_payloads;
  outcome = run_with_parts("pages", "fodt", kLineRectsFodt, "3,4,16",
                           &cell_payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "cell line rects render ok: " + outcome.detail);
  bool cells_ok = false;
  for (const std::string& payload : cell_payloads) {
    require(event.ParseFromString(payload), "cell line rects event parses");
    if (event.has_paragraph()) {
      require(event.paragraph().line_rects_size() == 0,
              "cell-only selection leaves paragraphs unmeasured");
    }
    if (event.has_table()) {
      const officev1::TableData& table = event.table();
      require(table.line_rects_size() == 0,
              "cell-only selection leaves the table pool empty");
      cells_ok = table.cells_size() == 4;
      for (const officev1::TableCellData& cell : table.cells()) {
        cells_ok = cells_ok && cell.line_rects_size() >= 1 &&
                   cell.line_rects(0).page_index() >= 0 &&
                   cell.line_rects(0).width_twips() > 0 &&
                   cell.line_rects(0).height_twips() > 0;
      }
    }
  }
  require(cells_ok, "every table cell carries its own line rectangles");

  // Both parts together: one measurement per cell serves both targets.
  cell_payloads.clear();
  outcome = run_with_parts("pages", "fodt", kLineRectsFodt, "4,15,16",
                           &cell_payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "combined line rects render ok: " + outcome.detail);
  bool combined_ok = false;
  for (const std::string& payload : cell_payloads) {
    require(event.ParseFromString(payload), "combined line rects event parses");
    if (event.has_table()) {
      const officev1::TableData& table = event.table();
      int cell_boxes = 0;
      for (const officev1::TableCellData& cell : table.cells()) {
        cell_boxes += cell.line_rects_size();
      }
      combined_ok = cell_boxes > 0 && table.line_rects_size() == cell_boxes;
    }
  }
  require(combined_ok, "pool and per-cell rectangles agree when both selected");
}

// A flat ODT with three embedded objects: a Math formula, a bar chart with
// three categories and two numeric series, and an embedded spreadsheet.
// Flat ODF embeds each object's document inline, keeping the fixture
// diskless and reviewable.
const char kEmbeddedFodt[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:chart="urn:oasis:names:tc:opendocument:xmlns:chart:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:body><office:text>
  <text:p>Objects follow.</text:p>
  <text:p>The flat XML type detector matches any office:mimetype token in
   the first four thousand bytes of the file, so this filler paragraph
   keeps the nested spreadsheet object's mimetype beyond that window and
   the document reliably detected as text. It carries no assertions of its
   own; it only pads the byte offset of the objects below, which is why it
   rambles on for a few more words than any reasonable paragraph would
   otherwise need to.</text:p>
  <text:p><draw:frame draw:name="Math1" svg:width="3cm" svg:height="1cm"><draw:object>
   <math xmlns="http://www.w3.org/1998/Math/MathML" display="block">
    <semantics>
     <mrow><msup><mi>a</mi><mn>2</mn></msup><mo>+</mo><msup><mi>b</mi><mn>2</mn></msup><mo>=</mo><msup><mi>c</mi><mn>2</mn></msup></mrow>
     <annotation encoding="StarMath 5.0">a^2 + b^2 = c^2</annotation>
    </semantics>
   </math>
  </draw:object></draw:frame></text:p>
  <text:p><draw:frame draw:name="Chart1" svg:width="8cm" svg:height="6cm"><draw:object>
   <office:document office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.chart">
    <office:body><office:chart>
     <chart:chart chart:class="chart:bar">
      <chart:title><text:p>Sales</text:p></chart:title>
      <chart:plot-area>
       <chart:axis chart:dimension="x" chart:name="primary-x"><chart:categories table:cell-range-address="local-table.$A$2:.$A$4"/></chart:axis>
       <chart:axis chart:dimension="y" chart:name="primary-y"/>
       <chart:series chart:values-cell-range-address="local-table.$B$2:.$B$4" chart:label-cell-address="local-table.$B$1"><chart:data-point chart:repeated="3"/></chart:series>
       <chart:series chart:values-cell-range-address="local-table.$C$2:.$C$4" chart:label-cell-address="local-table.$C$1"><chart:data-point chart:repeated="3"/></chart:series>
      </chart:plot-area>
      <table:table table:name="local-table">
       <table:table-header-columns><table:table-column/></table:table-header-columns>
       <table:table-columns><table:table-column table:number-columns-repeated="2"/></table:table-columns>
       <table:table-header-rows>
        <table:table-row><table:table-cell/><table:table-cell office:value-type="string"><text:p>Alpha</text:p></table:table-cell><table:table-cell office:value-type="string"><text:p>Beta</text:p></table:table-cell></table:table-row>
       </table:table-header-rows>
       <table:table-rows>
        <table:table-row><table:table-cell office:value-type="string"><text:p>Q1</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="1"><text:p>1</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="4"><text:p>4</text:p></table:table-cell></table:table-row>
        <table:table-row><table:table-cell office:value-type="string"><text:p>Q2</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="2"><text:p>2</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="5"><text:p>5</text:p></table:table-cell></table:table-row>
        <table:table-row><table:table-cell office:value-type="string"><text:p>Q3</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="3"><text:p>3</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="6"><text:p>6</text:p></table:table-cell></table:table-row>
       </table:table-rows>
      </table:table>
     </chart:chart>
    </office:chart></office:body>
   </office:document>
  </draw:object></draw:frame></text:p>
  <text:p><draw:frame draw:name="Calc1" svg:width="6cm" svg:height="3cm"><draw:object>
   <office:document office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.spreadsheet">
    <office:body><office:spreadsheet>
     <table:table table:name="Inner">
      <table:table-row><table:table-cell office:value-type="string"><text:p>K</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="7"><text:p>7</text:p></table:table-cell></table:table-row>
      <table:table-row><table:table-cell office:value-type="string"><text:p>L</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="8"><text:p>8</text:p></table:table-cell></table:table-row>
     </table:table>
    </office:spreadsheet></office:body>
   </office:document>
  </draw:object></draw:frame></text:p>
 </office:text></office:body>
</office:document>
)";

void verify_embedded_objects() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodt", kEmbeddedFodt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "embedded fodt renders ok: " + outcome.detail);
  std::vector<officev1::EmbeddedObject> objects;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "embedded event parses");
    if (event.has_embedded_object()) objects.push_back(event.embedded_object());
  }
  require(objects.size() == 3, "three embedded objects, got " +
                                   std::to_string(objects.size()));
  bool formula_ok = false;
  bool chart_ok = false;
  bool sheet_ok = false;
  for (const officev1::EmbeddedObject& object : objects) {
    if (object.kind() == officev1::EMBEDDED_OBJECT_KIND_FORMULA) {
      formula_ok = object.formula().find("a^2") != std::string::npos &&
                   object.page_index() == 0 && object.width_twips() > 0;
    }
    if (object.kind() == officev1::EMBEDDED_OBJECT_KIND_CHART) {
      const officev1::EmbeddedChart& chart = object.chart();
      bool series_ok =
          chart.series_size() == 2 && chart.series(0).values_y_size() == 3 &&
          chart.series(0).values_y(0) == 1.0 &&
          chart.series(0).values_y(2) == 3.0 &&
          chart.series(1).values_y_size() == 3 &&
          chart.series(1).values_y(2) == 6.0 &&
          chart.series(0).label() == "Alpha" &&
          chart.series(1).label() == "Beta";
      bool categories_ok = chart.categories_size() == 3 &&
                           chart.categories(0) == "Q1" &&
                           chart.categories(2) == "Q3";
      bool tabular_ok = chart.tabular().rows() == 4 &&
                        chart.tabular().columns() == 3 &&
                        chart.tabular().cells_size() > 0;
      chart_ok = !chart.chart_type_service().empty() &&
                 chart.kind() != officev1::EMBEDDED_CHART_KIND_UNSPECIFIED &&
                 chart.title() == "Sales" && series_ok && categories_ok &&
                 tabular_ok;
    }
    if (object.kind() == officev1::EMBEDDED_OBJECT_KIND_SPREADSHEET) {
      const officev1::TableData& table = object.inner_table();
      bool sampled = false;
      for (const officev1::TableCellData& cell : table.cells()) {
        if (cell.row() == 1 && cell.column() == 1 && cell.text() == "8") {
          sampled = true;
        }
      }
      sheet_ok = table.rows() == 2 && table.columns() == 2 && sampled;
    }
  }
  require(formula_ok, "formula object with StarMath command and anchor page");
  require(chart_ok, "chart object with typed series, categories, and grid");
  require(sheet_ok, "embedded spreadsheet projected to its used grid");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "embedded last event parses");
  require(last.has_status() &&
              last.status().state() == officev1::RenderStatus::STATE_OK,
          "embedded stream ends ok");

  // Born gated: EMBEDDED_OBJECTS only emits object events and nothing else.
  std::map<int, int> counts =
      run_selection("fodt", kEmbeddedFodt, "14", "embedded-only");
  require(counts[officev1::StreamPagesResponse::kEmbeddedObject] == 3,
          "embedded-only emits the objects");
  require(counts[officev1::StreamPagesResponse::kPageImage] == 0 &&
              counts[officev1::StreamPagesResponse::kMetadata] == 0 &&
              counts[officev1::StreamPagesResponse::kParagraph] == 0,
          "embedded-only emits no pages, metadata, or paragraphs");
}

// Feeds a real worker event stream through the consumer-side docling mapper
// and checks the produced Document: page rectangles on the wire, a well
// formed ref tree, and the flagship label and layer mappings.
void verify_docling_mapping() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodt", kTypedFodt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "docling source render ok: " + outcome.detail);
  grlibre::DoclingMapper mapper;
  officev1::DocumentInfo info;
  for (const std::string& payload : payloads) {
    officev1::StreamPagesResponse event;
    require(event.ParseFromString(payload), "docling source event parses");
    if (event.has_document_info()) info = event.document_info();
    mapper.consume(event);
  }
  require(info.page_rects_size() == info.page_count(),
          "DocumentInfo carries one page rect per page");
  require(info.page_rects_size() > 0
              && info.page_rects(0).width_twips() > 0
              && info.page_rects(0).height_twips() > 0,
          "page rects have real dimensions");
  require(mapper.finished(), "mapper consumed the terminal status");

  const auto& document = mapper.document();
  std::vector<std::string> errors = grlibre::docling_integrity_errors(document);
  for (const std::string& error : errors) {
    std::cerr << "integrity: " << error << "\n";
  }
  require(errors.empty(), "mapped document ref tree is well formed");
  require(document.pages_size() == info.page_count(),
          "one PageItem per page");

  namespace docv1 = ai::pipestream::document::v1;
  bool heading_ok = false;
  bool header_ok = false;
  bool footnote_ok = false;
  for (const docv1::BaseTextItem& item : document.texts()) {
    if (item.item_case() == docv1::BaseTextItem::kSectionHeader) {
      const docv1::SectionHeaderItem& heading = item.section_header();
      if (heading.base().text() == "Heading One") {
        heading_ok = heading.level() == 1
            && heading.base().content_layer() == docv1::CONTENT_LAYER_BODY;
      }
    }
    if (item.item_case() == docv1::BaseTextItem::kText) {
      const docv1::TextItemBase& base = item.text().base();
      if (base.label() == docv1::DOC_ITEM_LABEL_PAGE_HEADER) {
        header_ok = base.text() == "Header text"
            && base.content_layer() == docv1::CONTENT_LAYER_FURNITURE
            && base.parent().ref() == "#/furniture";
      }
      if (base.label() == docv1::DOC_ITEM_LABEL_FOOTNOTE) {
        footnote_ok = base.text() == "Footnote text."
            && base.content_layer() == docv1::CONTENT_LAYER_BODY;
      }
    }
  }
  require(heading_ok, "mapped heading is a body SectionHeaderItem level 1");
  require(header_ok, "mapped header is furniture PAGE_HEADER");
  require(footnote_ok, "mapped footnote keeps the FOOTNOTE label in body");
  require(document.tables_size() >= 1
              && document.tables(0).data().num_rows() == 2
              && document.tables(0).data().num_cols() == 2,
          "mapped table keeps its grid dimensions");
  require(document.pictures_size() >= 1
              && document.pictures(0).image().uri()
                     .rfind("data:image/", 0) == 0,
          "mapped picture carries a data URI");
  bool frame_group = false;
  std::string wpg_ref;
  for (const docv1::GroupItem& group : document.groups()) {
    if (group.name() == "Frame1") {
      frame_group = group.meta().custom_fields().count("chain_next") == 1;
    }
    if (group.name() == "WPG1") wpg_ref = group.self_ref();
  }
  require(frame_group, "mapped frame group keeps its chain name");
  require(!wpg_ref.empty(), "mapped WPG group container exists");
  int nested = 0;
  for (const docv1::GroupItem& group : document.groups()) {
    if ((group.name() == "GShape1" || group.name() == "GShape2")
        && group.parent().ref() == wpg_ref) {
      nested++;
    }
  }
  require(nested == 2, "grouped shapes nest under the WPG group");

  // Every provenance box is page-local: inside its page's rectangle.
  auto box_in_page = [&](const docv1::ProvenanceItem& prov) {
    if (prov.page_no() < 1 || prov.page_no() > info.page_rects_size()) {
      return false;
    }
    const officev1::PageRect& page = info.page_rects(prov.page_no() - 1);
    return prov.bbox().l() >= 0 && prov.bbox().t() >= 0
        && prov.bbox().r() <= page.width_twips()
        && prov.bbox().b() <= page.height_twips();
  };
  for (const docv1::BaseTextItem& item : document.texts()) {
    if (item.item_case() != docv1::BaseTextItem::kText) continue;
    for (const docv1::ProvenanceItem& prov : item.text().base().prov()) {
      require(box_in_page(prov), "text prov box is page-local");
    }
  }
}

// A flat ODT exercising every annotation mark: a hyperlink, a point and a
// ranged comment, point and ranged bookmarks, a tracked insertion and a
// tracked deletion, checkbox and text fieldmarks, and a draw-page form
// control. Flat XML keeps the fixture diskless and reviewable.
const char kMarkedFodt[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:form="urn:oasis:names:tc:opendocument:xmlns:form:1.0"
 xmlns:xml="http://www.w3.org/XML/1998/namespace"
 xmlns:field="urn:openoffice:names:experimental:ooo-ms-interop:xmlns:field:1.0"
 office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:automatic-styles/>
 <office:body><office:text>
  <office:forms form:automatic-focus="false" form:apply-design-mode="false">
   <form:form form:name="F1">
    <form:checkbox form:name="CB1" form:id="ctl1" xml:id="ctl1"
     form:label="Agree" form:current-state="checked"/>
   </form:form>
  </office:forms>
  <text:tracked-changes>
   <text:changed-region xml:id="ins1" text:id="ins1">
    <text:insertion><office:change-info><dc:creator>Bob</dc:creator><dc:date>2024-02-03T04:05:06</dc:date></office:change-info></text:insertion>
   </text:changed-region>
   <text:changed-region xml:id="del1" text:id="del1">
    <text:deletion><office:change-info><dc:creator>Cara</dc:creator><dc:date>2024-02-04T05:06:07</dc:date></office:change-info><text:p>gone words</text:p></text:deletion>
   </text:changed-region>
  </text:tracked-changes>
  <text:p>Visit <text:a xlink:href="https://example.test/go" office:target-frame-name="_blank">linked words</text:a> today.</text:p>
  <text:p>Point<office:annotation office:name="cp1"><dc:creator>Alice</dc:creator><dc:date>2024-01-02T03:04:05</dc:date><text:p>A point comment</text:p></office:annotation> anchor here.</text:p>
  <text:p>Range <office:annotation office:name="cr1"><dc:creator>Bea</dc:creator><dc:date>2024-01-03T04:05:06</dc:date><text:p>A ranged comment</text:p></office:annotation>target words<office:annotation-end office:name="cr1"/> end.</text:p>
  <text:p><text:bookmark text:name="bmPoint"/>Bookmark <text:bookmark-start text:name="bmRange"/>marked words<text:bookmark-end text:name="bmRange"/> tail.</text:p>
  <text:p>Change <text:change-start text:change-id="ins1"/>added words<text:change-end text:change-id="ins1"/> and <text:change text:change-id="del1"/> after.</text:p>
  <text:p>Check: <field:fieldmark text:name="check1" field:type="vnd.oasis.opendocument.field.FORMCHECKBOX"><field:param field:name="Checkbox_Checked" field:value="true"/></field:fieldmark> Text: <field:fieldmark-start text:name="txt1" field:type="vnd.oasis.opendocument.field.FORMTEXT"/>filled value<field:fieldmark-end/> done.</text:p>
  <text:p><draw:control text:anchor-type="as-char" svg:width="2cm" svg:height="0.6cm" draw:control="ctl1"/></text:p>
 </office:text></office:body>
</office:document>
)";

void verify_marks_content() {
  std::vector<std::string> payloads;
  auto outcome = run("pages", "fodt", kMarkedFodt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "marked fodt renders ok: " + outcome.detail);

  bool hyperlink_ok = false;
  bool point_comment_ok = false;
  bool ranged_comment_ok = false;
  bool point_bookmark_ok = false;
  bool ranged_bookmark_ok = false;
  bool insert_change_ok = false;
  bool delete_change_ok = false;
  bool checkbox_ok = false;
  bool text_field_ok = false;
  bool control_ok = false;
  std::vector<std::string> warnings;
  officev1::StreamPagesResponse event;
  for (const std::string& payload : payloads) {
    require(event.ParseFromString(payload), "marked event parses");
    switch (event.event_case()) {
      case officev1::StreamPagesResponse::kParagraph: {
        for (const officev1::TextRun& run : event.paragraph().runs()) {
          if (run.text() == "linked words") {
            hyperlink_ok =
                run.hyperlink_url() == "https://example.test/go" &&
                run.hyperlink_target() == "_blank" && run.char_offset() >= 0;
          }
        }
        break;
      }
      case officev1::StreamPagesResponse::kComment: {
        const officev1::Comment& comment = event.comment();
        if (comment.author() == "Alice") {
          point_comment_ok = comment.text() == "A point comment" &&
                             comment.char_start() >= 0 &&
                             comment.char_end() == comment.char_start() &&
                             comment.epoch_ms() > 0 &&
                             comment.page_index() == 0;
        }
        if (comment.author() == "Bea") {
          ranged_comment_ok = comment.text() == "A ranged comment" &&
                              comment.char_start() >= 0 &&
                              comment.char_end() - comment.char_start() == 12 &&
                              comment.anchored_text() == "target words";
        }
        break;
      }
      case officev1::StreamPagesResponse::kBookmark: {
        const officev1::Bookmark& bookmark = event.bookmark();
        if (bookmark.name() == "bmPoint") {
          point_bookmark_ok = bookmark.char_start() >= 0 &&
                              bookmark.char_end() == bookmark.char_start();
        }
        if (bookmark.name() == "bmRange") {
          ranged_bookmark_ok =
              bookmark.char_start() >= 0 &&
              bookmark.char_end() - bookmark.char_start() == 12 &&
              bookmark.covered_text() == "marked words";
        }
        break;
      }
      case officev1::StreamPagesResponse::kTrackedChange: {
        const officev1::TrackedChange& change = event.tracked_change();
        if (change.kind() == officev1::TRACKED_CHANGE_KIND_INSERT) {
          insert_change_ok = change.author() == "Bob" &&
                             change.char_start() >= 0 &&
                             change.char_end() - change.char_start() == 11 &&
                             change.changed_text() == "added words" &&
                             change.epoch_ms() > 0;
        }
        if (change.kind() == officev1::TRACKED_CHANGE_KIND_DELETE) {
          delete_change_ok =
              change.author() == "Cara" &&
              change.changed_text().find("gone words") != std::string::npos;
        }
        break;
      }
      case officev1::StreamPagesResponse::kFormField: {
        const officev1::FormField& field = event.form_field();
        if (field.name() == "check1") {
          checkbox_ok = field.kind() == officev1::FORM_FIELD_KIND_CHECKBOX &&
                        field.checked() && !field.control() &&
                        field.char_start() >= 0;
        }
        if (field.name() == "txt1") {
          text_field_ok = field.kind() == officev1::FORM_FIELD_KIND_TEXT &&
                          field.text() == "filled value" &&
                          field.char_start() >= 0 &&
                          field.char_end() - field.char_start() == 12;
        }
        if (field.control()) {
          control_ok = field.kind() == officev1::FORM_FIELD_KIND_CHECKBOX &&
                       field.checked() && field.label() == "Agree" &&
                       field.width_twips() > 0;
        }
        break;
      }
      case officev1::StreamPagesResponse::kStatus: {
        warnings.assign(event.status().warnings().begin(),
                        event.status().warnings().end());
        break;
      }
      default:
        break;
    }
  }
  for (const std::string& warning : warnings) {
    std::cerr << "marked warning: " << warning << "\n";
  }
  require(hyperlink_ok, "hyperlink url, target, and span on the linked run");
  require(point_comment_ok, "point comment with author, date, and anchor");
  require(ranged_comment_ok, "ranged comment with span and anchored text");
  require(point_bookmark_ok, "point bookmark with a collapsed span");
  require(ranged_bookmark_ok, "ranged bookmark with span and covered text");
  require(insert_change_ok, "tracked insertion with author, span, and text");
  require(delete_change_ok, "tracked deletion with author and deleted text");
  require(checkbox_ok, "checkbox fieldmark with its checked state");
  require(text_field_ok, "text fieldmark with its content and span");
  require(control_ok, "form control with label, state, and geometry");
  require(warnings.empty(), "marked extraction produced no warnings");
}

// The edict behind the diskless path: no document-named file may exist in
// the work dir while frames are flowing or after the run. The first frame
// is emitted only after the load, by which point the upload must already be
// unlinked; in pdf mode the staged out.pdf must be gone before its chunks
// stream. The fixture carries an embedded image so the check also proves
// the core's lazy storage reads survive the unlink.
void verify_work_dir_stays_documentless() {
  for (const std::string& mode : {std::string("pages"), std::string("pdf")}) {
    std::string work_dir = make_work_dir();
    std::vector<std::string> argv = {
        worker_path(), mode, "fodt", "96", "2048",
        work_dir, lo_install_path(), "all"};
    auto document_files = [&] {
      std::vector<std::string> found;
      for (const auto& entry : std::filesystem::directory_iterator(work_dir)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("doc.", 0) == 0 || name == "out.pdf") found.push_back(name);
      }
      return found;
    };
    int frames = 0;
    bool clean_during = true;
    auto outcome = grlibre::run_worker(
        argv, kTypedFodt, std::chrono::milliseconds(120000),
        256u * 1024 * 1024, [&](std::string&&) {
          frames++;
          clean_during = clean_during && document_files().empty();
          return true;
        });
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            mode + " documentless render ok: " + outcome.detail);
    require(frames > 0, mode + " streamed frames");
    require(clean_during, mode + " work dir holds no document file while streaming");
    require(document_files().empty(), mode + " work dir holds no document file after");
    std::error_code ignored;
    std::filesystem::remove_all(work_dir, ignored);
  }
}

// A work dir off tmpfs would put uploaded bytes on disk; the worker must
// refuse outright instead of falling back.
void verify_disk_work_dir_is_refused() {
  // The build tree's own directory is the most reliable disk-backed
  // candidate: on hosts where every temp path is tmpfs, the sources still
  // live on real storage.
  std::string base;
  for (const char* candidate : {"/var/tmp", "/tmp", "."}) {
    struct statfs fs;
    if (::statfs(candidate, &fs) == 0 && fs.f_type != TMPFS_MAGIC) {
      base = candidate;
      break;
    }
  }
  require(!base.empty(),
          "no disk-backed directory found to exercise the refusal check");
  std::string pattern = base + "/grlibre-disk-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  require(::mkdtemp(buffer.data()) != nullptr, "mkdtemp on disk");
  std::string work_dir = buffer.data();
  std::vector<std::string> payloads;
  std::vector<std::string> argv = {
      worker_path(), "pages", "txt", "96", "2048",
      work_dir, lo_install_path(), "all"};
  auto outcome = grlibre::run_worker(
      argv, "must not land on disk\n", std::chrono::milliseconds(120000),
      256u * 1024 * 1024, [&](std::string&& payload) {
        payloads.push_back(std::move(payload));
        return true;
      });
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kWorkDirNotTmpfs,
          "disk work dir is refused, got detail: " + outcome.detail);
  require(payloads.empty(), "refusal happens before any frame");
  require(!std::filesystem::exists(work_dir + "/doc.txt"),
          "no document bytes were written to the disk work dir");
  std::error_code ignored;
  std::filesystem::remove_all(work_dir, ignored);
}

void verify_corrupt_zip_is_load_failure() {
  // Plain ASCII garbage would not do here: the office core content-sniffs
  // it as text and loads it. A broken zip container is genuinely unloadable
  // and, before the Batch load option, hung on the repair interaction.
  // This one is damage beyond repair (no valid entry survives), so it stays
  // a plain load failure rather than a repair refusal.
  std::string corrupt = "PK\x03\x04";
  for (int i = 0; i < 4096; i++) corrupt.push_back(static_cast<char>(i * 131 % 251));
  std::vector<std::string> payloads;
  auto outcome = run("pages", "docx", corrupt, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kLoadFailure,
          "corrupt docx is a load failure, got detail: " + outcome.detail);
}

// A stored-entry OOXML zip truncated right before its central directory:
// the office core's own broken-ZIP probe classifies it as repairable, so
// loading it must surface the allow_package_repair opt-in instead of a
// generic load failure. Embedded as bytes because a broken zip cannot be a
// readable fixture; two intact local file entries ([Content_Types].xml and
// a minimal word/document.xml), no central directory.
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

void verify_broken_package_needs_repair_opt_in() {
  std::string broken(kRepairableDocx, sizeof kRepairableDocx - 1);
  // Default: refused with the status naming the opt-in; no frame precedes
  // the refusal.
  {
    std::string work_dir = make_work_dir();
    std::vector<std::string> payloads;
    std::vector<std::string> argv = {
        worker_path(), "pages", "docx", "96", "2048",
        work_dir, lo_install_path(), "all", "no-repair"};
    auto outcome = grlibre::run_worker(
        argv, broken, std::chrono::milliseconds(120000), 256u * 1024 * 1024,
        [&](std::string&& payload) {
          payloads.push_back(std::move(payload));
          return true;
        });
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kRepairNeedsOptIn,
            "broken package needs the repair opt-in, got detail: " + outcome.detail);
    require(outcome.detail.find("allow_package_repair") != std::string::npos,
            "refusal names the opt-in field");
    require(payloads.empty(), "refusal happens before any frame");
    std::error_code ignored;
    std::filesystem::remove_all(work_dir, ignored);
  }
  // Opted in: the worker retries with RepairPackage=true. A package this
  // truncated may still fail to load; it must never report unimplemented.
  {
    std::string work_dir = make_work_dir();
    std::vector<std::string> payloads;
    std::vector<std::string> argv = {
        worker_path(), "pages", "docx", "96", "2048",
        work_dir, lo_install_path(), "all", "repair"};
    auto outcome = grlibre::run_worker(
        argv, broken, std::chrono::milliseconds(120000), 256u * 1024 * 1024,
        [&](std::string&& payload) {
          payloads.push_back(std::move(payload));
          return true;
        });
    require(outcome.kind != grlibre::WorkerOutcome::Kind::kRepairUnimplemented,
            "opted-in repair is no longer unimplemented, got detail: "
                + outcome.detail);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk
                || outcome.kind == grlibre::WorkerOutcome::Kind::kLoadFailure,
            "opted-in repair either loads or fails as a load error, got detail: "
                + outcome.detail);
    std::error_code ignored;
    std::filesystem::remove_all(work_dir, ignored);
  }
}

// An HTML source once burned the worker's exit path twice over: this
// process never runs DeInitVCL, so exit()-time teardown crashed inside
// tcmalloc's atexit scavenge after every HTML load (turning a finished
// render into a crash report), and static destruction of the office
// core's BufferedDecompositionFlusher stalled the exit in exact 2 s
// steps after the terminal frame. The worker now ends with _exit, so an
// HTML render must succeed and the outcome must follow the final frame
// far inside the flusher's 2 s wait quantum.
void verify_html_renders_and_exits_promptly() {
  std::string work_dir = make_work_dir();
  std::vector<std::string> argv = {
      worker_path(), "pages", "html", "96", "2048",
      work_dir, lo_install_path(), "all"};
  std::vector<std::string> payloads;
  auto last_frame = std::chrono::steady_clock::now();
  auto outcome = grlibre::run_worker(
      argv, "<html><body><h1>Title</h1><p>Hello over HTML.</p></body></html>\n",
      std::chrono::milliseconds(120000), 256u * 1024 * 1024,
      [&](std::string&& payload) {
        payloads.push_back(std::move(payload));
        last_frame = std::chrono::steady_clock::now();
        return true;
      });
  auto settled = std::chrono::steady_clock::now();
  std::error_code ignored;
  std::filesystem::remove_all(work_dir, ignored);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "html renders ok: " + outcome.detail);
  require(payloads.size() >= 3, "html info + page + status events");
  officev1::StreamPagesResponse first;
  require(first.ParseFromString(payloads.front()), "html info parses");
  require(first.has_document_info() &&
              first.document_info().document_type() == "text",
          "html loads as a text document");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "html last event parses");
  require(last.has_status() &&
              last.status().state() == officev1::RenderStatus::STATE_OK,
          "html stream ends with STATE_OK");
  auto lag = std::chrono::duration_cast<std::chrono::milliseconds>(
      settled - last_frame);
  require(lag.count() < 1000,
          "outcome follows the final frame inside the 2 s teardown quantum, "
          "took " + std::to_string(lag.count()) + " ms");
}

// The completion signal is the terminal status frame backed by the exit
// code: exit 0 is trusted to mean the terminal frame was written because
// run_render only returns kExitOk after that frame's write succeeded. The
// parent side of the contract: a worker that dies mid-stream, before the
// terminal frame, must surface as a crash, never as kOk. A stub stands in
// for the worker so the death is deterministic.
void verify_death_before_status_is_crash() {
  int frames = 0;
  auto outcome = grlibre::run_worker(
      {"/bin/sh", "-c", "printf '\\0\\0\\0\\0'; kill -9 $$"}, "",
      std::chrono::milliseconds(120000), 256u * 1024 * 1024,
      [&](std::string&&) {
        frames++;
        return true;
      });
  require(frames == 1, "the stub's one frame arrived before it died");
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "death before the terminal frame is a crash, got: " + outcome.detail);
  require(outcome.detail.find("signal") != std::string::npos,
          "the crash names the killing signal");
}

// A worker that hangs producing nothing must still die at the task
// deadline: with the office core in an unknown state, the parent's kill
// path is the only way the RPC ends. A sleeping stub stands in for a hung
// office core.
void verify_hung_worker_is_killed_at_deadline() {
  auto begin = std::chrono::steady_clock::now();
  // exec, not fork: the deadline SIGKILL goes to the pid run_worker
  // spawned, and a forked sleep would survive its shell and hold the
  // inherited output descriptors open long past this test.
  auto outcome = grlibre::run_worker(
      {"/bin/sh", "-c", "exec sleep 600"}, "", std::chrono::milliseconds(500),
      256u * 1024 * 1024, [&](std::string&&) { return true; });
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kTimeout,
          "hung worker times out, got: " + outcome.detail);
  require(elapsed.count() < 5000,
          "the deadline kill returns promptly, took "
              + std::to_string(elapsed.count()) + " ms");
}

// A worker that closes its stream but never exits must not hold the caller
// (and its concurrency slot) forever: finish() reaps it after a bounded
// grace and reports the abnormal exit. A stub that closes stdout then
// sleeps stands in for a wedged process.
void verify_eof_without_exit_is_reaped() {
  auto begin = std::chrono::steady_clock::now();
  auto outcome = grlibre::run_worker(
      {"/bin/sh", "-c", "exec 1>&-; exec sleep 600"}, "",
      std::chrono::milliseconds(120000), 256u * 1024 * 1024,
      [&](std::string&&) { return true; });
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kCrash,
          "eof-without-exit surfaces as a crash, got: " + outcome.detail);
  require(outcome.detail.find("did not exit") != std::string::npos,
          "the reap detail names the condition, got: " + outcome.detail);
  require(elapsed.count() >= 1900 && elapsed.count() < 8000,
          "reap happens at the grace bound, took "
              + std::to_string(elapsed.count()) + " ms");
}

}  // namespace

// The per-request extras that ride options.pb: fit-to-width, grayscale,
// and the SVG vector format, each against a plain text upload.
void verify_stream_option_extras() {
  const std::string doc = "Hello from the extras render.\n";
  {
    officev1::StreamOptions extras;
    extras.set_max_width_px(200);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "txt", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "max_width render ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(!run.pages.empty(), "max_width page painted");
    require(run.pages[0].width_px() > 0 && run.pages[0].width_px() <= 200,
            "max_width_px bounds the page width");
    require(run.pages[0].dpi() >= 1 && run.pages[0].dpi() < 96,
            "fit-to-width derives a smaller effective dpi");
  }
  {
    officev1::StreamOptions extras;
    extras.set_grayscale(true);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "txt", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "grayscale render ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(!run.pages.empty(), "grayscale page painted");
    require(run.pages[0].png().rfind("\x89PNG", 0) == 0,
            "grayscale page still encodes PNG");
  }
  {
    officev1::StreamOptions extras;
    extras.set_vector_format(officev1::PAGE_VECTOR_FORMAT_SVG);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "txt", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "svg render ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(!run.pages.empty(), "svg page emitted");
    require(run.pages[0].format() == officev1::PAGE_IMAGE_FORMAT_SVG,
            "svg page format named");
    require(run.pages[0].png().find("<svg") != std::string::npos,
            "svg payload carries an <svg tag");
    // The raster fallback (PNG wrapped in an SVG) must announce itself in
    // the status warnings, exactly once; a true vector page must not.
    const bool wrapped =
        run.pages[0].png().find("data:image/png;base64,") != std::string::npos;
    int downgrade_warnings = 0;
    require(run.got_status, "svg render carries a final status");
    for (const std::string& warning : run.status.warnings()) {
      if (warning.find("vector SVG unavailable") != std::string::npos) {
        downgrade_warnings++;
      }
    }
    require(downgrade_warnings == (wrapped ? 1 : 0),
            "raster-fallback warning matches the payload kind");
  }
  {
    // PAGE_VECTOR_FORMAT_NONE forces raster even when page_format names
    // SVG; the implied-SVG mapping must not override the explicit NONE.
    officev1::StreamOptions extras;
    extras.set_vector_format(officev1::PAGE_VECTOR_FORMAT_NONE);
    extras.set_page_format(officev1::PAGE_IMAGE_FORMAT_SVG);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "txt", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "vector none render ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(!run.pages.empty(), "vector none page painted");
    require(run.pages[0].format() != officev1::PAGE_IMAGE_FORMAT_SVG,
            "explicit NONE keeps raster despite page_format SVG");
    require(run.pages[0].png().rfind("\x89PNG", 0) == 0,
            "vector none page is PNG");
  }
}

// The all-but-pages parts token, ToDocument's default: typed content
// streams, page images do not.
void verify_all_but_pages_token() {
  std::vector<std::string> payloads;
  auto outcome = run_with_parts("pages", "txt", "Text without page images.\n",
                                "all-but-pages", &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "all-but-pages renders ok: " + outcome.detail);
  PagesRun run = fold_pages(payloads);
  require(run.pages.empty(), "all-but-pages emits no page images");
  require(!run.paragraphs.empty(), "all-but-pages still emits paragraphs");
  require(run.info.page_count() >= 1, "page count still reported");
  require(run.got_status && run.status.state() == officev1::RenderStatus::STATE_OK,
          "all-but-pages status ok");
}

// Redaction on page images: a span in the middle of the text must change
// the painted page. Guards the span-overlap fix; the old fallback only
// looked at a paragraph's first character.
void verify_redact_spans_change_pages() {
  const std::string doc =
      "First line of the document.\n"
      "Second line holds the SECRET-VALUE to hide.\n"
      "Third line stays visible.\n";
  std::vector<std::string> baseline_payloads;
  auto baseline_outcome =
      run_with_parts("pages", "txt", doc, "1", &baseline_payloads);
  require(baseline_outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "baseline render ok: " + baseline_outcome.detail);
  PagesRun baseline = fold_pages(baseline_payloads);
  require(!baseline.pages.empty(), "baseline page painted");

  officev1::StreamOptions extras;
  officev1::TextSpan* span = extras.add_redact_spans();
  // Inside the second paragraph, nowhere near a paragraph start.
  const std::string flat =
      "First line of the document.Second line holds the SECRET-VALUE";
  span->set_char_start(static_cast<int64_t>(flat.find("SECRET-VALUE")));
  span->set_char_end(span->char_start() + 12);
  std::vector<std::string> payloads;
  auto outcome = run_with_extras("pages", "txt", doc, extras, &payloads, "1");
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "redacted render ok: " + outcome.detail);
  PagesRun redacted = fold_pages(payloads);
  require(!redacted.pages.empty(), "redacted page painted");
  require(redacted.pages[0].png() != baseline.pages[0].png(),
          "mid-paragraph redact span changes the painted page");
}

// Redaction on PDF export: the exported page must carry a filled black
// box where the text was. Glyph strokes never produce a long horizontal
// black run; a redaction rectangle does.
void verify_pdf_redaction_paints_black_box() {
  const std::string doc = "Hello over gRPC, this line gets redacted.\n";
  std::vector<std::string> baseline_payloads;
  auto baseline_outcome = run("pdf", "txt", doc, &baseline_payloads);
  require(baseline_outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "baseline pdf ok: " + baseline_outcome.detail);
  std::vector<std::string> baseline_pages =
      rasterize_pdf(fold_pdf(baseline_payloads));
  require(!baseline_pages.empty(), "baseline pdf rasterizes");
  require(longest_black_run(baseline_pages[0]) < 50,
          "un-redacted pdf has no long black run");

  officev1::StreamOptions extras;
  officev1::TextSpan* span = extras.add_redact_spans();
  span->set_char_start(0);
  span->set_char_end(5);
  std::vector<std::string> payloads;
  auto outcome = run_with_extras("pdf", "txt", doc, extras, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "redacted pdf ok: " + outcome.detail);
  std::vector<std::string> pages = rasterize_pdf(fold_pdf(payloads));
  require(!pages.empty(), "redacted pdf rasterizes");
  require(longest_black_run(pages[0]) >= 50,
          "redacted pdf carries a filled black box");
}

// The PDF page range: a multi-page document exported 1:1 yields exactly
// one page.
void verify_pdf_page_range() {
  std::string doc;
  for (int line = 0; line < 200; line++) {
    doc += "Line " + std::to_string(line) + " pads the document out.\n";
  }
  std::vector<std::string> full_payloads;
  auto full_outcome = run("pdf", "txt", doc, &full_payloads);
  require(full_outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "full pdf ok: " + full_outcome.detail);
  std::vector<std::string> full_pages = rasterize_pdf(fold_pdf(full_payloads));
  require(full_pages.size() >= 2, "padded document spans multiple pages");

  officev1::StreamOptions extras;
  extras.set_first_page(1);
  extras.set_last_page(1);
  std::vector<std::string> payloads;
  auto outcome = run_with_extras("pdf", "txt", doc, extras, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "ranged pdf ok: " + outcome.detail);
  std::vector<std::string> pages = rasterize_pdf(fold_pdf(payloads));
  require(pages.size() == 1, "1:1 range exports exactly one page, got "
                                 + std::to_string(pages.size()));
}

// skip_hidden on the pdf path. The office core's pdf filter never exports
// the hidden sheet, flag or no flag; what skip_hidden adds is a
// DocumentInfo that describes only the visible set, so page_count matches
// the PDF instead of counting sheets the PDF does not contain.
void verify_pdf_skip_hidden() {
  {
    std::vector<std::string> payloads;
    auto outcome = run("pdf", "fods", kTwoSheetFods, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "default pdf ok: " + outcome.detail);
    require(pdf_info(payloads).page_count() == 2,
            "default DocumentInfo counts the hidden sheet");
    require(rasterize_pdf(fold_pdf(payloads)).size() == 1,
            "pdf filter omits the hidden sheet by default");
  }
  {
    officev1::StreamOptions extras;
    extras.set_skip_hidden(true);
    std::vector<std::string> payloads;
    auto outcome =
        run_with_extras("pdf", "fods", kTwoSheetFods, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "skip_hidden pdf ok: " + outcome.detail);
    require(pdf_info(payloads).page_count() == 1,
            "skip_hidden DocumentInfo matches the exported PDF");
    require(rasterize_pdf(fold_pdf(payloads)).size() == 1,
            "skip_hidden PDF has one page");
  }
}

// Sheet visibility and used-range cropping on a flat ODS with a hidden
// second sheet and data away from A1.
void verify_sheet_visibility_and_used_range() {
  const std::string doc = kTwoSheetFods;
  std::vector<std::string> default_payloads;
  auto default_outcome = run("pages", "fods", doc, &default_payloads);
  require(default_outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "fods renders ok: " + default_outcome.detail);
  PagesRun everything = fold_pages(default_payloads);
  require(everything.info.page_count() == 2, "both sheets are pages");

  {
    officev1::StreamOptions extras;
    extras.set_skip_hidden(true);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "fods", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "skip_hidden renders ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(run.info.page_count() == 1, "hidden sheet dropped from pages");
    require(run.pages.size() == 1, "one page image painted");
  }
  {
    officev1::StreamOptions extras;
    extras.set_paint_used_range(true);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "fods", doc, extras, &payloads);
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "used range renders ok: " + outcome.detail);
    PagesRun run = fold_pages(payloads);
    require(!run.info.page_rects().empty(), "used-range page rect present");
    // Data sits at C5, so the crop's origin must shift right and down
    // past the empty leading columns and rows.
    require(run.info.page_rects(0).x_twips() > 0,
            "used range shifts the crop origin right");
    require(run.info.page_rects(0).y_twips() > 0,
            "used range shifts the crop origin down");
    require(run.info.page_rects(0).width_twips()
                < everything.info.page_rects(0).width_twips(),
            "used range narrows the painted sheet");
  }
}

// Notes pages ride behind their slides as extra page images.
void verify_notes_pages() {
  const std::string doc = kNotesFodp;
  std::vector<std::string> default_payloads;
  auto default_outcome = run("pages", "fodp", doc, &default_payloads);
  require(default_outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "fodp renders ok: " + default_outcome.detail);
  PagesRun slides_only = fold_pages(default_payloads);
  require(slides_only.info.page_count() == 1, "one slide by default");

  officev1::StreamOptions extras;
  extras.set_include_notes_pages(true);
  std::vector<std::string> payloads;
  auto outcome = run_with_extras("pages", "fodp", doc, extras, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "notes render ok: " + outcome.detail);
  PagesRun run = fold_pages(payloads);
  require(run.info.page_count() == 2, "slide plus its notes page");
  require(run.pages.size() == 2, "both pages painted");
  require(run.pages[0].png() != run.pages[1].png(),
          "notes page differs from its slide");
}

// Tracked-change display resolves before extraction: FINAL keeps the
// tracked insertion, ORIGINAL rejects it.
void verify_tracked_change_display() {
  const std::string doc = kTrackedChangeFodt;
  {
    officev1::StreamOptions extras;
    extras.set_tracked_changes(officev1::TRACKED_CHANGE_DISPLAY_FINAL);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "fodt", doc, extras, &payloads, "3");
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "final display renders ok: " + outcome.detail);
    std::string text = all_paragraph_text(fold_pages(payloads));
    require(text.find("INSERTED") != std::string::npos,
            "FINAL keeps the tracked insertion");
  }
  {
    officev1::StreamOptions extras;
    extras.set_tracked_changes(officev1::TRACKED_CHANGE_DISPLAY_ORIGINAL);
    std::vector<std::string> payloads;
    auto outcome = run_with_extras("pages", "fodt", doc, extras, &payloads, "3");
    require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
            "original display renders ok: " + outcome.detail);
    std::string text = all_paragraph_text(fold_pages(payloads));
    require(text.find("INSERTED") == std::string::npos,
            "ORIGINAL rejects the tracked insertion");
    require(text.find("Alpha") != std::string::npos,
            "ORIGINAL keeps the stored text");
  }
}

// Form values naming no existing field must degrade to nothing: same
// render, no crash, status still OK.
void verify_unknown_form_value_is_harmless() {
  officev1::StreamOptions extras;
  officev1::FormFillValue* value = extras.add_form_values();
  value->set_name("no-such-field");
  value->set_value("ignored");
  std::vector<std::string> payloads;
  auto outcome = run_with_extras("pages", "txt", "No form fields here.\n",
                                 extras, &payloads);
  require(outcome.kind == grlibre::WorkerOutcome::Kind::kOk,
          "unknown form value renders ok: " + outcome.detail);
  PagesRun run = fold_pages(payloads);
  require(run.got_status && run.status.state() == officev1::RenderStatus::STATE_OK,
          "unknown form value keeps status ok");
}

int main() {
  if (!std::filesystem::exists(lo_install_path())) {
    std::cerr << "SKIP: no LibreOffice at " << lo_install_path() << "\n";
    return 77;
  }
  verify_text_pages();
  verify_csv_is_spreadsheet();
  verify_tsv_is_spreadsheet();
  verify_pdf_mode();
  verify_pdf_chunk_streaming();
  verify_typed_content();
  verify_typed_spreadsheet();
  verify_draw_shapes();
  verify_typed_presentation();
  verify_part_selection();
  verify_embedded_objects();
  verify_line_rects();
  verify_docling_mapping();
  verify_marks_content();
  verify_work_dir_stays_documentless();
  verify_disk_work_dir_is_refused();
  verify_corrupt_zip_is_load_failure();
  verify_broken_package_needs_repair_opt_in();
  verify_html_renders_and_exits_promptly();
  verify_stream_option_extras();
  verify_all_but_pages_token();
  verify_redact_spans_change_pages();
  verify_pdf_redaction_paints_black_box();
  verify_pdf_page_range();
  verify_pdf_skip_hidden();
  verify_sheet_visibility_and_used_range();
  verify_notes_pages();
  verify_tracked_change_display();
  verify_unknown_form_value_is_harmless();
  verify_death_before_status_is_crash();
  verify_hung_worker_is_killed_at_deadline();
  verify_eof_without_exit_is_reaped();
  std::cout << "worker-render-test passed\n";
  return 0;
}
