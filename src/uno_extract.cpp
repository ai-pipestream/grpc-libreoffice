#include "uno_extract.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/UnknownPropertyException.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPageSupplier.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XMasterPageTarget.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/presentation/XPresentationPage.hpp>
#include <com/sun/star/style/XStyle.hpp>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/graphic/XGraphicProvider.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/io/XStream.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XDataPilotDescriptor.hpp>
#include <com/sun/star/sheet/XDataPilotTable.hpp>
#include <com/sun/star/sheet/XDataPilotTables.hpp>
#include <com/sun/star/sheet/XDataPilotTablesSupplier.hpp>
#include <com/sun/star/sheet/XNamedRange.hpp>
#include <com/sun/star/sheet/XPrintAreas.hpp>
#include <com/sun/star/sheet/XSheetAnnotation.hpp>
#include <com/sun/star/sheet/XSheetAnnotations.hpp>
#include <com/sun/star/sheet/XSheetAnnotationsSupplier.hpp>
#include <com/sun/star/sheet/XSheetCellCursor.hpp>
#include <com/sun/star/sheet/XSheetCellRange.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/sheet/XUsedAreaCursor.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/table/XTableChart.hpp>
#include <com/sun/star/table/XTableCharts.hpp>
#include <com/sun/star/table/XTableChartsSupplier.hpp>
#include <com/sun/star/table/XTableColumns.hpp>
#include <com/sun/star/table/XTableRows.hpp>
#include <com/sun/star/text/XSimpleText.hpp>
#include <com/sun/star/util/XMergeable.hpp>
#include <com/sun/star/util/XNumberFormats.hpp>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/text/XDocumentIndex.hpp>
#include <com/sun/star/text/XDocumentIndexesSupplier.hpp>
#include <com/sun/star/text/XEndnotesSupplier.hpp>
#include <com/sun/star/text/XFootnote.hpp>
#include <com/sun/star/text/XFootnotesSupplier.hpp>
#include <com/sun/star/text/XPageCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextColumns.hpp>
#include <com/sun/star/text/XTextFrame.hpp>
#include <com/sun/star/text/XTextFramesSupplier.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/util/Date.hpp>
#include <com/sun/star/util/DateTime.hpp>
#include <cppuhelper/implbase4.hxx>
#include <rtl/ref.hxx>

#include "ai/pipestream/office/v1/office_service.pb.h"

namespace grlibre {

namespace {

namespace css = ::com::sun::star;
namespace officev1 = ai::pipestream::office::v1;
using css::uno::Reference;
using css::uno::UNO_QUERY;
using EmitFn = std::function<bool(const google::protobuf::MessageLite&)>;

// 1/100 mm to twips: 1 inch = 2540 * (1/100 mm) = 1440 twips.
long hundredth_mm_to_twips(long value) { return value * 72 / 127; }

std::string utf8(const rtl::OUString& text) {
  rtl::OString bytes = rtl::OUStringToOString(text, RTL_TEXTENCODING_UTF8);
  return std::string(bytes.getStr(), static_cast<size_t>(bytes.getLength()));
}

// Collects extraction problems. Every problem is kept for the stream's
// RenderStatus.warnings and mirrored to stderr immediately, so a crash later
// in the walk cannot erase the trail.
class Warner {
 public:
  explicit Warner(std::vector<std::string>* sink) : sink_(sink) {}

  void warn(const std::string& context, const css::uno::Exception& error) {
    warn(context + ": " + utf8(error.Message));
  }

  void warn(const std::string& message) {
    std::fprintf(stderr, "grlibre-worker: typed content: %s\n", message.c_str());
    sink_->push_back("typed content: " + message);
  }

