// Runs the real grlibre-worker binary against a headless LibreOffice.
// Skips (exit 77) when soffice is not installed.

#include <cstdlib>
#include <cstring>
#include <filesystem>
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

std::string make_work_dir() {
  const char* base = std::getenv("TMPDIR");
  std::string pattern = std::string(base != nullptr ? base : "/tmp") + "/grlibre-test-XXXXXX";
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
                     para.line_rects(0).height_twips() > 0;
      }
      if (text.compare(0, 4, "wrap") == 0) {
        wrap_ok = para.line_rects_size() > 3;
        // Within one page, line boxes descend in reading order.
        for (int i = 1; i < para.line_rects_size(); i++) {
          if (para.line_rects(i).page_index() ==
              para.line_rects(i - 1).page_index()) {
            wrap_ok = wrap_ok && para.line_rects(i).y_twips() >
                                     para.line_rects(i - 1).y_twips();
          }
        }
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
  verify_typed_spreadsheet();
  verify_draw_shapes();
  verify_typed_presentation();
  verify_part_selection();
  verify_embedded_objects();
  verify_line_rects();
  verify_docling_mapping();
  verify_corrupt_zip_is_load_failure();
  std::cout << "worker-render-test passed\n";
  return 0;
}
