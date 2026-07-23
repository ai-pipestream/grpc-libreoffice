// Golden-stream tests for the consumer-side docling mapper: hand-authored
// StreamPagesResponse sequences in, one docling-parity Document out. No
// LibreOffice involved; the worker-driven integration lives in
// worker_render_test.cpp.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "docling_map.h"

namespace {

namespace officev1 = ai::pipestream::office::v1;
namespace docv1 = ai::pipestream::document::v1;

void require(bool condition, const std::string& what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}

void require_integrity(const grlibre::DoclingMapper& mapper,
                       const std::string& what) {
  std::vector<std::string> errors =
      grlibre::docling_integrity_errors(mapper.document());
  for (const std::string& error : errors) {
    std::cerr << "integrity: " << error << "\n";
  }
  require(errors.empty(), what + ": ref tree is well formed");
}

const docv1::TextItemBase& base_of(const docv1::BaseTextItem& item) {
  switch (item.item_case()) {
    case docv1::BaseTextItem::kTitle: return item.title().base();
    case docv1::BaseTextItem::kSectionHeader:
      return item.section_header().base();
    case docv1::BaseTextItem::kListItem: return item.list_item().base();
    case docv1::BaseTextItem::kFormula: return item.formula().base();
    default: return item.text().base();
  }
}

// Finds the first text item carrying the given label.
const docv1::TextItemBase* find_text(const docv1::Document& document,
                                     docv1::DocItemLabel label) {
  for (const docv1::BaseTextItem& item : document.texts()) {
    const docv1::TextItemBase& base = base_of(item);
    if (base.label() == label) return &base;
  }
  return nullptr;
}

officev1::StreamPagesResponse info_event(const std::string& type, int pages,
                                         long page_height) {
  officev1::StreamPagesResponse event;
  officev1::DocumentInfo* info = event.mutable_document_info();
  info->set_document_id("doc-1");
  info->set_source_format("fodt");
  info->set_document_type(type);
  info->set_page_count(pages);
  for (int i = 0; i < pages; i++) {
    officev1::PageRect* rect = info->add_page_rects();
    rect->set_x_twips(284);
    rect->set_y_twips(284 + i * (page_height + 568));
    rect->set_width_twips(12240);
    rect->set_height_twips(page_height);
  }
  return event;
}

officev1::StreamPagesResponse status_event(const std::string& warning) {
  officev1::StreamPagesResponse event;
  event.mutable_status()->set_state(officev1::RenderStatus::STATE_OK);
  if (!warning.empty()) event.mutable_status()->add_warnings(warning);
  return event;
}

void add_run(officev1::Paragraph* paragraph, const std::string& text,
             long long offset) {
  officev1::TextRun* run = paragraph->add_runs();
  run->set_text(text);
  run->set_char_offset(offset);
  run->set_char_length(static_cast<long long>(text.size()));
}

void verify_writer_stream() {
  grlibre::DoclingMapper mapper;
  mapper.consume(info_event("text", 2, 15840));

  {
    officev1::StreamPagesResponse event;
    officev1::DocumentMetadata* meta = event.mutable_metadata();
    meta->set_title("Mapped Title");
    meta->set_author("Author A");
    meta->set_language("en-US");
    (*meta->mutable_statistics())["WordCount"] = 42;
    mapper.consume(event);
  }
  {
    // The document title paragraph.
    officev1::StreamPagesResponse event;
    officev1::Paragraph* paragraph = event.mutable_paragraph();
    paragraph->set_style("Title");
    paragraph->set_list_level(-1);
    paragraph->set_char_offset(0);
    add_run(paragraph, "Mapped Title", 0);
    mapper.consume(event);
  }
  {
    // A level-2 heading.
    officev1::StreamPagesResponse event;
    officev1::Paragraph* paragraph = event.mutable_paragraph();
    paragraph->set_style("Heading 2");
    paragraph->set_outline_level(2);
    paragraph->set_list_level(-1);
    paragraph->set_char_offset(13);
    add_run(paragraph, "Section", 13);
    mapper.consume(event);
  }
  {
    // A list item.
    officev1::StreamPagesResponse event;
    officev1::Paragraph* paragraph = event.mutable_paragraph();
    paragraph->set_list_level(0);
    paragraph->set_char_offset(21);
    add_run(paragraph, "First entry", 21);
    mapper.consume(event);
  }
  {
    // A body paragraph wrapping across both pages, with measured lines.
    officev1::StreamPagesResponse event;
    officev1::Paragraph* paragraph = event.mutable_paragraph();
    paragraph->set_list_level(-1);
    paragraph->set_page_index(0);
    paragraph->set_char_offset(33);
    add_run(paragraph, "Body text that wraps.", 33);
    officev1::LineBox* first = paragraph->add_line_rects();
    first->set_page_index(0);
    first->set_x_twips(1417);
    first->set_y_twips(1417);
    first->set_width_twips(9000);
    first->set_height_twips(276);
    officev1::LineBox* second = paragraph->add_line_rects();
    second->set_page_index(1);
    second->set_x_twips(1417);
    second->set_y_twips(18109);
    second->set_width_twips(4000);
    second->set_height_twips(276);
    mapper.consume(event);
  }
  {
    // A 2x2 table with one split cell outside the base grid.
    officev1::StreamPagesResponse event;
    officev1::TableData* table = event.mutable_table();
    table->set_rows(2);
    table->set_columns(2);
    table->set_page_index(0);
    const char* names[] = {"A1", "B1", "A2", "B2"};
    for (int i = 0; i < 4; i++) {
      officev1::TableCellData* cell = table->add_cells();
      cell->set_row(i / 2);
      cell->set_column(i % 2);
      cell->set_name(names[i]);
      cell->set_text("cell " + std::string(names[i]));
    }
    officev1::TableCellData* split = table->add_cells();
    split->set_row(-1);
    split->set_column(-1);
    split->set_name("B2.1");
    split->set_text("split text");
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::EmbeddedImage* image = event.mutable_embedded_image();
    image->set_page_index(0);
    image->set_name("Image1");
    image->set_mime_type("image/png");
    image->set_data("PNGBYTES");
    image->mutable_anchor()->set_x(2000);
    image->mutable_anchor()->set_y(3000);
    image->set_width_twips(1440);
    image->set_height_twips(720);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::Footnote* footnote = event.mutable_footnote();
    footnote->set_label("1");
    footnote->set_page_index(0);
    officev1::TextRun* run = footnote->add_runs();
    run->set_text("The note.");
    run->set_char_offset(-1);
    run->set_char_length(9);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::HeaderFooter* header = event.mutable_header_footer();
    header->set_page_style("Standard");
    officev1::Paragraph* paragraph = header->add_paragraphs();
    officev1::TextRun* run = paragraph->add_runs();
    run->set_text("Running header");
    run->set_char_offset(-1);
    run->set_char_length(14);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::TextFrame* frame = event.mutable_text_frame();
    frame->set_name("Frame1");
    frame->set_page_index(0);
    frame->mutable_anchor()->set_x(8000);
    frame->mutable_anchor()->set_y(2000);
    frame->set_width_twips(2000);
    frame->set_height_twips(1000);
    frame->set_chain_next("Frame2");
    officev1::TextRun* run = frame->add_runs();
    run->set_text("Frame text");
    run->set_char_offset(-1);
    run->set_char_length(10);
    mapper.consume(event);
  }
  {
    // An embedded bar chart.
    officev1::StreamPagesResponse event;
    officev1::EmbeddedObject* object = event.mutable_embedded_object();
    object->set_kind(officev1::EMBEDDED_OBJECT_KIND_CHART);
    object->set_page_index(0);
    object->set_name("Chart1");
    object->mutable_anchor()->set_x(3000);
    object->mutable_anchor()->set_y(9000);
    object->set_width_twips(5000);
    object->set_height_twips(3000);
    officev1::EmbeddedChart* chart = object->mutable_chart();
    chart->set_kind(officev1::EMBEDDED_CHART_KIND_BAR);
    chart->set_title("Sales");
    chart->add_categories("Q1");
    chart->add_categories("Q2");
    officev1::EmbeddedChartSeries* series = chart->add_series();
    series->set_label("Revenue");
    series->add_values_y(10.5);
    series->add_values_y(12.25);
    officev1::TableData* tabular = chart->mutable_tabular();
    tabular->set_rows(3);
    tabular->set_columns(2);
    mapper.consume(event);
  }
  {
    // An embedded formula.
    officev1::StreamPagesResponse event;
    officev1::EmbeddedObject* object = event.mutable_embedded_object();
    object->set_kind(officev1::EMBEDDED_OBJECT_KIND_FORMULA);
    object->set_page_index(1);
    object->set_formula("{a} over {b}");
    object->mutable_anchor()->set_x(2000);
    object->mutable_anchor()->set_y(17000);
    mapper.consume(event);
  }
  mapper.consume(status_event("one warning"));

  require(mapper.finished(), "writer: mapper finished");
  require(mapper.warnings().size() == 1 && mapper.warnings()[0] == "one warning",
          "writer: status warnings surfaced");
  require_integrity(mapper, "writer");
  const docv1::Document& document = mapper.document();

  require(document.name() == "Mapped Title", "writer: name from metadata title");
  require(document.origin().mimetype()
              == "application/vnd.oasis.opendocument.text",
          "writer: origin mimetype from source format");
  require(document.origin().filename() == "doc-1",
          "writer: origin filename from document id");
  require(document.body().meta().language().code() ==
              docv1::HUMAN_LANGUAGE_LABEL_EN,
          "writer: language code parsed from tag");
  require(document.pages_size() == 2, "writer: one PageItem per page rect");
  require(document.pages().at(1).size().height() == 15840.0,
          "writer: page size from page rect");

  const docv1::TextItemBase* title =
      find_text(document, docv1::DOC_ITEM_LABEL_TITLE);
  require(title != nullptr && title->text() == "Mapped Title",
          "writer: Title style paragraph becomes TitleItem");
  const docv1::TextItemBase* heading =
      find_text(document, docv1::DOC_ITEM_LABEL_SECTION_HEADER);
  require(heading != nullptr, "writer: heading becomes SectionHeaderItem");
  bool level_ok = false;
  for (const docv1::BaseTextItem& item : document.texts()) {
    if (item.item_case() == docv1::BaseTextItem::kSectionHeader) {
      level_ok = item.section_header().level() == 2;
    }
  }
  require(level_ok, "writer: section header level equals outline level");
  require(find_text(document, docv1::DOC_ITEM_LABEL_LIST_ITEM) != nullptr,
          "writer: list paragraph becomes ListItem");

  // The wrapping paragraph: one prov per measured line, page-local, TOPLEFT.
  const docv1::TextItemBase* body = nullptr;
  for (const docv1::BaseTextItem& item : document.texts()) {
    const docv1::TextItemBase& base = base_of(item);
    if (base.text() == "Body text that wraps.") body = &base;
  }
  require(body != nullptr && body->prov_size() == 2,
          "writer: one ProvenanceItem per line");
  require(body->prov(0).page_no() == 1 && body->prov(1).page_no() == 2,
          "writer: line prov page numbers");
  require(body->prov(0).bbox().coord_origin() == docv1::COORD_ORIGIN_TOPLEFT,
          "writer: line prov top-left origin");
  require(body->prov(0).bbox().l() == 1417.0 - 284.0,
          "writer: page origin subtracted from line box x");
  require(body->prov(1).bbox().t() == 18109.0 - (284.0 + 15840.0 + 568.0),
          "writer: second page origin subtracted from line box y");
  require(body->prov(1).bbox().t() >= 0
              && body->prov(1).bbox().b()
                  <= document.pages().at(2).size().height(),
          "writer: page-local box inside the page");
  require(body->prov(0).charspan().start() == 33
              && body->prov(0).charspan().end() == 54,
          "writer: body charspan in annotation space");

  // Table folding.
  require(document.tables_size() == 1, "writer: one TableItem");
  const docv1::TableItem& table = document.tables(0);
  require(table.data().num_rows() == 2 && table.data().num_cols() == 2,
          "writer: table dimensions");
  require(table.data().table_cells_size() == 4, "writer: four placed cells");
  require(table.data().grid_size() == 2
              && table.data().grid(1).cells(1).text() == "cell B2",
          "writer: grid populated");
  require(table.meta().custom_fields().count("cell:B2.1") == 1,
          "writer: split cell rides custom fields by name");

  // Pictures: embedded image plus the chart.
  require(document.pictures_size() == 2,
          "writer: image and chart become pictures");
  const docv1::PictureItem& image = document.pictures(0);
  require(image.label() == docv1::DOC_ITEM_LABEL_PICTURE
              && image.image().uri().rfind("data:image/png;base64,", 0) == 0,
          "writer: embedded image data URI");
  require(image.prov_size() == 1
              && image.prov(0).bbox().l() == 2000.0 - 284.0,
          "writer: image anchor page-local");
  const docv1::PictureItem& chart = document.pictures(1);
  require(chart.label() == docv1::DOC_ITEM_LABEL_CHART,
          "writer: chart labeled CHART");
  bool bar_seen = false;
  bool tabular_seen = false;
  for (const docv1::PictureAnnotation& annotation : chart.annotations()) {
    if (annotation.has_bar_chart()) {
      bar_seen = annotation.bar_chart().bars_size() == 2
          && annotation.bar_chart().bars(0).label() == "Q1"
          && annotation.bar_chart().bars(0).values() == 10.5;
    }
    if (annotation.has_tabular_chart()) tabular_seen = true;
  }
  require(bar_seen, "writer: bar chart annotation typed");
  require(tabular_seen, "writer: tabular chart annotation always present");

  // Formula.
  const docv1::TextItemBase* formula =
      find_text(document, docv1::DOC_ITEM_LABEL_FORMULA);
  require(formula != nullptr && formula->text() == "{a} over {b}",
          "writer: embedded formula becomes FormulaItem");

  // Furniture.
  const docv1::TextItemBase* header =
      find_text(document, docv1::DOC_ITEM_LABEL_PAGE_HEADER);
  require(header != nullptr
              && header->content_layer() == docv1::CONTENT_LAYER_FURNITURE
              && header->parent().ref() == "#/furniture",
          "writer: header lands in furniture");
  require(header->prov_size() == 0, "writer: furniture carries no prov");

  // Footnote stays in body.
  const docv1::TextItemBase* footnote =
      find_text(document, docv1::DOC_ITEM_LABEL_FOOTNOTE);
  require(footnote != nullptr
              && footnote->content_layer() == docv1::CONTENT_LAYER_BODY,
          "writer: footnote is body with FOOTNOTE label");

  // Text frame group.
  bool frame_group = false;
  for (const docv1::GroupItem& group : document.groups()) {
    if (group.name() == "Frame1") {
      frame_group = group.meta().custom_fields().count("chain_next") == 1
          && group.children_size() == 1;
    }
  }
  require(frame_group, "writer: frame group wraps its text with chain names");
}

void verify_calc_stream() {
  grlibre::DoclingMapper mapper;
  {
    officev1::StreamPagesResponse event = info_event("spreadsheet", 2, 20000);
    // Spreadsheet parts are page-local: origins zero.
    for (auto& rect : *event.mutable_document_info()->mutable_page_rects()) {
      rect.set_x_twips(0);
      rect.set_y_twips(0);
    }
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::Sheet* sheet = event.mutable_sheet();
    sheet->set_index(0);
    sheet->set_name("Data");
    sheet->set_visible(true);
    sheet->set_tab_color_rgb(-1);
    sheet->set_used_end_row(2);
    sheet->set_used_end_column(1);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetRow* row = event.mutable_sheet_row();
    row->set_sheet_index(0);
    row->set_row(0);
    officev1::SheetCell* merged = row->add_cells();
    merged->set_column(0);
    merged->set_type(officev1::SHEET_CELL_TYPE_TEXT);
    merged->set_display("Header");
    merged->set_merged_columns(2);
    merged->set_merged_rows(1);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetRow* row = event.mutable_sheet_row();
    row->set_sheet_index(0);
    row->set_row(2);
    officev1::SheetCell* value = row->add_cells();
    value->set_column(0);
    value->set_type(officev1::SHEET_CELL_TYPE_VALUE);
    value->set_number(3.5);
    value->set_display("3.5");
    value->set_merged_columns(1);
    value->set_merged_rows(1);
    officev1::SheetCell* formula = row->add_cells();
    formula->set_column(1);
    formula->set_type(officev1::SHEET_CELL_TYPE_FORMULA);
    formula->set_formula("=A3*2");
    formula->set_number(7.0);
    formula->set_display("7");
    formula->set_merged_columns(1);
    formula->set_merged_rows(1);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetCellComment* comment = event.mutable_sheet_cell_comment();
    comment->set_sheet_index(0);
    comment->set_row(2);
    comment->set_column(0);
    comment->set_author("Reviewer");
    comment->set_text("check this");
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetNamedRange* range = event.mutable_sheet_named_range();
    range->set_name("MyRange");
    range->set_content("$Data.$A$1:$B$3");
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetDatabaseRange* range = event.mutable_sheet_database_range();
    range->set_name("Orders");
    range->set_sheet_index(0);
    range->mutable_range()->set_end_row(2);
    range->mutable_range()->set_end_column(1);
    range->set_contains_header(true);
    range->set_auto_filter(true);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    officev1::SheetChart* chart = event.mutable_sheet_chart();
    chart->set_sheet_index(0);
    chart->set_name("Chart 1");
    officev1::SheetRangeRef* range = chart->add_ranges();
    range->set_end_row(2);
    range->set_end_column(1);
    mapper.consume(event);
  }
  {
    // A hidden second sheet.
    officev1::StreamPagesResponse event;
    officev1::Sheet* sheet = event.mutable_sheet();
    sheet->set_index(1);
    sheet->set_name("Hidden");
    sheet->set_visible(false);
    sheet->set_tab_color_rgb(-1);
    mapper.consume(event);
  }
  mapper.consume(status_event(""));

  require_integrity(mapper, "calc");
  const docv1::Document& document = mapper.document();

  int sheet_groups = 0;
  const docv1::GroupItem* hidden = nullptr;
  for (const docv1::GroupItem& group : document.groups()) {
    if (group.label() == docv1::GROUP_LABEL_SHEET) {
      sheet_groups++;
      if (group.name() == "Hidden") hidden = &group;
    }
  }
  require(sheet_groups == 2, "calc: one group per sheet");
  require(hidden != nullptr
              && hidden->content_layer() == docv1::CONTENT_LAYER_INVISIBLE,
          "calc: hidden sheet is invisible layer");
  require(document.tables_size() == 2, "calc: one table per sheet");
  const docv1::TableItem& table = document.tables(0);
  require(table.data().num_rows() == 3 && table.data().num_cols() == 2,
          "calc: dimensions from used bounds");
  bool merged_ok = false;
  bool value_ok = false;
  for (const docv1::TableCell& cell : table.data().table_cells()) {
    if (cell.start_row_offset_idx() == 0 && cell.start_col_offset_idx() == 0) {
      merged_ok = cell.col_span() == 2 && cell.text() == "Header";
    }
    if (cell.start_row_offset_idx() == 2 && cell.start_col_offset_idx() == 0) {
      value_ok = cell.text() == "3.5";
    }
  }
  require(merged_ok, "calc: merge span mapped");
  require(value_ok, "calc: value cell display text mapped");
  require(table.meta().custom_fields().count("A3") == 1
              && table.meta().custom_fields().at("A3").number_value() == 3.5,
          "calc: numeric value rides custom fields by A1 name");
  require(table.meta().custom_fields().count("B3") == 1
              && table.meta().custom_fields().at("B3").struct_value()
                      .fields().at("formula").string_value() == "=A3*2",
          "calc: formula rides custom fields by A1 name");
  require(table.comments_size() == 1, "calc: comment referenced from table");
  require(document.body().meta().custom_fields().count("named_range:MyRange")
              == 1,
          "calc: named range on body meta");
  require(document.body().meta().custom_fields().count("database_range:Orders")
              == 1,
          "calc: database range on body meta");
  {
    const auto& fields = document.body().meta().custom_fields()
        .at("database_range:Orders").struct_value().fields();
    require(fields.at("range").string_value() == "A1:B3"
                && fields.at("sheet_index").number_value() == 0
                && fields.at("contains_header").bool_value()
                && fields.at("auto_filter").bool_value()
                && !fields.at("totals_row").bool_value(),
            "calc: database range fields survive on body meta");
  }
  bool chart_ok = false;
  for (const docv1::PictureItem& picture : document.pictures()) {
    if (picture.label() == docv1::DOC_ITEM_LABEL_CHART) {
      chart_ok = picture.meta().custom_fields().at("source_ranges")
              .list_value().values(0).string_value() == "A1:B3";
    }
  }
  require(chart_ok, "calc: sheet chart with A1 source ranges");
}

void verify_impress_stream() {
  grlibre::DoclingMapper mapper;
  mapper.consume(info_event("presentation", 1, 21600));
  {
    officev1::StreamPagesResponse event;
    officev1::Slide* slide = event.mutable_slide();
    slide->set_index(0);
    slide->set_name("Slide 1");
    mapper.consume(event);
  }
  auto shape_event = [](officev1::PlaceholderRole role, bool notes) {
    officev1::StreamPagesResponse event;
    officev1::SlideShape* shape = event.mutable_slide_shape();
    shape->set_slide_index(0);
    shape->set_placeholder_role(role);
    shape->set_is_placeholder(role != officev1::PLACEHOLDER_ROLE_NONE);
    shape->set_notes(notes);
    shape->mutable_position()->set_x(1000);
    shape->mutable_position()->set_y(1000);
    shape->set_width_twips(8000);
    shape->set_height_twips(2000);
    return event;
  };
  {
    officev1::StreamPagesResponse event =
        shape_event(officev1::PLACEHOLDER_ROLE_TITLE, false);
    officev1::SlideTextParagraph* paragraph =
        event.mutable_slide_shape()->add_paragraphs();
    officev1::TextRun* run = paragraph->add_runs();
    run->set_text("Deck Title");
    run->set_char_offset(-1);
    run->set_char_length(10);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event =
        shape_event(officev1::PLACEHOLDER_ROLE_OUTLINE, false);
    officev1::SlideShape* shape = event.mutable_slide_shape();
    officev1::SlideTextParagraph* top = shape->add_paragraphs();
    top->set_outline_depth(0);
    officev1::TextRun* top_run = top->add_runs();
    top_run->set_text("Point one");
    top_run->set_char_offset(-1);
    top_run->set_char_length(9);
    officev1::SlideTextParagraph* nested = shape->add_paragraphs();
    nested->set_outline_depth(1);
    officev1::TextRun* nested_run = nested->add_runs();
    nested_run->set_text("Detail");
    nested_run->set_char_offset(-1);
    nested_run->set_char_length(6);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event =
        shape_event(officev1::PLACEHOLDER_ROLE_NOTES, true);
    officev1::SlideTextParagraph* paragraph =
        event.mutable_slide_shape()->add_paragraphs();
    officev1::TextRun* run = paragraph->add_runs();
    run->set_text("Speaker notes");
    run->set_char_offset(-1);
    run->set_char_length(13);
    mapper.consume(event);
  }
  {
    // An empty placeholder is skipped.
    officev1::StreamPagesResponse event =
        shape_event(officev1::PLACEHOLDER_ROLE_SUBTITLE, false);
    event.mutable_slide_shape()->set_is_empty_placeholder(true);
    mapper.consume(event);
  }
  mapper.consume(status_event(""));

  require_integrity(mapper, "impress");
  const docv1::Document& document = mapper.document();
  const docv1::GroupItem* slide = nullptr;
  for (const docv1::GroupItem& group : document.groups()) {
    if (group.label() == docv1::GROUP_LABEL_SLIDE) slide = &group;
  }
  require(slide != nullptr && slide->name() == "Slide 1",
          "impress: slide group");
  // Title + outline header + outline detail + notes.
  require(document.texts_size() == 4, "impress: empty placeholder skipped");
  const docv1::TextItemBase* title =
      find_text(document, docv1::DOC_ITEM_LABEL_TITLE);
  require(title != nullptr && title->parent().ref() == slide->self_ref(),
          "impress: title under slide group");
  require(title->prov_size() == 1 && title->prov(0).page_no() == 1
              && title->prov(0).bbox().l() == 1000.0,
          "impress: slide prov page-local as-is");
  require(find_text(document, docv1::DOC_ITEM_LABEL_SECTION_HEADER) != nullptr,
          "impress: outline depth 0 is a section header");
  require(find_text(document, docv1::DOC_ITEM_LABEL_LIST_ITEM) != nullptr,
          "impress: outline depth 1 is a list item");
  const docv1::TextItemBase* notes = nullptr;
  for (const docv1::BaseTextItem& item : document.texts()) {
    const docv1::TextItemBase& base = base_of(item);
    if (base.content_layer() == docv1::CONTENT_LAYER_NOTES) notes = &base;
  }
  require(notes != nullptr && notes->text() == "Speaker notes",
          "impress: notes shape on the notes layer");
  require(notes->prov_size() == 0, "impress: notes carry no slide-page prov");
}

void verify_draw_stream() {
  grlibre::DoclingMapper mapper;
  mapper.consume(info_event("drawing", 1, 21600));
  auto shape = [](int z, const std::string& path, const std::string& type,
                  bool group, bool text) {
    officev1::StreamPagesResponse event;
    officev1::DrawingShape* out = event.mutable_drawing_shape();
    out->set_page_index(0);
    out->set_z_order(z);
    out->set_group_path(path);
    out->set_shape_type(type);
    out->set_is_group(group);
    out->set_has_text(text);
    out->mutable_position()->set_x(100);
    out->mutable_position()->set_y(200);
    out->set_width_twips(1000);
    out->set_height_twips(500);
    if (text) {
      officev1::TextRun* run = out->add_runs();
      run->set_text("Label");
      run->set_char_offset(-1);
      run->set_char_length(5);
    }
    return event;
  };
  mapper.consume(shape(0, "", "com.sun.star.drawing.RectangleShape", false,
                       false));
  mapper.consume(shape(1, "", "com.sun.star.drawing.GroupShape", true, false));
  mapper.consume(shape(0, "1", "com.sun.star.drawing.TextShape", false, true));
  mapper.consume(status_event(""));

  require_integrity(mapper, "draw");
  const docv1::Document& document = mapper.document();
  require(document.groups_size() == 1
              && document.groups(0).label() == docv1::GROUP_LABEL_PICTURE_AREA,
          "draw: group shape becomes picture-area group");
  require(document.pictures_size() == 1
              && document.pictures(0).parent().ref() == "#/body",
          "draw: plain shape becomes a body picture");
  require(document.texts_size() == 1, "draw: text shape becomes a text item");
  const docv1::TextItemBase& text = base_of(document.texts(0));
  require(text.parent().ref() == document.groups(0).self_ref(),
          "draw: group_path attaches the child under its group");
  require(text.prov_size() == 1 && text.prov(0).bbox().l() == 100.0,
          "draw: page-local geometry kept as-is");
}

void verify_partial_stream() {
  grlibre::DoclingMapper mapper;
  {
    officev1::StreamPagesResponse event;
    officev1::DocumentInfo* info = event.mutable_document_info();
    info->set_source_format("fodt");
    info->set_document_type("text");
    info->set_page_count(1);
    mapper.consume(event);
  }
  {
    officev1::StreamPagesResponse event;
    event.mutable_metadata()->set_title("Only Metadata");
    mapper.consume(event);
  }
  mapper.consume(status_event(""));

  require_integrity(mapper, "partial");
  const docv1::Document& document = mapper.document();
  require(mapper.finished(), "partial: finished");
  require(document.name() == "Only Metadata", "partial: root populated");
  require(document.texts_size() == 0 && document.tables_size() == 0
              && document.pictures_size() == 0 && document.groups_size() == 0,
          "partial: arenas empty");
}

}  // namespace

int main() {
  verify_writer_stream();
  verify_calc_stream();
  verify_impress_stream();
  verify_draw_stream();
  verify_partial_stream();
  std::cout << "docling_map_test passed\n";
  return 0;
}