 private:
  std::vector<std::string>* sink_;
};

// The office core bootstrapped UNO in this process when LibreOfficeKit
// initialized; the process service factory's DefaultContext is the live
// component context. getProcessComponentContext itself returns an empty
// reference under LibreOfficeKit, so the factory route is the reliable one.
Reference<css::uno::XComponentContext> process_context() {
  using GetFactory = Reference<css::lang::XMultiServiceFactory> (*)();
  auto get_factory = reinterpret_cast<GetFactory>(
      dlsym(RTLD_DEFAULT, "_ZN10comphelper24getProcessServiceFactoryEv"));
  Reference<css::uno::XComponentContext> context;
  if (get_factory == nullptr) return context;
  Reference<css::beans::XPropertySet> props(get_factory(), UNO_QUERY);
  if (props.is()) props->getPropertyValue("DefaultContext") >>= context;
  return context;
}

// Collects graphic-provider output in memory; nothing touches a filesystem.
// The provider's stream helper unwraps an io::XStream and queries XSeekable
// from it, so this implements the full seekable read/write surface over one
// buffer.
class MemoryStream
    : public cppu::WeakImplHelper4<css::io::XStream, css::io::XInputStream,
                                   css::io::XOutputStream, css::io::XSeekable> {
 public:
  Reference<css::io::XInputStream> SAL_CALL getInputStream() override {
    return this;
  }
  Reference<css::io::XOutputStream> SAL_CALL getOutputStream() override {
    return this;
  }

  void SAL_CALL writeBytes(const css::uno::Sequence<sal_Int8>& data) override {
    size_t count = static_cast<size_t>(data.getLength());
    if (position_ + count > bytes_.size()) bytes_.resize(position_ + count);
    bytes_.replace(position_, count,
                   reinterpret_cast<const char*>(data.getConstArray()), count);
    position_ += count;
  }
  void SAL_CALL flush() override {}
  void SAL_CALL closeOutput() override {}

  sal_Int32 SAL_CALL readBytes(css::uno::Sequence<sal_Int8>& data,
                               sal_Int32 requested) override {
    size_t count = std::min(static_cast<size_t>(requested),
                            bytes_.size() - position_);
    data.realloc(static_cast<sal_Int32>(count));
    bytes_.copy(reinterpret_cast<char*>(data.getArray()), count, position_);
    position_ += count;
    return static_cast<sal_Int32>(count);
  }
  sal_Int32 SAL_CALL readSomeBytes(css::uno::Sequence<sal_Int8>& data,
                                   sal_Int32 requested) override {
    return readBytes(data, requested);
  }
  void SAL_CALL skipBytes(sal_Int32 count) override {
    position_ = std::min(bytes_.size(), position_ + static_cast<size_t>(count));
  }
  sal_Int32 SAL_CALL available() override {
    return static_cast<sal_Int32>(bytes_.size() - position_);
  }
  void SAL_CALL closeInput() override {}

  void SAL_CALL seek(sal_Int64 location) override {
    if (location < 0 || static_cast<size_t>(location) > bytes_.size()) {
      throw css::lang::IllegalArgumentException();
    }
    position_ = static_cast<size_t>(location);
  }
  sal_Int64 SAL_CALL getPosition() override {
    return static_cast<sal_Int64>(position_);
  }
  sal_Int64 SAL_CALL getLength() override {
    return static_cast<sal_Int64>(bytes_.size());
  }

  const std::string& bytes() const { return bytes_; }

 private:
  std::string bytes_;
  size_t position_ = 0;
};

int64_t datetime_epoch_ms(const css::util::DateTime& value) {
  if (value.Year == 0) return 0;
  struct tm parts = {};
  parts.tm_year = value.Year - 1900;
  parts.tm_mon = value.Month - 1;
  parts.tm_mday = value.Day;
  parts.tm_hour = value.Hours;
  parts.tm_min = value.Minutes;
  parts.tm_sec = value.Seconds;
  time_t seconds = timegm(&parts);
  if (seconds < 0) return 0;
  return static_cast<int64_t>(seconds) * 1000 +
         static_cast<int64_t>(value.NanoSeconds / 1000000);
}

bool emit_metadata(const Reference<css::frame::XModel>& model,
                   const EmitFn& emit_fn, Warner& warner) {
  Reference<css::document::XDocumentPropertiesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) {
    warner.warn("document model does not supply document properties");
    return true;
  }
  Reference<css::document::XDocumentProperties> props =
      supplier->getDocumentProperties();
  if (!props.is()) {
    warner.warn("document properties are missing");
    return true;
  }
  officev1::StreamPagesResponse event;
  officev1::DocumentMetadata* metadata = event.mutable_metadata();
  metadata->set_title(utf8(props->getTitle()));
  metadata->set_author(utf8(props->getAuthor()));
  metadata->set_subject(utf8(props->getSubject()));
  for (const rtl::OUString& keyword : props->getKeywords()) {
    metadata->add_keywords(utf8(keyword));
  }
  metadata->set_created_epoch_ms(datetime_epoch_ms(props->getCreationDate()));
  metadata->set_modified_epoch_ms(datetime_epoch_ms(props->getModificationDate()));
  metadata->set_modified_by(utf8(props->getModifiedBy()));
  metadata->set_generator(utf8(props->getGenerator()));
  metadata->set_editing_cycles(props->getEditingCycles());
  metadata->set_editing_duration_seconds(props->getEditingDuration());
  metadata->set_printed_epoch_ms(datetime_epoch_ms(props->getPrintDate()));
  metadata->set_printed_by(utf8(props->getPrintedBy()));
  css::lang::Locale locale = props->getLanguage();
  std::string language = utf8(locale.Language);
  if (!locale.Country.isEmpty()) language += "-" + utf8(locale.Country);
  metadata->set_language(language);
  metadata->set_template_name(utf8(props->getTemplateName()));
  for (const css::beans::NamedValue& stat : props->getDocumentStatistics()) {
    sal_Int32 count = 0;
    if (stat.Value >>= count) {
      (*metadata->mutable_statistics())[utf8(stat.Name)] = count;
    }
  }
  try {
    Reference<css::beans::XPropertySet> user_props(
        props->getUserDefinedProperties(), UNO_QUERY);
    if (user_props.is()) {
      for (const css::beans::Property& definition :
           user_props->getPropertySetInfo()->getProperties()) {
        css::uno::Any value = user_props->getPropertyValue(definition.Name);
        officev1::UserProperty* out = metadata->add_user_properties();
        out->set_name(utf8(definition.Name));
        rtl::OUString text;
        double number = 0;
        bool flag = false;
        css::util::DateTime datetime;
        css::util::Date date;
        if (value >>= text) {
          out->set_text(utf8(text));
        } else if (value >>= number) {
          out->set_number(number);
        } else if (value >>= flag) {
          out->set_flag(flag);
        } else if (value >>= datetime) {
          out->set_epoch_ms(datetime_epoch_ms(datetime));
        } else if (value >>= date) {
          css::util::DateTime midnight;
          midnight.Year = date.Year;
          midnight.Month = date.Month;
          midnight.Day = date.Day;
          out->set_epoch_ms(datetime_epoch_ms(midnight));
        } else {
          warner.warn("user property " + utf8(definition.Name) +
                      " has an unmapped type and was emitted without a value");
        }
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("user defined properties failed", error);
  }
  return emit_fn(event);
}

// Positions the view cursor at the range and reports the caret point and
// 0-based page. Reports failures against `what`.
void caret_at(const Reference<css::text::XTextViewCursor>& cursor,
              const Reference<css::text::XTextRange>& range,
              const std::string& what, officev1::TwipsPoint* point,
              int32_t* page_index, Warner& warner) {
  if (!cursor.is() || !range.is()) return;
  try {
    cursor->gotoRange(range, false);
    css::awt::Point position = cursor->getPosition();
    point->set_x(position.X);
    point->set_y(position.Y);
    if (page_index != nullptr) {
      Reference<css::text::XPageCursor> page_cursor(cursor, UNO_QUERY);
      if (page_cursor.is()) *page_index = page_cursor->getPage() - 1;
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("caret position of " + what + " failed", error);
  }
}

int64_t codepoints(const std::string& utf8_text) {
  int64_t count = 0;
  for (unsigned char byte : utf8_text) {
    if ((byte & 0xC0) != 0x80) count++;
  }
  return count;
}

// Appends one run per uniformly formatted text portion. When offset is null
// the runs are outside the body flow and carry char_offset -1; otherwise
// *offset is the running position in the annotation text space and advances
// by each run's length.
void fill_runs(const Reference<css::container::XEnumerationAccess>& paragraph,
               const std::string& label,
               google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
               int64_t* offset, Warner& warner) {
  Reference<css::container::XEnumeration> portions = paragraph->createEnumeration();
  int portion_index = 0;
  while (portions->hasMoreElements()) {
    css::uno::Any element = portions->nextElement();
    portion_index++;
    Reference<css::text::XTextRange> range(element, UNO_QUERY);
    Reference<css::beans::XPropertySet> props(element, UNO_QUERY);
    if (!range.is() || !props.is()) continue;
    try {
      rtl::OUString portion_type;
      props->getPropertyValue("TextPortionType") >>= portion_type;
      if (portion_type != "Text") continue;
      officev1::TextRun* run = runs->Add();
      run->set_text(utf8(range->getString()));
      int64_t length = codepoints(run->text());
      run->set_char_length(length);
      if (offset != nullptr) {
        run->set_char_offset(*offset);
        *offset += length;
      } else {
        run->set_char_offset(-1);
      }
      rtl::OUString font;
      props->getPropertyValue("CharFontName") >>= font;
      run->set_font(utf8(font));
      float size_pt = 0;
      props->getPropertyValue("CharHeight") >>= size_pt;
      run->set_size_pt(size_pt);
      float weight = 0;
      props->getPropertyValue("CharWeight") >>= weight;
      run->set_weight(weight);
      css::awt::FontSlant slant = css::awt::FontSlant_NONE;
      props->getPropertyValue("CharPosture") >>= slant;
      run->set_italic(slant == css::awt::FontSlant_ITALIC ||
                      slant == css::awt::FontSlant_OBLIQUE);
      sal_Int16 underline = 0;
      props->getPropertyValue("CharUnderline") >>= underline;
      run->set_underline(underline != 0);
      sal_Int16 strikeout = 0;
      props->getPropertyValue("CharStrikeout") >>= strikeout;
      run->set_strikethrough(strikeout != 0);
      sal_Int32 color = 0;
      props->getPropertyValue("CharColor") >>= color;
      run->set_color_rgb(color >= 0 ? static_cast<uint32_t>(color) : 0);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " portion " + std::to_string(portion_index - 1) +
                      " lost its run",
                  error);
    }
  }
}

// Fills one Paragraph event from a body or header/footer text element.
// Returns false when the element is not a paragraph. Geometry and offsets
// are only attached when the paragraph is in the body flow (cursor and
// offset non-null).
bool fill_paragraph(const css::uno::Any& element, int32_t index,
                    const Reference<css::text::XTextViewCursor>& cursor,
                    int64_t* offset, officev1::Paragraph* out, Warner& warner) {
  Reference<css::container::XEnumerationAccess> paragraph(element, UNO_QUERY);
  Reference<css::text::XTextRange> range(element, UNO_QUERY);
  if (!paragraph.is() || !range.is()) return false;
  std::string label = "paragraph " + std::to_string(index);
  out->set_index(index);
  out->set_page_index(-1);
  out->set_list_level(-1);
  out->set_page_number_offset(-1);
  out->set_char_offset(offset != nullptr ? *offset : -1);
  Reference<css::beans::XPropertySet> props(element, UNO_QUERY);
  if (props.is()) {
    try {
      rtl::OUString style;
      props->getPropertyValue("ParaStyleName") >>= style;
      out->set_style(utf8(style));
      sal_Int16 outline_level = 0;
      props->getPropertyValue("OutlineLevel") >>= outline_level;
      out->set_outline_level(outline_level);
      sal_Bool is_numbered = false;
      props->getPropertyValue("NumberingIsNumber") >>= is_numbered;
      sal_Int16 list_level = 0;
      props->getPropertyValue("NumberingLevel") >>= list_level;
      if (is_numbered) out->set_list_level(list_level);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " style query failed", error);
    }
    try {
      Reference<css::container::XNamed> section;
      props->getPropertyValue("TextSection") >>= section;
      if (section.is()) out->set_section(utf8(section->getName()));
      sal_Int16 page_number_offset = 0;
      if (props->getPropertyValue("PageNumberOffset") >>= page_number_offset) {
        out->set_page_number_offset(page_number_offset);
      }
    } catch (const css::beans::UnknownPropertyException&) {
      // Header and footer paragraphs have no section or numbering restart.
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " section query failed", error);
    }
  }
  if (cursor.is()) {
    int32_t page_index = -1;
    caret_at(cursor, range->getStart(), label + " start", out->mutable_start(),
             &page_index, warner);
    caret_at(cursor, range->getEnd(), label + " end", out->mutable_end(),
             nullptr, warner);
    out->set_page_index(page_index);
  }
  fill_runs(paragraph, label, out->mutable_runs(), offset, warner);
  if (offset != nullptr) *offset += 1;  // The newline after each body paragraph.
  return true;
}

// "B7" -> row 6, column 1. Split-cell names ("B7.1.2") report -1/-1; the
// name itself stays on the wire.
void parse_cell_name(const std::string& name, int32_t* row, int32_t* column) {
  *row = -1;
  *column = -1;
  size_t pos = 0;
  long col = 0;
  while (pos < name.size() && std::isupper(static_cast<unsigned char>(name[pos]))) {
    col = col * 26 + (name[pos] - 'A' + 1);
    pos++;
  }
  if (pos == 0 || pos >= name.size()) return;
  long row_number = 0;
  for (size_t digit = pos; digit < name.size(); digit++) {
    if (!std::isdigit(static_cast<unsigned char>(name[digit]))) return;
    row_number = row_number * 10 + (name[digit] - '0');
  }
  if (row_number <= 0) return;
  *row = static_cast<int32_t>(row_number - 1);
  *column = static_cast<int32_t>(col - 1);
}

bool emit_table(const Reference<css::text::XTextTable>& table, int32_t index,
                const Reference<css::text::XTextViewCursor>& cursor,
                const EmitFn& emit_fn, Warner& warner) {
  officev1::StreamPagesResponse event;
  officev1::TableData* out = event.mutable_table();
  out->set_index(index);
  out->set_page_index(-1);
  std::string label = "table " + std::to_string(index);
  try {
    out->set_rows(table->getRows()->getCount());
    out->set_columns(table->getColumns()->getCount());
    css::uno::Sequence<rtl::OUString> names = table->getCellNames();
    for (const rtl::OUString& name : names) {
      Reference<css::text::XText> cell(table->getCellByName(name), UNO_QUERY);
      if (!cell.is()) {
        warner.warn(label + " cell " + utf8(name) + " is not a text cell");
        continue;
      }
      officev1::TableCellData* cell_out = out->add_cells();
      std::string cell_name = utf8(name);
      int32_t row = -1;
      int32_t column = -1;
      parse_cell_name(cell_name, &row, &column);
      cell_out->set_row(row);
      cell_out->set_column(column);
      cell_out->set_name(cell_name);
      cell_out->set_text(utf8(cell->getString()));
    }
    if (names.hasElements()) {
      Reference<css::text::XText> first(table->getCellByName(names[0]), UNO_QUERY);
      Reference<css::text::XText> last(
          table->getCellByName(names[names.getLength() - 1]), UNO_QUERY);
      int32_t page_index = -1;
      if (first.is()) {
        caret_at(cursor, first->getStart(), label + " start",
                 out->mutable_start(), &page_index, warner);
      }
      if (last.is()) {
        caret_at(cursor, last->getEnd(), label + " end", out->mutable_end(),
                 nullptr, warner);
      }
      out->set_page_index(page_index);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " extraction failed", error);
  }
  return emit_fn(event);
}

// Flattens the paragraphs of an arbitrary text (footnote body, generated
// index, frame or shape text) into runs outside the annotation space.
void flatten_text_runs(const Reference<css::text::XText>& text,
                       const std::string& label,
                       google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
                       Warner& warner) {
  Reference<css::container::XEnumerationAccess> access(text, UNO_QUERY);
  if (!access.is()) return;
  Reference<css::container::XEnumeration> paragraphs = access->createEnumeration();
  while (paragraphs->hasMoreElements()) {
    Reference<css::container::XEnumerationAccess> paragraph(
        paragraphs->nextElement(), UNO_QUERY);
    if (paragraph.is()) fill_runs(paragraph, label, runs, nullptr, warner);
  }
}

// Creates the office core's graphic provider. Warns and returns an empty
// reference when unavailable, so callers still emit image metadata without
// bytes.
Reference<css::graphic::XGraphicProvider> graphic_provider(
    const Reference<css::uno::XComponentContext>& context, Warner& warner) {
  Reference<css::graphic::XGraphicProvider> provider;
  try {
    provider = Reference<css::graphic::XGraphicProvider>(
        context->getServiceManager()->createInstanceWithContext(
            "com.sun.star.graphic.GraphicProvider", context),
        UNO_QUERY);
  } catch (const css::uno::Exception& error) {
    warner.warn("graphic provider unavailable, image bytes will be missing",
                error);
  }
  return provider;
}

// Re-encodes a graphic through the provider entirely in memory, preferring
// the graphic's source format and falling back to PNG. Leaves mime_type and
// data empty when no encoding succeeds or no provider is available.
void encode_graphic(const Reference<css::graphic::XGraphic>& graphic,
                    const Reference<css::graphic::XGraphicProvider>& provider,
                    const std::string& label, std::string* mime_type,
                    std::string* data, Warner& warner) {
  if (!provider.is()) return;
  rtl::OUString mime;
  try {
    Reference<css::beans::XPropertySet> graphic_props(graphic, UNO_QUERY);
    if (graphic_props.is()) graphic_props->getPropertyValue("MimeType") >>= mime;
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: not every graphic knows its source format.
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " mime type query failed", error);
  }
  if (mime.isEmpty()) mime = "image/png";
  rtl::Reference<MemoryStream> sink(new MemoryStream);
  css::uno::Sequence<css::beans::PropertyValue> store_args(2);
  css::beans::PropertyValue* args = store_args.getArray();
  args[0].Name = "OutputStream";
  args[0].Value <<= Reference<css::io::XStream>(sink.get());
  args[1].Name = "MimeType";
  args[1].Value <<= mime;
  try {
    provider->storeGraphic(graphic, store_args);
    *mime_type = utf8(mime);
    *data = sink->bytes();
  } catch (const css::uno::Exception& original_error) {
    warner.warn(label + " does not round-trip as " + utf8(mime) +
                    ", re-encoding as image/png",
                original_error);
    rtl::Reference<MemoryStream> png_sink(new MemoryStream);
    args[0].Value <<= Reference<css::io::XStream>(png_sink.get());
    args[1].Value <<= rtl::OUString("image/png");
    try {
      provider->storeGraphic(graphic, store_args);
      *mime_type = "image/png";
      *data = png_sink->bytes();
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " could not be encoded at all", error);
    }
  }
}

