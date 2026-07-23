// Runs the real grlibre-worker binary against a headless LibreOffice.
// Skips (exit 77) when soffice is not installed.

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
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
  require(!frame_leaked_into_shapes, "frames are not double-counted as shapes");
  require(!out_of_body_text_in_paragraphs,
          "frame and shape text stays out of body paragraphs");
  officev1::StreamPagesResponse last;
  require(last.ParseFromString(payloads.back()), "typed last event parses");
  require(last.has_status(), "typed stream ends with status");
  require(last.status().warnings().empty(),
          "typed extraction produced no warnings");
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
  verify_draw_shapes();
  verify_typed_presentation();
  verify_part_selection();
  verify_corrupt_zip_is_load_failure();
  std::cout << "worker-render-test passed\n";
  return 0;
}