// Walks the text document's single draw page once and classifies each
// shape: graphic objects emit EmbeddedImage (gated on IMAGES), draw-page
// SwXTextFrame flys are skipped because emit_text_frames owns them, and the
// remaining text-bearing shapes (custom shapes, text shapes, imported
// textboxes whose XText resolves to their hidden backing frame) emit Shape
// events (gated on SHAPES). Control shapes and empty-text shapes are
// skipped silently.
bool emit_draw_shapes(const Reference<css::text::XTextDocument>& text_doc,
                      const Reference<css::uno::XComponentContext>& context,
                      const Reference<css::text::XTextViewCursor>& cursor,
                      const PartSelection& parts, const EmitFn& emit_fn,
                      Warner& warner) {
  Reference<css::drawing::XDrawPageSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::container::XIndexAccess> shapes(supplier->getDrawPage(), UNO_QUERY);
  if (!shapes.is()) return true;
  bool want_images = parts.wants(officev1::DOCUMENT_PART_IMAGES);
  bool want_shapes = parts.wants(officev1::DOCUMENT_PART_SHAPES);
  Reference<css::graphic::XGraphicProvider> provider;
  if (want_images) provider = graphic_provider(context, warner);
  int32_t image_index = 0;
  int32_t shape_index = 0;
  for (sal_Int32 i = 0; i < shapes->getCount(); i++) {
    std::string label = "shape " + std::to_string(i);
    Reference<css::beans::XPropertySet> props;
    try {
      props = Reference<css::beans::XPropertySet>(shapes->getByIndex(i), UNO_QUERY);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " is not reachable", error);
      continue;
    }
    if (!props.is()) continue;
    Reference<css::lang::XServiceInfo> services(props, UNO_QUERY);
    if (!services.is()) continue;

    if (services->supportsService("com.sun.star.text.TextGraphicObject") ||
        services->supportsService("com.sun.star.drawing.GraphicObjectShape")) {
      if (!want_images) continue;
      Reference<css::graphic::XGraphic> graphic;
      try {
        props->getPropertyValue("Graphic") >>= graphic;
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " graphic query failed", error);
        continue;
      }
      if (!graphic.is()) continue;

      officev1::StreamPagesResponse event;
      officev1::EmbeddedImage* out = event.mutable_embedded_image();
      out->set_index(image_index);
      out->set_page_index(-1);
      Reference<css::container::XNamed> named(props, UNO_QUERY);
      if (named.is()) out->set_name(utf8(named->getName()));
      std::string image_label = "image " + std::to_string(image_index);

      encode_graphic(graphic, provider, image_label, out->mutable_mime_type(),
                     out->mutable_data(), warner);

      try {
        Reference<css::text::XTextContent> content(props, UNO_QUERY);
        if (content.is()) {
          int32_t page_index = -1;
          caret_at(cursor, content->getAnchor(), image_label + " anchor",
                   out->mutable_anchor(), &page_index, warner);
          out->set_page_index(page_index);
        }
        css::awt::Size layout_size;
        props->getPropertyValue("LayoutSize") >>= layout_size;
        out->set_width_twips(hundredth_mm_to_twips(layout_size.Width));
        out->set_height_twips(hundredth_mm_to_twips(layout_size.Height));
      } catch (const css::uno::Exception& error) {
        warner.warn(image_label + " geometry query failed", error);
      }

      if (!emit_fn(event)) return false;
      image_index++;
      continue;
    }

    // Writer text frames surface on the draw page as SwXTextFrame too;
    // emit_text_frames owns them, so skipping here is the dedup that keeps
    // each frame a single event.
    if (services->supportsService("com.sun.star.text.TextFrame")) continue;
    if (!want_shapes) continue;
    if (services->supportsService("com.sun.star.drawing.ControlShape")) {
      continue;
    }
    Reference<css::text::XText> text(props, UNO_QUERY);
    if (!text.is()) continue;
    std::string content_text;
    try {
      content_text = utf8(text->getString());
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " text query failed", error);
      continue;
    }
    if (content_text.empty()) continue;

    officev1::StreamPagesResponse event;
    officev1::Shape* out = event.mutable_shape();
    out->set_index(shape_index);
    out->set_page_index(-1);
    Reference<css::container::XNamed> named(props, UNO_QUERY);
    if (named.is()) out->set_name(utf8(named->getName()));
    Reference<css::drawing::XShape> shape(props, UNO_QUERY);
    if (shape.is()) {
      out->set_shape_type(utf8(shape->getShapeType()));
      try {
        css::awt::Size size = shape->getSize();
        out->set_width_twips(hundredth_mm_to_twips(size.Width));
        out->set_height_twips(hundredth_mm_to_twips(size.Height));
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " geometry query failed", error);
      }
    }
    try {
      Reference<css::text::XTextContent> content(props, UNO_QUERY);
      if (content.is()) {
        int32_t page_index = -1;
        caret_at(cursor, content->getAnchor(), label + " anchor",
                 out->mutable_anchor(), &page_index, warner);
        out->set_page_index(page_index);
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " anchor query failed", error);
    }
    // Linked textboxes carry the chain names on the shape; absence is the
    // normal case, so probe the property set first.
    try {
      Reference<css::beans::XPropertySetInfo> info = props->getPropertySetInfo();
      if (info.is() && info->hasPropertyByName("ChainNextName")) {
        rtl::OUString chain;
        props->getPropertyValue("ChainNextName") >>= chain;
        out->set_chain_next(utf8(chain));
      }
      if (info.is() && info->hasPropertyByName("ChainPrevName")) {
        rtl::OUString chain;
        props->getPropertyValue("ChainPrevName") >>= chain;
        out->set_chain_prev(utf8(chain));
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " chain query failed", error);
    }
    flatten_text_runs(text, label, out->mutable_runs(), warner);
    if (!emit_fn(event)) return false;
    shape_index++;
  }
  return true;
}

// Emits every Writer text frame with its runs, chain names, layout anchor,
// and laid-out geometry. Textbox-backing hidden frames are not in this
// enumeration (the office core excludes them); their text arrives through
// the draw-shape pass instead, so the two passes never overlap.
bool emit_text_frames(const Reference<css::text::XTextDocument>& text_doc,
                      const Reference<css::text::XTextViewCursor>& cursor,
                      const EmitFn& emit_fn, Warner& warner) {
  Reference<css::text::XTextFramesSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::container::XIndexAccess> frames(supplier->getTextFrames(),
                                                 UNO_QUERY);
  if (!frames.is()) return true;
  for (sal_Int32 i = 0; i < frames->getCount(); i++) {
    std::string label = "text frame " + std::to_string(i);
    officev1::StreamPagesResponse event;
    officev1::TextFrame* out = event.mutable_text_frame();
    out->set_index(i);
    out->set_page_index(-1);
    try {
      Reference<css::text::XTextFrame> frame(frames->getByIndex(i), UNO_QUERY);
      if (!frame.is()) {
        warner.warn(label + " is not a text frame object");
        continue;
      }
      Reference<css::container::XNamed> named(frame, UNO_QUERY);
      if (named.is()) out->set_name(utf8(named->getName()));
      Reference<css::text::XTextContent> content(frame, UNO_QUERY);
      if (content.is()) {
        int32_t page_index = -1;
        caret_at(cursor, content->getAnchor(), label + " anchor",
                 out->mutable_anchor(), &page_index, warner);
        out->set_page_index(page_index);
      }
      Reference<css::beans::XPropertySet> props(frame, UNO_QUERY);
      if (props.is()) {
        // Prefer the laid-out size; fall back to the model width and height.
        // All of these are 1/100 mm on the wire out of the office core.
        css::awt::Size size;
        size.Width = 0;
        size.Height = 0;
        try {
          css::awt::Size layout_size;
          if ((props->getPropertyValue("LayoutSize") >>= layout_size) &&
              layout_size.Width > 0 && layout_size.Height > 0) {
            size = layout_size;
          }
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: LayoutSize is an optional frame property.
        }
        if (size.Width == 0 && size.Height == 0) {
          sal_Int32 width = 0, height = 0;
          props->getPropertyValue("Width") >>= width;
          props->getPropertyValue("Height") >>= height;
          size.Width = width;
          size.Height = height;
        }
        out->set_width_twips(hundredth_mm_to_twips(size.Width));
        out->set_height_twips(hundredth_mm_to_twips(size.Height));
        // Chain names are maybevoid: present on every real frame, void or
        // empty when the frame is not chained.
        rtl::OUString chain;
        props->getPropertyValue("ChainNextName") >>= chain;
        out->set_chain_next(utf8(chain));
        chain = rtl::OUString();
        props->getPropertyValue("ChainPrevName") >>= chain;
        out->set_chain_prev(utf8(chain));
      }
      flatten_text_runs(frame->getText(), label, out->mutable_runs(), warner);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " extraction failed", error);
    }
    if (!emit_fn(event)) return false;
  }
  return true;
}

// Walks one shape container in paint order, emitting a DrawingShape per
// shape and recursing through groups. The container-scoped contract (one
// call per XShapes, group_path carrying nesting) is what presentation
// extraction will reuse. image_counter numbers the EmbeddedImage events
// emitted for image shapes across the whole document. DrawingShape events
// are gated on the SHAPES part and image bytes on the IMAGES part; groups
// are still recursed when only IMAGES is selected so nested image shapes
// are found.
bool emit_shapes(const Reference<css::drawing::XShapes>& shapes,
                 int32_t page_index, const std::string& group_path,
                 const Reference<css::graphic::XGraphicProvider>& provider,
                 int32_t* image_counter, const PartSelection& parts,
                 const EmitFn& emit_fn, Warner& warner) {
  bool want_shapes = parts.wants(officev1::DOCUMENT_PART_SHAPES);
  bool want_images = parts.wants(officev1::DOCUMENT_PART_IMAGES);
  for (sal_Int32 i = 0; i < shapes->getCount(); i++) {
    std::string shape_path = group_path.empty()
                                 ? std::to_string(i)
                                 : group_path + "/" + std::to_string(i);
    std::string label =
        "page " + std::to_string(page_index) + " shape " + shape_path;
    Reference<css::drawing::XShape> shape;
    try {
      shape = Reference<css::drawing::XShape>(shapes->getByIndex(i), UNO_QUERY);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " is not reachable", error);
      continue;
    }
    if (!shape.is()) continue;
    std::string shape_type = utf8(shape->getShapeType());
    // The DrawingShape event doubles as the scratch for the name and
    // geometry the image event reuses, so those cheap fields fill in even
    // when the SHAPES part is off; the expensive text-run walk stays gated.
    officev1::StreamPagesResponse event;
    officev1::DrawingShape* out = event.mutable_drawing_shape();
    out->set_page_index(page_index);
    out->set_z_order(i);
    out->set_group_path(group_path);
    out->set_shape_type(shape_type);
    Reference<css::container::XNamed> named(shape, UNO_QUERY);
    if (named.is()) out->set_name(utf8(named->getName()));
    try {
      css::awt::Point position = shape->getPosition();
      css::awt::Size size = shape->getSize();
      out->mutable_position()->set_x(hundredth_mm_to_twips(position.X));
      out->mutable_position()->set_y(hundredth_mm_to_twips(position.Y));
      out->set_width_twips(hundredth_mm_to_twips(size.Width));
      out->set_height_twips(hundredth_mm_to_twips(size.Height));
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " geometry query failed", error);
    }
    Reference<css::beans::XPropertySet> props(shape, UNO_QUERY);
    if (want_shapes && props.is()) {
      try {
        sal_Int32 rotation = 0;
        props->getPropertyValue("RotateAngle") >>= rotation;
        out->set_rotation(rotation);
      } catch (const css::beans::UnknownPropertyException&) {
        // Expected probe result: embedded objects and form controls carry no
        // rotation property.
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " rotation query failed", error);
      }
    }
    if (want_shapes) {
      Reference<css::text::XText> text(shape, UNO_QUERY);
      if (text.is()) {
        out->set_has_text(!text->getString().isEmpty());
        flatten_text_runs(text, label, out->mutable_runs(), warner);
      }
    }
    Reference<css::drawing::XShapes> children;
    if (shape_type == "com.sun.star.drawing.GroupShape") {
      children = Reference<css::drawing::XShapes>(shape, UNO_QUERY);
    }
    out->set_is_group(children.is());
    if (want_shapes && !emit_fn(event)) return false;
    if (children.is()) {
      if (!emit_shapes(children, page_index, shape_path, provider,
                       image_counter, parts, emit_fn, warner)) {
        return false;
      }
      continue;
    }
    if (want_images &&
        shape_type == "com.sun.star.drawing.GraphicObjectShape" && props.is()) {
      Reference<css::graphic::XGraphic> graphic;
      try {
        props->getPropertyValue("Graphic") >>= graphic;
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " graphic query failed", error);
      }
      if (graphic.is()) {
        officev1::StreamPagesResponse image_event;
        officev1::EmbeddedImage* image = image_event.mutable_embedded_image();
        image->set_index((*image_counter)++);
        image->set_page_index(page_index);
        image->set_name(out->name());
        image->set_width_twips(out->width_twips());
        image->set_height_twips(out->height_twips());
        encode_graphic(graphic, provider,
                       "image " + std::to_string(image->index()),
                       image->mutable_mime_type(), image->mutable_data(),
                       warner);
        if (!emit_fn(image_event)) return false;
      }
    }
  }
  return true;
}

// Emits every shape of a drawing document, page by page, walking the model
// the render pass already loaded and laid out. page_index is the draw-page
// index and matches PageImage.index.
bool emit_drawing_content(
    const Reference<css::drawing::XDrawPagesSupplier>& supplier,
    const Reference<css::uno::XComponentContext>& context,
    const PartSelection& parts, const EmitFn& emit_fn, Warner& warner) {
  Reference<css::drawing::XDrawPages> pages = supplier->getDrawPages();
  if (!pages.is()) {
    warner.warn("drawing document has no draw pages");
    return true;
  }
  Reference<css::graphic::XGraphicProvider> provider;
  if (parts.wants(officev1::DOCUMENT_PART_IMAGES)) {
    provider = graphic_provider(context, warner);
  }
  int32_t image_counter = 0;
  for (sal_Int32 p = 0; p < pages->getCount(); p++) {
    Reference<css::drawing::XShapes> page;
    try {
      page = Reference<css::drawing::XShapes>(pages->getByIndex(p), UNO_QUERY);
    } catch (const css::uno::Exception& error) {
      warner.warn("draw page " + std::to_string(p) + " is not reachable", error);
      continue;
    }
    if (!page.is()) continue;
    if (!emit_shapes(page, static_cast<int32_t>(p), "", provider,
                     &image_counter, parts, emit_fn, warner)) {
      return false;
    }
  }
  return true;
}

bool ends_with(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Maps the office core's shape type string to the placeholder role. The
// type string is the reliable discriminator: SdXShape does not advertise
// SubtitleShape or NotesShape through supportsService.
officev1::PlaceholderRole placeholder_role_for(const std::string& shape_type) {
  if (ends_with(shape_type, ".TitleTextShape")) {
    return officev1::PLACEHOLDER_ROLE_TITLE;
  }
  if (ends_with(shape_type, ".OutlinerShape")) {
    return officev1::PLACEHOLDER_ROLE_OUTLINE;
  }
  if (ends_with(shape_type, ".SubtitleShape")) {
    return officev1::PLACEHOLDER_ROLE_SUBTITLE;
  }
  if (ends_with(shape_type, ".NotesShape")) {
    return officev1::PLACEHOLDER_ROLE_NOTES;
  }
  return officev1::PLACEHOLDER_ROLE_NONE;
}

// Emits one SlideShape for a slide or notes-page shape and recurses through
// groups so nested placeholders keep their paint order. Graphic, OLE, chart,
// and table shapes emit only this header; their heavy content (image bytes,
// chart data, table grid) belongs to the embedded-objects work.
bool emit_slide_shape(const Reference<css::drawing::XShape>& shape,
                      int32_t slide_index, int32_t z_order, bool notes,
                      const EmitFn& emit_fn, Warner& warner) {
  std::string shape_type = utf8(shape->getShapeType());
  std::string label = "slide " + std::to_string(slide_index) +
                      (notes ? " notes shape " : " shape ") +
                      std::to_string(z_order);
  officev1::StreamPagesResponse event;
  officev1::SlideShape* out = event.mutable_slide_shape();
  out->set_slide_index(slide_index);
  out->set_z_order(z_order);
  out->set_shape_type(shape_type);
  out->set_placeholder_role(placeholder_role_for(shape_type));
  out->set_notes(notes);
  Reference<css::beans::XPropertySet> props(shape, UNO_QUERY);
  if (props.is()) {
    try {
      sal_Bool is_placeholder = false;
      props->getPropertyValue("IsPresentationObject") >>= is_placeholder;
      out->set_is_placeholder(is_placeholder);
      sal_Bool is_empty = false;
      props->getPropertyValue("IsEmptyPresentationObject") >>= is_empty;
      out->set_is_empty_placeholder(is_empty);
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: plain draw shapes carry no presentation
      // placeholder properties.
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " placeholder query failed", error);
    }
  }
  try {
    css::awt::Point position = shape->getPosition();
    css::awt::Size size = shape->getSize();
    out->mutable_position()->set_x(hundredth_mm_to_twips(position.X));
    out->mutable_position()->set_y(hundredth_mm_to_twips(position.Y));
    out->set_width_twips(hundredth_mm_to_twips(size.Width));
    out->set_height_twips(hundredth_mm_to_twips(size.Height));
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " geometry query failed", error);
  }
  Reference<css::text::XText> text(shape, UNO_QUERY);
  Reference<css::container::XEnumerationAccess> access(text, UNO_QUERY);
  if (access.is()) {
    Reference<css::container::XEnumeration> paragraphs =
        access->createEnumeration();
    int paragraph_index = 0;
    while (paragraphs->hasMoreElements()) {
      css::uno::Any element = paragraphs->nextElement();
      Reference<css::container::XEnumerationAccess> paragraph(element, UNO_QUERY);
      if (!paragraph.is()) continue;
      officev1::SlideTextParagraph* para = out->add_paragraphs();
      Reference<css::beans::XPropertySet> para_props(element, UNO_QUERY);
      if (para_props.is()) {
        try {
          sal_Int16 level = 0;
          para_props->getPropertyValue("NumberingLevel") >>= level;
          // The editeng outline-level pool default is -1; normalize unset
          // and negative depths to 0.
          para->set_outline_depth(std::max<sal_Int16>(0, level));
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: NumberingLevel is an optional paragraph
          // property.
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " paragraph " + std::to_string(paragraph_index) +
                          " outline depth query failed",
                      error);
        }
      }
      fill_runs(paragraph,
                label + " paragraph " + std::to_string(paragraph_index),
                para->mutable_runs(), nullptr, warner);
      paragraph_index++;
    }
  }
  if (!emit_fn(event)) return false;
  Reference<css::drawing::XShapes> children;
  if (ends_with(shape_type, ".GroupShape")) {
    children = Reference<css::drawing::XShapes>(shape, UNO_QUERY);
  }
  if (children.is()) {
    for (sal_Int32 i = 0; i < children->getCount(); i++) {
      Reference<css::drawing::XShape> child;
      try {
        child = Reference<css::drawing::XShape>(children->getByIndex(i),
                                                UNO_QUERY);
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " group child " + std::to_string(i) +
                        " is not reachable",
                    error);
        continue;
      }
      if (!child.is()) continue;
      if (!emit_slide_shape(child, slide_index, static_cast<int32_t>(i), notes,
                            emit_fn, warner)) {
        return false;
      }
    }
  }
  return true;
}

// Emits every slide of a presentation document in slide order: one Slide
// header, then its shapes in paint order, then the speaker-notes shape of
// its notes page. Slide.index matches the LibreOfficeKit part index and
// PageImage.index, so no correlation logic is needed.
bool emit_presentation_content(
    const Reference<css::drawing::XDrawPagesSupplier>& supplier,
    const EmitFn& emit_fn, Warner& warner) {
  Reference<css::drawing::XDrawPages> pages = supplier->getDrawPages();
  if (!pages.is()) {
    warner.warn("presentation document has no slides");
    return true;
  }
  for (sal_Int32 i = 0; i < pages->getCount(); i++) {
    std::string label = "slide " + std::to_string(i);
    Reference<css::drawing::XDrawPage> slide;
    try {
      slide = Reference<css::drawing::XDrawPage>(pages->getByIndex(i),
                                                 UNO_QUERY);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " is not reachable", error);
      continue;
    }
    if (!slide.is()) continue;

    officev1::StreamPagesResponse slide_event;
    officev1::Slide* out = slide_event.mutable_slide();
    out->set_index(static_cast<int32_t>(i));
    Reference<css::container::XNamed> named(slide, UNO_QUERY);
    if (named.is()) out->set_name(utf8(named->getName()));
    Reference<css::beans::XPropertySet> props(slide, UNO_QUERY);
    if (props.is()) {
      try {
        sal_Int16 layout = 0;
        props->getPropertyValue("Layout") >>= layout;
        out->set_layout(layout);
      } catch (const css::beans::UnknownPropertyException&) {
        // Expected probe result: Layout is an optional slide property.
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " layout query failed", error);
      }
    }
    try {
      Reference<css::drawing::XMasterPageTarget> target(slide, UNO_QUERY);
      if (target.is()) {
        Reference<css::container::XNamed> master(target->getMasterPage(),
                                                 UNO_QUERY);
        if (master.is()) out->set_master_page_name(utf8(master->getName()));
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " master page query failed", error);
    }
    if (!emit_fn(slide_event)) return false;

    Reference<css::drawing::XShapes> shapes(slide, UNO_QUERY);
    if (shapes.is()) {
      for (sal_Int32 z = 0; z < shapes->getCount(); z++) {
        Reference<css::drawing::XShape> shape;
        try {
          shape = Reference<css::drawing::XShape>(shapes->getByIndex(z),
                                                  UNO_QUERY);
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " shape " + std::to_string(z) +
                          " is not reachable",
                      error);
          continue;
        }
        if (!shape.is()) continue;
        if (!emit_slide_shape(shape, static_cast<int32_t>(i),
                              static_cast<int32_t>(z), false, emit_fn,
                              warner)) {
          return false;
        }
      }
    }

    // The notes page carries the speaker notes. Only the NotesShape is
    // emitted: the PageShape slide thumbnail and unfilled placeholders say
    // nothing.
    try {
      Reference<css::presentation::XPresentationPage> presentation(slide,
                                                                   UNO_QUERY);
      if (presentation.is()) {
        Reference<css::drawing::XShapes> notes_shapes(
            presentation->getNotesPage(), UNO_QUERY);
        if (notes_shapes.is()) {
          for (sal_Int32 z = 0; z < notes_shapes->getCount(); z++) {
            Reference<css::drawing::XShape> shape(notes_shapes->getByIndex(z),
                                                  UNO_QUERY);
            if (!shape.is()) continue;
            if (!ends_with(utf8(shape->getShapeType()), ".NotesShape")) {
              continue;
            }
            Reference<css::beans::XPropertySet> note_props(shape, UNO_QUERY);
            if (note_props.is()) {
              try {
                sal_Bool empty = false;
                note_props->getPropertyValue("IsEmptyPresentationObject") >>=
                    empty;
                if (empty) continue;
              } catch (const css::beans::UnknownPropertyException&) {
                // Expected probe result: not a placeholder object.
              }
            }
            if (!emit_slide_shape(shape, static_cast<int32_t>(i),
                                  static_cast<int32_t>(z), true, emit_fn,
                                  warner)) {
              return false;
            }
          }
        }
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " notes page walk failed", error);
    }
  }
  return true;
}

// Copies a cell-range address into the wire range ref.
void fill_range_ref(const css::table::CellRangeAddress& address,
                    officev1::SheetRangeRef* out) {
  out->set_start_row(address.StartRow);
  out->set_start_column(address.StartColumn);
  out->set_end_row(address.EndRow);
  out->set_end_column(address.EndColumn);
}

// Emits typed spreadsheet content from the live Calc model: workbook-scoped
// named ranges once, then per sheet a Sheet header, its used rows with
// non-empty typed cells, cell comments, charts, pivot tables, and draw-page
// images. Each event streams the moment it is parsed, and every walk is
// bounded to the sheet's used rectangle, so the full grid is never touched.
bool emit_calc_content(
    const Reference<css::sheet::XSpreadsheetDocument>& calc_doc,
    const Reference<css::frame::XModel>& model,
    const Reference<css::uno::XComponentContext>& context,
    const PartSelection& parts, const EmitFn& emit_fn, Warner& warner) {
  bool want_sheets = parts.wants(officev1::DOCUMENT_PART_SHEETS);
  bool want_images = parts.wants(officev1::DOCUMENT_PART_IMAGES);

  // Number-format codes resolve once per key through this cache. Key 0 is
  // General and stays an empty code on the wire.
  Reference<css::util::XNumberFormats> formats;
  if (want_sheets) {
    Reference<css::util::XNumberFormatsSupplier> supplier(model, UNO_QUERY);
    if (supplier.is()) formats = supplier->getNumberFormats();
  }
  std::map<sal_Int32, std::string> format_cache;
  auto format_code = [&](sal_Int32 key) -> std::string {
    if (key == 0 || !formats.is()) return std::string();
    auto found = format_cache.find(key);
    if (found != format_cache.end()) return found->second;
    std::string code;
    try {
      Reference<css::beans::XPropertySet> props = formats->getByKey(key);
      if (props.is()) {
        rtl::OUString text;
        props->getPropertyValue("FormatString") >>= text;
        code = utf8(text);
      }
    } catch (const css::uno::Exception& error) {
      warner.warn("number format " + std::to_string(key) + " query failed",
                  error);
    }
    format_cache[key] = code;
    return code;
  };

  if (want_sheets) {
    // Named ranges are a readonly property on the model, not a supplier
    // interface; the collection also answers XIndexAccess.
    try {
      Reference<css::beans::XPropertySet> doc_props(model, UNO_QUERY);
      if (doc_props.is()) {
        Reference<css::container::XIndexAccess> named;
        doc_props->getPropertyValue("NamedRanges") >>= named;
        if (named.is()) {
          for (sal_Int32 i = 0; i < named->getCount(); i++) {
            Reference<css::sheet::XNamedRange> range(named->getByIndex(i),
                                                     UNO_QUERY);
            if (!range.is()) continue;
            officev1::StreamPagesResponse event;
            officev1::SheetNamedRange* out = event.mutable_sheet_named_range();
            out->set_name(utf8(range->getName()));
            out->set_content(utf8(range->getContent()));
            out->set_type_flags(range->getType());
            if (!emit_fn(event)) return false;
          }
        }
      }
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: NamedRanges is a service property that a
      // minimal model may not carry.
    } catch (const css::uno::Exception& error) {
      warner.warn("named ranges query failed", error);
    }
  }

  Reference<css::container::XIndexAccess> sheets(calc_doc->getSheets(),
                                                 UNO_QUERY);
  if (!sheets.is()) {
    warner.warn("spreadsheet sheets are not index accessible");
    return true;
  }
  Reference<css::graphic::XGraphicProvider> provider;
  if (want_images) provider = graphic_provider(context, warner);
  PartSelection images_only;
  images_only.all = false;
  if (want_images) {
    images_only.mask = 1u << officev1::DOCUMENT_PART_IMAGES;
  }
  int32_t image_counter = 0;

  for (sal_Int32 s = 0; s < sheets->getCount(); s++) {
    std::string label = "sheet " + std::to_string(s);
    Reference<css::sheet::XSpreadsheet> sheet(sheets->getByIndex(s), UNO_QUERY);
    if (!sheet.is()) {
      warner.warn(label + " is not a spreadsheet object");
      continue;
    }

    if (want_sheets) {
      css::table::CellRangeAddress used;
      used.Sheet = static_cast<sal_Int16>(s);
      used.StartRow = 0;
      used.StartColumn = 0;
      used.EndRow = 0;
      used.EndColumn = 0;

      officev1::StreamPagesResponse event;
      officev1::Sheet* out = event.mutable_sheet();
      out->set_index(static_cast<int32_t>(s));
      out->set_tab_color_rgb(-1);
      Reference<css::container::XNamed> named(sheet, UNO_QUERY);
      if (named.is()) out->set_name(utf8(named->getName()));
      Reference<css::beans::XPropertySet> props(sheet, UNO_QUERY);
      if (props.is()) {
        try {
          sal_Bool visible = true;
          props->getPropertyValue("IsVisible") >>= visible;
          out->set_visible(visible);
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " visibility query failed", error);
        }
        try {
          sal_Int32 color = -1;
          props->getPropertyValue("TabColor") >>= color;
          out->set_tab_color_rgb(color);
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: TabColor is an optional sheet property.
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " tab color query failed", error);
        }
      }
      // The used-area cursor bounds the walk: collapse to the start, expand
      // to the end, and read the exact rectangle.
      try {
        Reference<css::sheet::XSheetCellCursor> cursor = sheet->createCursor();
        Reference<css::sheet::XUsedAreaCursor> area(cursor, UNO_QUERY);
        Reference<css::sheet::XCellRangeAddressable> addressable(cursor,
                                                                 UNO_QUERY);
        if (area.is() && addressable.is()) {
          area->gotoStartOfUsedArea(false);
          area->gotoEndOfUsedArea(true);
          used = addressable->getRangeAddress();
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " used area query failed", error);
      }
      out->set_used_start_row(used.StartRow);
      out->set_used_start_column(used.StartColumn);
      out->set_used_end_row(used.EndRow);
      out->set_used_end_column(used.EndColumn);
      try {
        Reference<css::sheet::XPrintAreas> print(sheet, UNO_QUERY);
        if (print.is()) {
          for (const css::table::CellRangeAddress& range :
               print->getPrintAreas()) {
            fill_range_ref(range, out->add_print_areas());
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " print areas query failed", error);
      }
      if (!emit_fn(event)) return false;

      for (sal_Int32 r = used.StartRow; r <= used.EndRow; r++) {
        officev1::StreamPagesResponse row_event;
        officev1::SheetRow* row = row_event.mutable_sheet_row();
        row->set_sheet_index(static_cast<int32_t>(s));
        row->set_row(r);
        for (sal_Int32 c = used.StartColumn; c <= used.EndColumn; c++) {
          std::string cell_label = label + " cell r" + std::to_string(r) +
                                   " c" + std::to_string(c);
          try {
            Reference<css::table::XCell> cell = sheet->getCellByPosition(c, r);
            if (!cell.is()) continue;
            css::table::CellContentType type = cell->getType();
            if (type == css::table::CellContentType_EMPTY) continue;
            officev1::SheetCell* out_cell = row->add_cells();
            out_cell->set_column(c);
            switch (type) {
              case css::table::CellContentType_VALUE:
                out_cell->set_type(officev1::SHEET_CELL_TYPE_VALUE);
                break;
              case css::table::CellContentType_TEXT:
                out_cell->set_type(officev1::SHEET_CELL_TYPE_TEXT);
                break;
              case css::table::CellContentType_FORMULA:
                out_cell->set_type(officev1::SHEET_CELL_TYPE_FORMULA);
                break;
              default:
                out_cell->set_type(officev1::SHEET_CELL_TYPE_EMPTY);
                break;
            }
            if (type == css::table::CellContentType_FORMULA) {
              out_cell->set_formula(utf8(cell->getFormula()));
            }
            if (type != css::table::CellContentType_TEXT) {
              out_cell->set_number(cell->getValue());
            }
            Reference<css::text::XText> text(cell, UNO_QUERY);
            if (text.is()) out_cell->set_display(utf8(text->getString()));
            sal_Int32 key = 0;
            Reference<css::beans::XPropertySet> cell_props(cell, UNO_QUERY);
            if (cell_props.is()) {
              cell_props->getPropertyValue("NumberFormat") >>= key;
            }
            out_cell->set_number_format(key);
            out_cell->set_number_format_string(format_code(key));
            out_cell->set_merged_columns(1);
            out_cell->set_merged_rows(1);
            // The merge cursor is built only when the cheap gate says the
            // cell is inside a merged region.
            Reference<css::util::XMergeable> mergeable(cell, UNO_QUERY);
            if (mergeable.is() && mergeable->getIsMerged()) {
              Reference<css::sheet::XSheetCellRange> cell_range(cell,
                                                                UNO_QUERY);
              if (cell_range.is()) {
                Reference<css::sheet::XSheetCellCursor> merge_cursor =
                    sheet->createCursorByRange(cell_range);
                Reference<css::sheet::XCellRangeAddressable> merge_address(
                    merge_cursor, UNO_QUERY);
                if (merge_cursor.is() && merge_address.is()) {
                  merge_cursor->collapseToMergedArea();
                  css::table::CellRangeAddress span =
                      merge_address->getRangeAddress();
                  if (span.StartRow == r && span.StartColumn == c) {
                    out_cell->set_merged_columns(span.EndColumn -
                                                 span.StartColumn + 1);
                    out_cell->set_merged_rows(span.EndRow - span.StartRow + 1);
                  }
                }
              }
            }
          } catch (const css::uno::Exception& error) {
            warner.warn(cell_label + " extraction failed", error);
          }
        }
        if (row->cells_size() > 0) {
          if (!emit_fn(row_event)) return false;
        }
      }

      try {
        Reference<css::sheet::XSheetAnnotationsSupplier> supplier(sheet,
                                                                  UNO_QUERY);
        if (supplier.is()) {
          Reference<css::container::XIndexAccess> annotations(
              supplier->getAnnotations(), UNO_QUERY);
          if (annotations.is()) {
            for (sal_Int32 i = 0; i < annotations->getCount(); i++) {
              Reference<css::sheet::XSheetAnnotation> annotation(
                  annotations->getByIndex(i), UNO_QUERY);
              if (!annotation.is()) continue;
              officev1::StreamPagesResponse comment_event;
              officev1::SheetCellComment* comment =
                  comment_event.mutable_sheet_cell_comment();
              css::table::CellAddress position = annotation->getPosition();
              comment->set_sheet_index(static_cast<int32_t>(s));
              comment->set_row(position.Row);
              comment->set_column(position.Column);
              comment->set_author(utf8(annotation->getAuthor()));
              comment->set_date(utf8(annotation->getDate()));
              comment->set_visible(annotation->getIsVisible());
              // Annotation text is guaranteed through XSimpleText, not
              // XText.
              Reference<css::text::XSimpleText> text(annotation, UNO_QUERY);
              if (text.is()) comment->set_text(utf8(text->getString()));
              if (!emit_fn(comment_event)) return false;
            }
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " annotations query failed", error);
      }

      try {
        Reference<css::table::XTableChartsSupplier> supplier(sheet, UNO_QUERY);
        if (supplier.is()) {
          Reference<css::container::XNameAccess> charts(supplier->getCharts(),
                                                        UNO_QUERY);
          if (charts.is()) {
            for (const rtl::OUString& name : charts->getElementNames()) {
              Reference<css::table::XTableChart> chart(charts->getByName(name),
                                                       UNO_QUERY);
              if (!chart.is()) continue;
              officev1::StreamPagesResponse chart_event;
              officev1::SheetChart* out_chart =
                  chart_event.mutable_sheet_chart();
              out_chart->set_sheet_index(static_cast<int32_t>(s));
              out_chart->set_name(utf8(name));
              for (const css::table::CellRangeAddress& range :
                   chart->getRanges()) {
                fill_range_ref(range, out_chart->add_ranges());
              }
              out_chart->set_has_column_headers(chart->getHasColumnHeaders());
              out_chart->set_has_row_headers(chart->getHasRowHeaders());
              if (!emit_fn(chart_event)) return false;
            }
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " charts query failed", error);
      }

      try {
        Reference<css::sheet::XDataPilotTablesSupplier> supplier(sheet,
                                                                 UNO_QUERY);
        if (supplier.is()) {
          Reference<css::container::XNameAccess> pivots(
              supplier->getDataPilotTables(), UNO_QUERY);
          if (pivots.is()) {
            auto add_fields =
                [&](const Reference<css::container::XIndexAccess>& fields,
                    google::protobuf::RepeatedPtrField<std::string>* out_fields) {
                  if (!fields.is()) return;
                  for (sal_Int32 i = 0; i < fields->getCount(); i++) {
                    Reference<css::container::XNamed> field(
                        fields->getByIndex(i), UNO_QUERY);
                    if (field.is()) *out_fields->Add() = utf8(field->getName());
                  }
                };
            for (const rtl::OUString& name : pivots->getElementNames()) {
              css::uno::Any entry = pivots->getByName(name);
              Reference<css::sheet::XDataPilotDescriptor> descriptor(entry,
                                                                     UNO_QUERY);
              Reference<css::sheet::XDataPilotTable> table(entry, UNO_QUERY);
              officev1::StreamPagesResponse pivot_event;
              officev1::SheetPivotTable* pivot =
                  pivot_event.mutable_sheet_pivot_table();
              pivot->set_sheet_index(static_cast<int32_t>(s));
              pivot->set_name(utf8(name));
              if (descriptor.is()) {
                fill_range_ref(descriptor->getSourceRange(),
                               pivot->mutable_source_range());
                add_fields(descriptor->getRowFields(),
                           pivot->mutable_row_fields());
                add_fields(descriptor->getColumnFields(),
                           pivot->mutable_column_fields());
                add_fields(descriptor->getDataFields(),
                           pivot->mutable_data_fields());
                add_fields(descriptor->getPageFields(),
                           pivot->mutable_page_fields());
              }
              if (table.is()) {
                fill_range_ref(table->getOutputRange(),
                               pivot->mutable_output_range());
              }
              if (!emit_fn(pivot_event)) return false;
            }
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " pivot tables query failed", error);
      }
    }

    if (want_images) {
      // The sheet draw page reuses the drawing walker in images-only mode:
      // EmbeddedImage events keyed to the sheet index, no DrawingShape
      // events, groups still recursed.
      try {
        Reference<css::drawing::XDrawPageSupplier> supplier(sheet, UNO_QUERY);
        if (supplier.is()) {
          Reference<css::drawing::XShapes> page(supplier->getDrawPage(),
                                                UNO_QUERY);
          if (page.is()) {
            if (!emit_shapes(page, static_cast<int32_t>(s), "", provider,
                             &image_counter, images_only, emit_fn, warner)) {
              return false;
            }
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " draw page walk failed", error);
      }
    }
  }
  return true;
}

bool emit_notes(const Reference<css::text::XTextDocument>& text_doc,
                const Reference<css::text::XTextViewCursor>& cursor,
                bool endnotes, const EmitFn& emit_fn, Warner& warner) {
  Reference<css::container::XIndexAccess> notes;
  if (endnotes) {
    Reference<css::text::XEndnotesSupplier> supplier(text_doc, UNO_QUERY);
    if (supplier.is()) notes = supplier->getEndnotes();
  } else {
    Reference<css::text::XFootnotesSupplier> supplier(text_doc, UNO_QUERY);
    if (supplier.is()) notes = supplier->getFootnotes();
  }
  if (!notes.is()) return true;
  const char* kind = endnotes ? "endnote" : "footnote";
  for (sal_Int32 i = 0; i < notes->getCount(); i++) {
    std::string label = std::string(kind) + " " + std::to_string(i);
    officev1::StreamPagesResponse event;
    officev1::Footnote* out = event.mutable_footnote();
    out->set_index(i);
    out->set_endnote(endnotes);
    out->set_page_index(-1);
    try {
      Reference<css::text::XFootnote> note(notes->getByIndex(i), UNO_QUERY);
      if (!note.is()) {
        warner.warn(label + " is not a footnote object");
        continue;
      }
      Reference<css::text::XTextRange> anchor = note->getAnchor();
      // getLabel is only the custom label; auto numbered notes render their
      // number through the anchor text.
      rtl::OUString note_label = note->getLabel();
      if (note_label.isEmpty() && anchor.is()) note_label = anchor->getString();
      out->set_label(utf8(note_label));
      int32_t page_index = -1;
      caret_at(cursor, anchor, label + " anchor", out->mutable_anchor(),
               &page_index, warner);
      out->set_page_index(page_index);
      flatten_text_runs(Reference<css::text::XText>(note, UNO_QUERY), label,
                        out->mutable_runs(), warner);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " extraction failed", error);
    }
    if (!emit_fn(event)) return false;
  }
  return true;
}

bool emit_document_indexes(const Reference<css::text::XTextDocument>& text_doc,
                           const Reference<css::text::XTextViewCursor>& cursor,
                           const EmitFn& emit_fn, Warner& warner) {
  Reference<css::text::XDocumentIndexesSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::container::XIndexAccess> indexes(supplier->getDocumentIndexes(),
                                                  UNO_QUERY);
  if (!indexes.is()) return true;
  for (sal_Int32 i = 0; i < indexes->getCount(); i++) {
    std::string label = "document index " + std::to_string(i);
    officev1::StreamPagesResponse event;
    officev1::DocumentIndex* out = event.mutable_document_index();
    out->set_index(i);
    out->set_page_index(-1);
    try {
      Reference<css::text::XDocumentIndex> doc_index(indexes->getByIndex(i),
                                                     UNO_QUERY);
      if (!doc_index.is()) {
        warner.warn(label + " is not an index object");
        continue;
      }
      out->set_type(utf8(doc_index->getServiceName()));
      Reference<css::beans::XPropertySet> props(doc_index, UNO_QUERY);
      if (props.is()) {
        rtl::OUString title;
        props->getPropertyValue("Title") >>= title;
        out->set_title(utf8(title));
      }
      Reference<css::text::XTextRange> anchor = doc_index->getAnchor();
      int32_t page_index = -1;
      caret_at(cursor, anchor, label + " anchor", out->mutable_anchor(),
               &page_index, warner);
      out->set_page_index(page_index);
      // The generated text: enumerate paragraphs over a cursor spanning the
      // index's anchor range.
      if (anchor.is()) {
        Reference<css::text::XText> text = anchor->getText();
        if (text.is()) {
          Reference<css::container::XEnumerationAccess> span(
              text->createTextCursorByRange(anchor), UNO_QUERY);
          if (span.is()) {
            Reference<css::container::XEnumeration> paragraphs =
                span->createEnumeration();
            while (paragraphs->hasMoreElements()) {
              Reference<css::container::XEnumerationAccess> paragraph(
                  paragraphs->nextElement(), UNO_QUERY);
              if (paragraph.is()) {
                fill_runs(paragraph, label, out->mutable_runs(), nullptr, warner);
              }
            }
          }
        }
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " extraction failed", error);
    }
    if (!emit_fn(event)) return false;
  }
  return true;
}

bool emit_page_styles(const Reference<css::frame::XModel>& model,
                      const PartSelection& parts, const EmitFn& emit_fn,
                      Warner& warner) {
  Reference<css::style::XStyleFamiliesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::container::XNameAccess> families = supplier->getStyleFamilies();
  if (!families.is() || !families->hasByName("PageStyles")) return true;
  Reference<css::container::XIndexAccess> styles;
  try {
    families->getByName("PageStyles") >>= styles;
  } catch (const css::uno::Exception& error) {
    warner.warn("page styles are not reachable", error);
    return true;
  }
  if (!styles.is()) return true;
  for (sal_Int32 i = 0; i < styles->getCount(); i++) {
    Reference<css::style::XStyle> style;
    Reference<css::beans::XPropertySet> props;
    try {
      styles->getByIndex(i) >>= style;
      props = Reference<css::beans::XPropertySet>(style, UNO_QUERY);
    } catch (const css::uno::Exception& error) {
      warner.warn("page style " + std::to_string(i) + " is not reachable", error);
      continue;
    }
    if (!style.is() || !props.is() || !style->isInUse()) continue;
    std::string name = utf8(style->getName());
    std::string label = "page style " + name;

    if (parts.wants(officev1::DOCUMENT_PART_PAGE_STYLES)) {
      officev1::StreamPagesResponse geometry_event;
      officev1::PageStyleInfo* out = geometry_event.mutable_page_style();
      out->set_name(name);
      out->set_columns(1);
      try {
        sal_Int32 width = 0, height = 0, left = 0, right = 0, top = 0, bottom = 0;
        props->getPropertyValue("Width") >>= width;
        props->getPropertyValue("Height") >>= height;
        props->getPropertyValue("LeftMargin") >>= left;
        props->getPropertyValue("RightMargin") >>= right;
        props->getPropertyValue("TopMargin") >>= top;
        props->getPropertyValue("BottomMargin") >>= bottom;
        out->set_width_twips(hundredth_mm_to_twips(width));
        out->set_height_twips(hundredth_mm_to_twips(height));
        out->set_margin_left_twips(hundredth_mm_to_twips(left));
        out->set_margin_right_twips(hundredth_mm_to_twips(right));
        out->set_margin_top_twips(hundredth_mm_to_twips(top));
        out->set_margin_bottom_twips(hundredth_mm_to_twips(bottom));
        Reference<css::text::XTextColumns> columns;
        props->getPropertyValue("TextColumns") >>= columns;
        if (columns.is()) {
          out->set_columns(std::max<sal_Int16>(1, columns->getColumnCount()));
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " geometry query failed", error);
      }
      if (!emit_fn(geometry_event)) return false;
    }

    if (!parts.wants(officev1::DOCUMENT_PART_HEADERS_FOOTERS)) continue;
    for (bool footer : {false, true}) {
      try {
        sal_Bool enabled = false;
        props->getPropertyValue(footer ? rtl::OUString("FooterIsOn")
                                       : rtl::OUString("HeaderIsOn")) >>= enabled;
        if (!enabled) continue;
        Reference<css::text::XText> text;
        props->getPropertyValue(footer ? rtl::OUString("FooterText")
                                       : rtl::OUString("HeaderText")) >>= text;
        if (!text.is()) continue;
        officev1::StreamPagesResponse event;
        officev1::HeaderFooter* header_footer = event.mutable_header_footer();
        header_footer->set_page_style(name);
        header_footer->set_footer(footer);
        Reference<css::container::XEnumerationAccess> access(text, UNO_QUERY);
        if (access.is()) {
          Reference<css::container::XEnumeration> paragraphs =
              access->createEnumeration();
          int32_t index = 0;
          Reference<css::text::XTextViewCursor> no_cursor;
          while (paragraphs->hasMoreElements()) {
            officev1::Paragraph paragraph;
            if (fill_paragraph(paragraphs->nextElement(), index, no_cursor,
                               nullptr, &paragraph, warner)) {
              *header_footer->add_paragraphs() = paragraph;
              index++;
            }
          }
        }
        if (!emit_fn(event)) return false;
      } catch (const css::uno::Exception& error) {
        warner.warn(label + (footer ? " footer" : " header") + " query failed",
                    error);
      }
    }
  }
  return true;
}

bool emit_text_content(const Reference<css::text::XTextDocument>& text_doc,
                       const Reference<css::uno::XComponentContext>& context,
                       const PartSelection& parts, const EmitFn& emit_fn,
                       Warner& warner) {
  Reference<css::frame::XModel> model(text_doc, UNO_QUERY);
  Reference<css::text::XTextViewCursor> cursor;
  if (model.is()) {
    Reference<css::text::XTextViewCursorSupplier> supplier(
        model->getCurrentController(), UNO_QUERY);
    if (supplier.is()) cursor = supplier->getViewCursor();
  }
  if (!cursor.is()) {
    warner.warn("no view cursor, layout positions will be missing");
  }

  bool want_paragraphs = parts.wants(officev1::DOCUMENT_PART_PARAGRAPHS);
  bool want_tables = parts.wants(officev1::DOCUMENT_PART_TABLES);
  if (want_paragraphs || want_tables) {
    Reference<css::container::XEnumerationAccess> body(text_doc->getText(),
                                                       UNO_QUERY);
    if (!body.is()) {
      warner.warn("document body is not enumerable");
      return true;
    }
    Reference<css::container::XEnumeration> elements = body->createEnumeration();
    int32_t paragraph_index = 0;
    int32_t table_index = 0;
    int64_t annotation_offset = 0;
    while (elements->hasMoreElements()) {
      css::uno::Any element = elements->nextElement();
      Reference<css::text::XTextTable> table(element, UNO_QUERY);
      if (table.is()) {
        if (want_tables) {
          if (!emit_table(table, table_index, cursor, emit_fn, warner)) {
            return false;
          }
        }
        table_index++;
        continue;
      }
      if (!want_paragraphs) continue;
      officev1::StreamPagesResponse event;
      if (!fill_paragraph(element, paragraph_index, cursor, &annotation_offset,
                          event.mutable_paragraph(), warner)) {
        continue;
      }
      if (!emit_fn(event)) return false;
      paragraph_index++;
    }
  }
  if (parts.wants(officev1::DOCUMENT_PART_FOOTNOTES)) {
    if (!emit_notes(text_doc, cursor, false, emit_fn, warner)) return false;
    if (!emit_notes(text_doc, cursor, true, emit_fn, warner)) return false;
  }
  if (parts.wants(officev1::DOCUMENT_PART_INDEXES)) {
    if (!emit_document_indexes(text_doc, cursor, emit_fn, warner)) return false;
  }
  if (parts.wants(officev1::DOCUMENT_PART_PAGE_STYLES) ||
      parts.wants(officev1::DOCUMENT_PART_HEADERS_FOOTERS)) {
    if (!emit_page_styles(model, parts, emit_fn, warner)) return false;
  }
  if (parts.wants(officev1::DOCUMENT_PART_TEXT_FRAMES)) {
    if (!emit_text_frames(text_doc, cursor, emit_fn, warner)) return false;
  }
  if (parts.wants(officev1::DOCUMENT_PART_IMAGES) ||
      parts.wants(officev1::DOCUMENT_PART_SHAPES)) {
    return emit_draw_shapes(text_doc, context, cursor, parts, emit_fn, warner);
  }
  return true;
}

}  // namespace

bool emit_typed_content(const PartSelection& parts, const EmitFn& emit_fn,
                        std::vector<std::string>* warnings) {
  Warner warner(warnings);
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) {
      warner.warn("no in-process UNO context, typed content unavailable");
      return true;
    }
    Reference<css::frame::XDesktop2> desktop = css::frame::Desktop::create(context);
    Reference<css::container::XEnumerationAccess> components(
        desktop->getComponents(), UNO_QUERY);
    if (!components.is()) {
      warner.warn("desktop has no component list, typed content unavailable");
      return true;
    }
    Reference<css::container::XEnumeration> it = components->createEnumeration();
    Reference<css::frame::XModel> model;
    while (it->hasMoreElements()) {
      Reference<css::frame::XModel> candidate(it->nextElement(), UNO_QUERY);
      if (candidate.is()) model = candidate;
    }
    if (!model.is()) {
      warner.warn("loaded document not found on the desktop, typed content unavailable");
      return true;
    }
    if (parts.wants(officev1::DOCUMENT_PART_METADATA)) {
      if (!emit_metadata(model, emit_fn, warner)) return false;
    }
    Reference<css::text::XTextDocument> text_doc(model, UNO_QUERY);
    if (text_doc.is()) {
      // Skip the whole text walk when no text-document part is selected.
      bool wants_text_part =
          parts.wants(officev1::DOCUMENT_PART_PARAGRAPHS) ||
          parts.wants(officev1::DOCUMENT_PART_TABLES) ||
          parts.wants(officev1::DOCUMENT_PART_IMAGES) ||
          parts.wants(officev1::DOCUMENT_PART_FOOTNOTES) ||
          parts.wants(officev1::DOCUMENT_PART_HEADERS_FOOTERS) ||
          parts.wants(officev1::DOCUMENT_PART_PAGE_STYLES) ||
          parts.wants(officev1::DOCUMENT_PART_INDEXES) ||
          parts.wants(officev1::DOCUMENT_PART_TEXT_FRAMES) ||
          parts.wants(officev1::DOCUMENT_PART_SHAPES);
      if (!wants_text_part) return true;
      return emit_text_content(text_doc, context, parts, emit_fn, warner);
    }
    Reference<css::sheet::XSpreadsheetDocument> calc_doc(model, UNO_QUERY);
    if (calc_doc.is()) {
      if (!parts.wants(officev1::DOCUMENT_PART_SHEETS) &&
          !parts.wants(officev1::DOCUMENT_PART_IMAGES)) {
        return true;
      }
      return emit_calc_content(calc_doc, model, context, parts, emit_fn,
                               warner);
    }
    Reference<css::lang::XServiceInfo> info(model, UNO_QUERY);
    // Impress models also supply draw pages, so the presentation branch is
    // decided by the PresentationDocument service, which the office core
    // reports only for Impress.
    if (info.is() && info->supportsService(
                         "com.sun.star.presentation.PresentationDocument")) {
      if (!parts.wants(officev1::DOCUMENT_PART_SLIDES)) return true;
      Reference<css::drawing::XDrawPagesSupplier> slides(model, UNO_QUERY);
      if (slides.is()) {
        return emit_presentation_content(slides, emit_fn, warner);
      }
      return true;
    }
    // Draw and Calc models both supply draw pages too, so gate on the
    // DrawingDocument service, which the office core reports only for Draw.
    if (!parts.wants(officev1::DOCUMENT_PART_SHAPES) &&
        !parts.wants(officev1::DOCUMENT_PART_IMAGES)) {
      return true;
    }
    if (info.is() &&
        info->supportsService("com.sun.star.drawing.DrawingDocument")) {
      Reference<css::drawing::XDrawPagesSupplier> draw(model, UNO_QUERY);
      if (draw.is()) {
        return emit_drawing_content(draw, context, parts, emit_fn, warner);
      }
    }
    return true;
  } catch (const css::uno::Exception& error) {
    warner.warn("extraction aborted", error);
    return true;
  }
}

}  // namespace grlibre
