#include "uno_extract.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <print>
#include <ctime>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <com/sun/star/awt/FontPitch.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/UnknownPropertyException.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/chart/XComplexDescriptionAccess.hpp>
#include <com/sun/star/chart2/AxisType.hpp>
#include <com/sun/star/chart2/ScaleData.hpp>
#include <com/sun/star/chart2/XAxis.hpp>
#include <com/sun/star/chart2/XChartDocument.hpp>
#include <com/sun/star/chart2/XChartType.hpp>
#include <com/sun/star/chart2/XChartTypeContainer.hpp>
#include <com/sun/star/chart2/XCoordinateSystem.hpp>
#include <com/sun/star/chart2/XCoordinateSystemContainer.hpp>
#include <com/sun/star/chart2/XDataSeries.hpp>
#include <com/sun/star/chart2/XDataSeriesContainer.hpp>
#include <com/sun/star/chart2/XDataPointCustomLabelField.hpp>
#include <com/sun/star/chart2/XDiagram.hpp>
#include <com/sun/star/chart2/XFormattedString.hpp>
#include <com/sun/star/chart2/XRegressionCurve.hpp>
#include <com/sun/star/chart2/XRegressionCurveContainer.hpp>
#include <com/sun/star/chart2/XTitle.hpp>
#include <com/sun/star/chart2/XTitled.hpp>
#include <com/sun/star/chart2/data/XDataSequence.hpp>
#include <com/sun/star/chart2/data/XDataSource.hpp>
#include <com/sun/star/chart2/data/XLabeledDataSequence.hpp>
#include <com/sun/star/chart2/data/XNumericalDataSequence.hpp>
#include <com/sun/star/chart2/data/XTextualDataSequence.hpp>
#include <com/sun/star/awt/XControlModel.hpp>
#include <com/sun/star/document/XEmbeddedObjectSupplier2.hpp>
#include <com/sun/star/document/XEmbeddedObjectSupplier.hpp>
#include <com/sun/star/document/XRedlinesSupplier.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/drawing/XControlShape.hpp>
#include <com/sun/star/office/XAnnotation.hpp>
#include <com/sun/star/office/XAnnotationAccess.hpp>
#include <com/sun/star/office/XAnnotationEnumeration.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPageSupplier.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XMasterPageTarget.hpp>
#include <com/sun/star/drawing/XMasterPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/embed/StorageFormats.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XDispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/packages/zip/ZipIOException.hpp>
#include <com/sun/star/presentation/XPresentationPage.hpp>
#include <com/sun/star/style/CaseMap.hpp>
#include <com/sun/star/style/XStyle.hpp>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/form/XFormsSupplier2.hpp>
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
#include <com/sun/star/sheet/CellFlags.hpp>
#include <com/sun/star/sheet/XCellRangesQuery.hpp>
#include <com/sun/star/sheet/XCellRangeReferrer.hpp>
#include <com/sun/star/sheet/XDataPilotDescriptor.hpp>
#include <com/sun/star/sheet/XDataPilotTable.hpp>
#include <com/sun/star/sheet/XDataPilotTables.hpp>
#include <com/sun/star/sheet/XDataPilotTablesSupplier.hpp>
#include <com/sun/star/sheet/XDatabaseRange.hpp>
#include <com/sun/star/sheet/XNamedRange.hpp>
#include <com/sun/star/sheet/XPrintAreas.hpp>
#include <com/sun/star/sheet/XSheetAnnotation.hpp>
#include <com/sun/star/sheet/XSheetAnnotations.hpp>
#include <com/sun/star/sheet/XSheetAnnotationsSupplier.hpp>
#include <com/sun/star/sheet/XSheetCellCursor.hpp>
#include <com/sun/star/sheet/XSheetCellRange.hpp>
#include <com/sun/star/sheet/XSheetCellRanges.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/sheet/XUsedAreaCursor.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/table/XColumnRowRange.hpp>
#include <com/sun/star/table/XTableChart.hpp>
#include <com/sun/star/text/TextContentAnchorType.hpp>
#include <com/sun/star/table/XTableCharts.hpp>
#include <com/sun/star/table/XTableChartsSupplier.hpp>
#include <com/sun/star/table/XMergeableCell.hpp>
#include <com/sun/star/table/XTable.hpp>
#include <com/sun/star/table/XTableColumns.hpp>
#include <com/sun/star/table/XTableRows.hpp>
#include <com/sun/star/text/TableColumnSeparator.hpp>
#include <com/sun/star/util/NumberFormat.hpp>
#include <com/sun/star/text/XSimpleText.hpp>
#include <com/sun/star/util/XMergeable.hpp>
#include <com/sun/star/util/XNumberFormats.hpp>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/text/XBookmarksSupplier.hpp>
#include <com/sun/star/text/XDocumentIndex.hpp>
#include <com/sun/star/text/XDocumentIndexesSupplier.hpp>
#include <com/sun/star/text/XEndnotesSupplier.hpp>
#include <com/sun/star/text/XFootnote.hpp>
#include <com/sun/star/text/XFootnotesSupplier.hpp>
#include <com/sun/star/text/XFormField.hpp>
#include <com/sun/star/text/FilenameDisplayFormat.hpp>
#include <com/sun/star/text/XTextField.hpp>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>
#include <com/sun/star/text/XPageCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextColumns.hpp>
#include <com/sun/star/text/XTextEmbeddedObjectsSupplier.hpp>
#include <com/sun/star/text/XTextFrame.hpp>
#include <com/sun/star/text/XTextFramesSupplier.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextSectionsSupplier.hpp>
#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/util/Date.hpp>
#include <com/sun/star/util/DateTime.hpp>
#include <com/sun/star/view/XLineCursor.hpp>
#include <com/sun/star/view/XViewCursor.hpp>
#include <cppuhelper/implbase1.hxx>
#include <cppuhelper/implbase4.hxx>
#include <rtl/ref.hxx>
#include <rtl/ustrbuf.hxx>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "gif_palette.h"
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

// Draw-page groups nest recursively, and every walk over them recurses
// with them; a crafted document can nest groups deep enough to exhaust the
// worker's stack. Groups past this depth are reported and not descended.
constexpr int kMaxShapeGroupDepth = 64;

std::string utf8(const rtl::OUString& text) {
  rtl::OString bytes = rtl::OUStringToOString(text, RTL_TEXTENCODING_UTF8);
  return std::string(bytes.getStr(), static_cast<size_t>(bytes.getLength()));
}

rtl::OUString oustring(const std::string& text) {
  return rtl::OUString(text.data(), static_cast<sal_Int32>(text.size()),
                       RTL_TEXTENCODING_UTF8);
}

// The name a mark is reported under. The office core names the fieldmarks
// it creates on import __Fieldmark__<n>_<random>, the random part drawn
// afresh on every load; <n> already numbers them uniquely in the document,
// so the random tail is dropped and two parses of the same bytes agree.
// Every other name is the document's own and passes through.
std::string stable_mark_name(const std::string& name) {
  static constexpr std::string_view kPrefix = "__Fieldmark__";
  if (!name.starts_with(kPrefix)) return name;
  size_t at = kPrefix.size();
  const size_t digits = at;
  while (at < name.size() && name[at] >= '0' && name[at] <= '9') at++;
  if (at == digits || at + 1 >= name.size() || name[at] != '_') return name;
  for (size_t i = at + 1; i < name.size(); i++) {
    if (name[i] < '0' || name[i] > '9') return name;
  }
  return name.substr(0, at);
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
    count_++;
    report(message);
  }

  // Problems reported so far. A walk that must read everything (the
  // redaction checks) compares it before and after instead of reading the
  // messages.
  size_t count() const { return count_; }

  // Reports a problem that loses no text (an image that will not
  // re-encode), so count() does not include it.
  void note(const std::string& context, const css::uno::Exception& error) {
    report(context + ": " + utf8(error.Message));
  }

  // Warns about a condition once per walk, however often it recurs.
  void warn_once(const std::string& message) {
    if (warned_.insert(message).second) warn(message);
  }

 private:
  void report(const std::string& message) {
    std::println(stderr, "grlibre-worker: typed content: {}", message);
    if (sink_ != nullptr) sink_->push_back("typed content: " + message);
  }

  std::vector<std::string>* sink_;
  std::set<std::string> warned_;
  size_t count_ = 0;
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

// The document LibreOfficeKit loaded, found on the desktop's component
// list. Embedded objects put their inner models on the desktop too; the
// loaded document is the component whose URL is the loaded file, with the
// last component as fallback.
Reference<css::frame::XModel> find_loaded_model(
    const Reference<css::uno::XComponentContext>& context) {
  Reference<css::frame::XDesktop2> desktop = css::frame::Desktop::create(context);
  Reference<css::container::XEnumerationAccess> components(
      desktop->getComponents(), UNO_QUERY);
  if (!components.is()) return {};
  Reference<css::container::XEnumeration> it = components->createEnumeration();
  Reference<css::frame::XModel> model;
  Reference<css::frame::XModel> fallback;
  while (it->hasMoreElements()) {
    Reference<css::frame::XModel> candidate(it->nextElement(), UNO_QUERY);
    if (!candidate.is()) continue;
    fallback = candidate;
    if (candidate->getURL().startsWith("file://")) model = candidate;
  }
  return model.is() ? model : fallback;
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

// Receives the pdf export filter's output and frames it onward, so the PDF
// never exists as one whole buffer here. The filter delivers everything in
// one burst of bounded writes after the render (its internal temp file is
// copied out in 32 KiB reads), sometimes led by a zero-length write; this
// sink aggregates up to chunk_limit per emitted frame and treats
// closeOutput as the completion signal. A declined emit throws, which
// aborts the store loudly inside the filter.
class PdfChunkSink : public cppu::WeakImplHelper1<css::io::XOutputStream> {
 public:
  PdfChunkSink(size_t chunk_limit,
               const std::function<bool(std::string&&)>& emit_chunk)
      : chunk_limit_(chunk_limit), emit_chunk_(emit_chunk) {}

  void SAL_CALL writeBytes(const css::uno::Sequence<sal_Int8>& data) override {
    buffer_.append(reinterpret_cast<const char*>(data.getConstArray()),
                   static_cast<size_t>(data.getLength()));
    total_ += data.getLength();
    while (buffer_.size() >= chunk_limit_) emit_front(chunk_limit_);
  }
  void SAL_CALL flush() override {}
  void SAL_CALL closeOutput() override {
    closed_ = true;
    if (!buffer_.empty()) emit_front(buffer_.size());
  }

  long total() const { return total_; }
  bool closed() const { return closed_; }

 private:
  void emit_front(size_t count) {
    std::string chunk = buffer_.substr(0, count);
    buffer_.erase(0, count);
    if (!emit_chunk_(std::move(chunk))) {
      throw css::io::IOException("PDF chunk emission failed (parent gone?)");
    }
  }

  size_t chunk_limit_;
  std::function<bool(std::string&&)> emit_chunk_;
  std::string buffer_;
  long total_ = 0;
  bool closed_ = false;
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

// Milliseconds from the Unix epoch to midnight of a calendar date, negative
// for dates before 1970. A spreadsheet's null date is 1899-12-30 by default,
// which is exactly the range datetime_epoch_ms clamps away, so serial dates
// resolve through this helper instead.
// Splits a day count expressed in milliseconds from the Unix epoch into its
// wall-clock components. The sum a spreadsheet serial produces is a civil
// value, not an instant, so it is decomposed rather than reported as one.
void fill_cell_datetime(int64_t day_ms, officev1::CellDateTime* out) {
  time_t seconds = static_cast<time_t>(day_ms / 1000);
  struct tm parts = {};
  if (gmtime_r(&seconds, &parts) == nullptr) return;
  out->set_year(parts.tm_year + 1900);
  out->set_month(parts.tm_mon + 1);
  out->set_day(parts.tm_mday);
  out->set_hour(parts.tm_hour);
  out->set_minute(parts.tm_min);
  out->set_second(parts.tm_sec);
}

int64_t date_midnight_epoch_ms(const css::util::Date& value) {
  if (value.Year == 0) return 0;
  struct tm parts = {};
  parts.tm_year = value.Year - 1900;
  parts.tm_mon = value.Month - 1;
  parts.tm_mday = value.Day;
  return static_cast<int64_t>(timegm(&parts)) * 1000;
}

// The user-defined property the office core files an OOXML document's
// cp:category under, and writes back out as cp:category on export.
constexpr std::string_view kCategoryProperty = "OOXMLCorePropertyCategory";

// Copies one user-defined property value into its typed slot, keeping the
// type the document stored.
void set_user_property_value(const rtl::OUString& name,
                             const css::uno::Any& value,
                             officev1::UserProperty* out, Warner& warner) {
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
    warner.warn("user property " + utf8(name) +
                " has an unmapped type and was emitted without a value");
  }
}

// Reads the custom properties. The category has a typed field, so the
// property the office core keeps it in fills that field instead of riding
// as a custom property.
void read_user_properties(
    const Reference<css::document::XDocumentProperties>& props,
    officev1::DocumentMetadata* metadata, Warner& warner) {
  try {
    Reference<css::beans::XPropertySet> user_props(
        props->getUserDefinedProperties(), UNO_QUERY);
    if (!user_props.is()) return;
    for (const css::beans::Property& definition :
         user_props->getPropertySetInfo()->getProperties()) {
      css::uno::Any value = user_props->getPropertyValue(definition.Name);
      rtl::OUString category;
      if (utf8(definition.Name) == kCategoryProperty && (value >>= category)) {
        metadata->set_category(utf8(category));
        continue;
      }
      officev1::UserProperty* out = metadata->add_user_properties();
      out->set_name(utf8(definition.Name));
      set_user_property_value(definition.Name, value, out, warner);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("user defined properties failed", error);
  }
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
  metadata->set_description(utf8(props->getDescription()));
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
  read_user_properties(props, metadata, warner);
  return emit_fn(event);
}

// Converts the view cursor's getPosition values into document-absolute
// twips. The office core reports them in 1/100 mm relative to the page text
// area (it subtracts the page style's top and left margins plus its fixed
// document border), so the conversion adds those back per page style. This
// keeps caret anchors and line rectangles in one coordinate space.
class CaretSpace {
 public:
  // pages, when given, are the laid-out page rectangles the page images and
  // line boxes are numbered by; see page_at.
  explicit CaretSpace(const Reference<css::frame::XModel>& model,
                      const std::vector<PageBox>* pages = nullptr)
      : model_(model), pages_(pages) {}

  // The 0-based rendered page whose rectangle holds the document-absolute
  // y, or -1 when no rectangles are known or none holds it. The view
  // cursor's own page number counts the blank pages the office core
  // inserts to put a section on a right or left page, which are never
  // rendered, so from the first such page on it runs ahead of the page
  // images; the rectangles are the numbering everything else uses.
  int32_t page_at(long y) const {
    if (pages_ == nullptr) return -1;
    for (size_t page = 0; page < pages_->size(); page++) {
      const PageBox& rect = (*pages_)[page];
      if (y >= rect.y && y < rect.y + rect.height) {
        return static_cast<int32_t>(page);
      }
    }
    return -1;
  }

  // Returns the (left, top) offset in twips for the page style at the
  // cursor, loading the style margin table on first use.
  std::pair<long, long> origin_for(
      const Reference<css::text::XTextViewCursor>& cursor, Warner& warner) {
    // The office core's fixed border around the page area, in twips.
    constexpr long kDocumentBorder = 284;
    load(warner);
    try {
      Reference<css::beans::XPropertySet> props(cursor, UNO_QUERY);
      if (props.is()) {
        rtl::OUString style;
        props->getPropertyValue("PageStyleName") >>= style;
        if (auto found = margins_.find(utf8(style));
            found != margins_.end()) {
          return {found->second.first + kDocumentBorder,
                  found->second.second + kDocumentBorder};
        }
      }
    } catch (const css::uno::Exception& error) {
      warner.warn("page style of caret failed", error);
    }
    return {kDocumentBorder, kDocumentBorder};
  }

 private:
  void load(Warner& warner) {
    if (loaded_) return;
    loaded_ = true;
    try {
      Reference<css::style::XStyleFamiliesSupplier> supplier(model_, UNO_QUERY);
      if (!supplier.is()) return;
      Reference<css::container::XNameAccess> families =
          supplier->getStyleFamilies();
      if (!families.is() || !families->hasByName("PageStyles")) return;
      Reference<css::container::XIndexAccess> styles;
      families->getByName("PageStyles") >>= styles;
      if (!styles.is()) return;
      for (sal_Int32 i = 0; i < styles->getCount(); i++) {
        Reference<css::style::XStyle> style;
        styles->getByIndex(i) >>= style;
        Reference<css::beans::XPropertySet> props(style, UNO_QUERY);
        if (!style.is() || !props.is()) continue;
        sal_Int32 left = 0, top = 0;
        props->getPropertyValue("LeftMargin") >>= left;
        props->getPropertyValue("TopMargin") >>= top;
        margins_[utf8(style->getName())] = {hundredth_mm_to_twips(left),
                                            hundredth_mm_to_twips(top)};
      }
    } catch (const css::uno::Exception& error) {
      warner.warn("page style margins are not readable", error);
    }
  }

  Reference<css::frame::XModel> model_;
  const std::vector<PageBox>* pages_ = nullptr;
  bool loaded_ = false;
  std::map<std::string, std::pair<long, long>> margins_;
};

// True when the range lies in a page header or footer text. Writer hands
// those texts out as SwXHeadFootText; body, frame and cell texts differ.
bool anchored_in_header_footer(const Reference<css::text::XTextRange>& range,
                               const std::string& what, Warner& warner) {
  if (!range.is()) return false;
  try {
    Reference<css::lang::XServiceInfo> info(range->getText(), UNO_QUERY);
    return info.is() && info->getImplementationName() == "SwXHeadFootText";
  } catch (const css::uno::Exception& error) {
    warner.warn("text of " + what + " failed", error);
  }
  return false;
}

// Positions the view cursor at the range and reports the caret point in
// document-absolute twips and the 0-based page. Reports failures against
// `what`.
void caret_at(const Reference<css::text::XTextViewCursor>& cursor,
              const Reference<css::text::XTextRange>& range,
              const std::string& what, CaretSpace* space,
              officev1::TwipsPoint* point, int32_t* page_index,
              Warner& warner) {
  if (!cursor.is() || !range.is()) return;
  try {
    cursor->gotoRange(range, false);
    css::awt::Point position = cursor->getPosition();
    std::pair<long, long> origin =
        space != nullptr ? space->origin_for(cursor, warner)
                         : std::pair<long, long>{0, 0};
    point->set_x(hundredth_mm_to_twips(position.X) + origin.first);
    point->set_y(hundredth_mm_to_twips(position.Y) + origin.second);
    if (page_index != nullptr) {
      *page_index = space != nullptr ? space->page_at(point->y()) : -1;
      if (*page_index < 0) {
        Reference<css::text::XPageCursor> page_cursor(cursor, UNO_QUERY);
        if (page_cursor.is()) *page_index = page_cursor->getPage() - 1;
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("caret position of " + what + " failed", error);
  }
}

// Parses the probe's last selection payload into LineBox entries: one
// "x, y, w, h" rectangle per laid-out line, joined by semicolons, in
// document twips. Blank payloads and the literal EMPTY parse to nothing.
void collect_line_rects(
    SelectionProbe* probe,
    google::protobuf::RepeatedPtrField<officev1::LineBox>* out) {
  struct Box {
    long x, y, width, height;
  };
  std::vector<Box> boxes;
  std::stringstream stream(probe->last_payload);
  std::string entry;
  while (std::getline(stream, entry, ';')) {
    Box box{0, 0, 0, 0};
    if (std::sscanf(entry.c_str(), "%ld , %ld , %ld , %ld", &box.x, &box.y,
                    &box.width, &box.height) != 4 ||
        box.width <= 0 || box.height <= 0) {
      continue;
    }
    boxes.push_back(box);
  }
  // The office core's region compression reorders the rectangles; restore
  // reading order.
  std::ranges::sort(boxes, [](const Box& a, const Box& b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
  });
  for (const Box& parsed : boxes) {
    officev1::LineBox* box = out->Add();
    box->set_x_twips(parsed.x);
    box->set_y_twips(parsed.y);
    box->set_width_twips(parsed.width);
    box->set_height_twips(parsed.height);
    box->set_page_index(-1);
    // Character boundaries are a separate measurement; -1 until it runs, so
    // an unmeasured pair is never mistaken for a real [0, 0).
    box->set_char_start(-1);
    box->set_char_end(-1);
    long mid = parsed.y + parsed.height / 2;
    for (size_t page = 0; page < probe->pages.size(); page++) {
      const PageBox& rect = probe->pages[page];
      if (mid >= rect.y && mid < rect.y + rect.height) {
        box->set_page_index(static_cast<int32_t>(page));
        break;
      }
    }
  }
}

// Selects [start, end] with the view cursor, drains the selection callback,
// and parses the per-line rectangles the layout reports for the selection.
// Re-collapses the cursor and the payload afterwards so later caret reads
// and measurements start clean.
void measure_line_rects(
    const Reference<css::text::XTextViewCursor>& cursor,
    const Reference<css::text::XTextRange>& start,
    const Reference<css::text::XTextRange>& end, SelectionProbe* probe,
    const std::string& what,
    google::protobuf::RepeatedPtrField<officev1::LineBox>* out,
    Warner& warner) {
  if (probe == nullptr || probe->reschedule == nullptr) return;
  if (!cursor.is() || !start.is() || !end.is()) return;
  try {
    probe->last_payload.clear();
    cursor->gotoRange(start, false);
    cursor->gotoRange(end, true);
    probe->flush();
    collect_line_rects(probe, out);
    cursor->gotoRange(start, false);
    probe->flush();
    probe->last_payload.clear();
  } catch (const css::uno::Exception& error) {
    warner.warn("line rectangles of " + what + " failed", error);
  }
}

int64_t codepoints(const std::string& utf8_text) {
  int64_t count = 0;
  for (unsigned char byte : utf8_text) {
    if ((byte & 0xC0) != 0x80) count++;
  }
  return count;
}

class MarkerCollector;

void fill_runs(const Reference<css::container::XEnumerationAccess>& paragraph,
               const std::string& label,
               google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
               int64_t* offset, MarkerCollector* marks, Warner& warner);

// Maps the office core's redline type name to the wire enum; unmatched
// types stay UNSPECIFIED with the verbatim name on the wire.
officev1::TrackedChangeKind tracked_change_kind(const std::string& type) {
  if (type == "Insert") return officev1::TRACKED_CHANGE_KIND_INSERT;
  if (type == "Delete") return officev1::TRACKED_CHANGE_KIND_DELETE;
  if (type == "Format") return officev1::TRACKED_CHANGE_KIND_FORMAT;
  if (type == "ParagraphFormat") {
    return officev1::TRACKED_CHANGE_KIND_PARAGRAPH_FORMAT;
  }
  if (type == "TextTable") return officev1::TRACKED_CHANGE_KIND_TABLE_CHANGE;
  if (type == "Style") return officev1::TRACKED_CHANGE_KIND_STYLE;
  return officev1::TRACKED_CHANGE_KIND_UNSPECIFIED;
}

// Classifies a form field from the office core's type string: the fieldmark
// type for in-text fields, the control model's service name for draw-page
// controls. Unmatched types stay UNSPECIFIED with the verbatim type on the
// wire.
officev1::FormFieldKind form_field_kind(const std::string& type) {
  if (type.contains("FORMTEXT") || type.contains("component.TextField")) {
    return officev1::FORM_FIELD_KIND_TEXT;
  }
  if (type.contains("FORMCHECKBOX") || type.contains("component.CheckBox")) {
    return officev1::FORM_FIELD_KIND_CHECKBOX;
  }
  if (type.contains("FORMDROPDOWN") || type.contains("component.ListBox") ||
      type.contains("component.ComboBox")) {
    return officev1::FORM_FIELD_KIND_DROPDOWN;
  }
  return officev1::FORM_FIELD_KIND_UNSPECIFIED;
}

// Reads one fieldmark parameter value in its stored type. Sequence-of-text
// values land in string_list; scalars in the value oneof. Unmatched types
// are dropped silently (the parameter name still records that it exists).
void fill_parameter(const css::uno::Any& value, officev1::FormFieldParameter* out) {
  css::uno::Sequence<rtl::OUString> texts;
  if (value >>= texts) {
    for (const rtl::OUString& text : texts) out->add_string_list(utf8(text));
    return;
  }
  bool flag = false;
  if (value >>= flag) {
    out->set_bool_value(flag);
    return;
  }
  sal_Int64 integer = 0;
  if (value >>= integer) {
    out->set_int_value(integer);
    return;
  }
  double number = 0;
  if (value >>= number) {
    out->set_double_value(number);
    return;
  }
  rtl::OUString text;
  if (value >>= text) out->set_string_value(utf8(text));
}

// Collects comment, tracked-change, bookmark, and form-field marks from the
// text portion walks, pairs their start and end sightings, and emits one
// typed event per mark. Marks in the body flow carry exact annotation-space
// offsets because the observation rides the same walk that counts them;
// marks in out-of-body text (table cells, headers, footnotes, frames) are
// still collected, with offsets -1. While a ranged mark is open, the run
// text walked past accumulates as its covered text; the covered text is
// only attached when the closing mark was actually seen, so an unclosed
// mark never claims text it may not cover.
class MarkerCollector {
 public:
  MarkerCollector(const PartSelection& parts,
                  const Reference<css::text::XTextViewCursor>& cursor,
                  CaretSpace* space, const EmitFn& emit_fn, Warner& warner)
      : want_comments_(parts.wants(officev1::DOCUMENT_PART_COMMENTS)),
        want_changes_(parts.wants(officev1::DOCUMENT_PART_TRACKED_CHANGES)),
        want_bookmarks_(parts.wants(officev1::DOCUMENT_PART_BOOKMARKS)),
        want_form_fields_(parts.wants(officev1::DOCUMENT_PART_FORM_FIELDS)),
        cursor_(cursor),
        space_(space),
        emit_fn_(emit_fn),
        warner_(warner) {}

  // True when emit_fn reported a gone parent; the walks should unwind.
  bool failed() const { return failed_; }

  // Observes one non-Text portion from a portion walk. offset is the
  // current annotation-space position, null outside the body flow.
  void on_portion(const rtl::OUString& type,
                  const Reference<css::text::XTextRange>& range,
                  const Reference<css::beans::XPropertySet>& props,
                  const int64_t* offset) {
    try {
      if (type == "Annotation" || type == "AnnotationEnd") {
        if (want_comments_) on_annotation(range, props, offset);
      } else if (type == "Bookmark") {
        if (want_bookmarks_) on_bookmark(range, props, offset);
      } else if (type == "Redline") {
        if (want_changes_) on_redline(range, props, offset);
      } else if (type == "TextFieldStart" || type == "TextFieldEnd" ||
                 type == "TextFieldStartEnd") {
        if (want_form_fields_) on_fieldmark(utf8(type), range, props, offset);
      }
    } catch (const css::uno::Exception& error) {
      warner_.warn("marker portion (" + utf8(type) + ") failed", error);
    }
  }

  // Accumulates walked run text into every open ranged mark.
  void on_run_text(const std::string& text) {
    for (auto& entry : open_comments_) entry.second.covered += text;
    for (auto& entry : open_changes_) entry.second.covered += text;
    for (auto& entry : open_bookmarks_) entry.second.covered += text;
    for (auto& entry : open_fields_) entry.second.covered += text;
  }

  // The newline the annotation text space appends after each body
  // paragraph, mirrored into open covered text.
  void on_paragraph_break() { on_run_text("\n"); }

  // Emits one draw-page form control as a FormField, sharing the emission
  // index with fieldmarks.
  void emit_form_control(officev1::FormField&& field) {
    officev1::StreamPagesResponse event;
    *event.mutable_form_field() = std::move(field);
    event.mutable_form_field()->set_index(field_index_++);
    emit(event);
  }

  // Emits every mark still open (a point comment, or a range whose end was
  // never walked), then sweeps the document-level suppliers for marks the
  // portion walks never met (bookmarks and comments in exotic locations,
  // hidden tracked changes). Returns false when the parent is gone.
  bool finish(const Reference<css::text::XTextDocument>& text_doc,
              const Reference<css::frame::XModel>& model) {
    flush_open();
    if (want_bookmarks_) sweep_bookmarks(text_doc);
    if (want_comments_) sweep_comments(text_doc);
    if (want_changes_) sweep_redlines(model);
    return !failed_;
  }

 private:
  // One open ranged mark: the partially built event plus the covered-text
  // accumulator, attached only when the closing mark is seen.
  template <typename Event>
  struct Open {
    Event event;
    std::string covered;
  };

  void emit(const officev1::StreamPagesResponse& event) {
    if (failed_) return;
    if (!emit_fn_(event)) failed_ = true;
  }

  // Positions the anchor caret and page for a mark's first sighting.
  void anchor_at(const Reference<css::text::XTextRange>& range,
                 const std::string& what, officev1::TwipsPoint* point,
                 int32_t* page_index) {
    *page_index = -1;
    caret_at(cursor_, range.is() ? range->getStart() : range, what, space_,
             point, page_index, warner_);
  }

  void on_annotation(const Reference<css::text::XTextRange>& range,
                     const Reference<css::beans::XPropertySet>& props,
                     const int64_t* offset) {
    Reference<css::text::XTextField> field;
    try {
      props->getPropertyValue("TextField") >>= field;
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: the range-end portion may not carry the
      // field.
    }
    std::string name;
    Reference<css::beans::XPropertySet> field_props(field, UNO_QUERY);
    if (field_props.is()) {
      rtl::OUString value;
      field_props->getPropertyValue("Name") >>= value;
      name = utf8(value);
    }
    if (!name.empty()) {
      auto found = open_comments_.find(name);
      if (found != open_comments_.end()) {
        close_comment(&found->second, offset);
        open_comments_.erase(found);
        return;
      }
    }
    if (!field_props.is()) {
      // A boundary portion without the field: close the most recent open
      // comment, or hold an anonymous span start until the field arrives.
      if (!open_comments_.empty()) {
        auto last = std::prev(open_comments_.end());
        close_comment(&last->second, offset);
        open_comments_.erase(last);
        return;
      }
      Open<officev1::Comment>& open =
          open_comments_["\nanon" + std::to_string(comment_index_)];
      open.event.set_char_start(offset != nullptr ? *offset : -1);
      int32_t page_index = -1;
      anchor_at(range, "comment anchor", open.event.mutable_anchor(),
                &page_index);
      open.event.set_page_index(page_index);
      return;
    }
    seen_comment_names_.insert(name);
    // A named sighting with no open partner: adopt a pending anonymous
    // span start when one exists, else open a new mark.
    officev1::Comment event;
    bool had_anonymous = false;
    for (auto it = open_comments_.begin(); it != open_comments_.end(); ++it) {
      if (it->first.starts_with("\nanon")) {
        event = std::move(it->second.event);
        // The covered text accumulated between the two boundary portions.
        event.set_anchored_text(it->second.covered);
        open_comments_.erase(it);
        had_anonymous = true;
        break;
      }
    }
    fill_comment_field(field, field_props, name, &event);
    if (had_anonymous) {
      event.set_char_end(offset != nullptr ? *offset : -1);
      event.set_index(comment_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_comment() = std::move(event);
      emit(response);
      return;
    }
    Open<officev1::Comment>& open = open_comments_[name];
    open.event = std::move(event);
    open.event.set_char_start(offset != nullptr ? *offset : -1);
    int32_t page_index = -1;
    anchor_at(range, "comment " + name + " anchor",
              open.event.mutable_anchor(), &page_index);
    open.event.set_page_index(page_index);
  }

  // Fills a comment's identity and content from its annotation field.
  void fill_comment_field(const Reference<css::text::XTextField>& field,
                          const Reference<css::beans::XPropertySet>& props,
                          const std::string& name, officev1::Comment* out) {
    out->set_name(name);
    rtl::OUString text;
    props->getPropertyValue("Author") >>= text;
    out->set_author(utf8(text));
    text = rtl::OUString();
    props->getPropertyValue("Content") >>= text;
    out->set_text(utf8(text));
    try {
      text = rtl::OUString();
      props->getPropertyValue("Initials") >>= text;
      out->set_initials(utf8(text));
      css::util::DateTime when;
      props->getPropertyValue("DateTimeValue") >>= when;
      out->set_epoch_ms(datetime_epoch_ms(when));
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: initials and creation time are optional.
    }
    try {
      bool resolved = false;
      props->getPropertyValue("Resolved") >>= resolved;
      out->set_resolved(resolved);
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: older office cores have no resolved state.
    }
    try {
      rtl::OUString parent;
      props->getPropertyValue("ParentName") >>= parent;
      out->set_parent_name(utf8(parent));
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: older office cores have no reply threading.
    }
    // Rich content when the field exposes its text; the plain Content
    // string already carries the content either way.
    Reference<css::text::XText> rich_text(field, UNO_QUERY);
    if (rich_text.is()) {
      Reference<css::container::XEnumerationAccess> access(rich_text, UNO_QUERY);
      if (access.is()) {
        Reference<css::container::XEnumeration> paragraphs =
            access->createEnumeration();
        while (paragraphs->hasMoreElements()) {
          Reference<css::container::XEnumerationAccess> paragraph(
              paragraphs->nextElement(), UNO_QUERY);
          if (paragraph.is()) {
            fill_runs(paragraph, "comment " + name + " content",
                      out->mutable_runs(), nullptr, nullptr, warner_);
          }
        }
      }
    }
  }

  void close_comment(Open<officev1::Comment>* open, const int64_t* offset) {
    open->event.set_char_end(offset != nullptr ? *offset : -1);
    open->event.set_anchored_text(open->covered);
    open->event.set_index(comment_index_++);
    officev1::StreamPagesResponse response;
    *response.mutable_comment() = std::move(open->event);
    emit(response);
  }

  void on_bookmark(const Reference<css::text::XTextRange>& range,
                   const Reference<css::beans::XPropertySet>& props,
                   const int64_t* offset) {
    Reference<css::container::XNamed> named;
    props->getPropertyValue("Bookmark") >>= named;
    if (!named.is()) return;
    std::string name = utf8(named->getName());
    seen_bookmark_names_.insert(name);
    bool collapsed = false;
    props->getPropertyValue("IsCollapsed") >>= collapsed;
    bool is_start = false;
    props->getPropertyValue("IsStart") >>= is_start;
    if (collapsed) {
      officev1::StreamPagesResponse response;
      officev1::Bookmark* out = response.mutable_bookmark();
      out->set_index(bookmark_index_++);
      out->set_name(stable_mark_name(name));
      out->set_char_start(offset != nullptr ? *offset : -1);
      out->set_char_end(offset != nullptr ? *offset : -1);
      int32_t page_index = -1;
      anchor_at(range, "bookmark " + name, out->mutable_anchor(), &page_index);
      out->set_page_index(page_index);
      emit(response);
      return;
    }
    auto found = open_bookmarks_.find(name);
    if (is_start && found == open_bookmarks_.end()) {
      Open<officev1::Bookmark>& open = open_bookmarks_[name];
      open.event.set_name(stable_mark_name(name));
      open.event.set_char_start(offset != nullptr ? *offset : -1);
      int32_t page_index = -1;
      anchor_at(range, "bookmark " + name, open.event.mutable_anchor(),
                &page_index);
      open.event.set_page_index(page_index);
      return;
    }
    if (found == open_bookmarks_.end()) {
      // An end mark with no open partner: a bookmark whose start sits in
      // text the walk has not covered. Emit what is known.
      officev1::StreamPagesResponse response;
      officev1::Bookmark* out = response.mutable_bookmark();
      out->set_index(bookmark_index_++);
      out->set_name(stable_mark_name(name));
      out->set_char_start(-1);
      out->set_char_end(offset != nullptr ? *offset : -1);
      int32_t page_index = -1;
      anchor_at(range, "bookmark " + name, out->mutable_anchor(), &page_index);
      out->set_page_index(page_index);
      emit(response);
      return;
    }
    found->second.event.set_char_end(offset != nullptr ? *offset : -1);
    found->second.event.set_covered_text(found->second.covered);
    found->second.event.set_index(bookmark_index_++);
    officev1::StreamPagesResponse response;
    *response.mutable_bookmark() = std::move(found->second.event);
    open_bookmarks_.erase(found);
    emit(response);
  }

  void on_redline(const Reference<css::text::XTextRange>& range,
                  const Reference<css::beans::XPropertySet>& props,
                  const int64_t* offset) {
    rtl::OUString value;
    props->getPropertyValue("RedlineIdentifier") >>= value;
    std::string identifier = utf8(value);
    bool is_start = false;
    props->getPropertyValue("IsStart") >>= is_start;
    auto found = open_changes_.find(identifier);
    if (found != open_changes_.end() && !is_start) {
      found->second.event.set_char_end(offset != nullptr ? *offset : -1);
      found->second.event.set_changed_text(found->second.covered);
      found->second.event.set_index(change_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_tracked_change() = std::move(found->second.event);
      open_changes_.erase(found);
      emit(response);
      return;
    }
    if (found != open_changes_.end()) return;  // A duplicate start mark.
    seen_change_ids_.insert(identifier);
    Open<officev1::TrackedChange>& open = open_changes_[identifier];
    fill_redline(props, identifier, &open.event);
    open.event.set_char_start(offset != nullptr ? *offset : -1);
    int32_t page_index = -1;
    anchor_at(range, "tracked change " + stable_change_id(identifier),
              open.event.mutable_anchor(), &page_index);
    open.event.set_page_index(page_index);
    if (!is_start) {
      // An end mark with no open partner: a change whose start sits in text
      // the walk has not covered. Emit what is known.
      open.event.set_char_end(offset != nullptr ? *offset : -1);
      open.event.set_char_start(-1);
      open.event.set_index(change_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_tracked_change() = std::move(open.event);
      open_changes_.erase(identifier);
      emit(response);
    }
  }

  // The identifier a tracked change is reported under. The office core's
  // RedlineIdentifier is the redline's address in memory, different on
  // every load; changes are numbered instead, 1 up, in the order the walks
  // first meet them, which the document alone decides.
  std::string stable_change_id(const std::string& identifier) {
    auto [found, added] = change_ordinals_.try_emplace(
        identifier, static_cast<int32_t>(change_ordinals_.size()) + 1);
    return std::to_string(found->second);
  }

  // Fills a tracked change's identity from a redline portion's (or a
  // document-level redline's) properties.
  void fill_redline(const Reference<css::beans::XPropertySet>& props,
                    const std::string& identifier,
                    officev1::TrackedChange* out) {
    out->set_identifier(stable_change_id(identifier));
    rtl::OUString text;
    props->getPropertyValue("RedlineType") >>= text;
    out->set_kind_name(utf8(text));
    out->set_kind(tracked_change_kind(out->kind_name()));
    text = rtl::OUString();
    props->getPropertyValue("RedlineAuthor") >>= text;
    out->set_author(utf8(text));
    text = rtl::OUString();
    props->getPropertyValue("RedlineComment") >>= text;
    out->set_comment(utf8(text));
    css::util::DateTime when;
    props->getPropertyValue("RedlineDateTime") >>= when;
    out->set_epoch_ms(datetime_epoch_ms(when));
    try {
      css::uno::Sequence<css::beans::PropertyValue> successor;
      props->getPropertyValue("RedlineSuccessorData") >>= successor;
      for (const css::beans::PropertyValue& entry : successor) {
        officev1::TrackedChangeSuccessor* under = out->mutable_successor();
        if (entry.Name == "RedlineType") {
          rtl::OUString kind;
          entry.Value >>= kind;
          under->set_kind_name(utf8(kind));
          under->set_kind(tracked_change_kind(under->kind_name()));
        } else if (entry.Name == "RedlineAuthor") {
          rtl::OUString author;
          entry.Value >>= author;
          under->set_author(utf8(author));
        } else if (entry.Name == "RedlineComment") {
          rtl::OUString comment;
          entry.Value >>= comment;
          under->set_comment(utf8(comment));
        } else if (entry.Name == "RedlineDateTime") {
          css::util::DateTime under_when;
          entry.Value >>= under_when;
          under->set_epoch_ms(datetime_epoch_ms(under_when));
        }
      }
      // A successor block whose fields were all empty is no successor.
      if (out->has_successor() && out->successor().kind_name().empty() &&
          out->successor().author().empty()) {
        out->clear_successor();
      }
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: not every office core exposes nesting data.
    }
  }

  void on_fieldmark(const std::string& type,
                    const Reference<css::text::XTextRange>& range,
                    const Reference<css::beans::XPropertySet>& props,
                    const int64_t* offset) {
    Reference<css::text::XFormField> fieldmark;
    try {
      props->getPropertyValue("Bookmark") >>= fieldmark;
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: the end portion may not carry the mark.
    }
    std::string name;
    Reference<css::container::XNamed> named(fieldmark, UNO_QUERY);
    if (named.is()) name = utf8(named->getName());
    if (type == "TextFieldEnd") {
      auto found = name.empty() && !open_fields_.empty()
                       ? std::prev(open_fields_.end())
                       : open_fields_.find(name);
      if (found == open_fields_.end()) return;  // A stray end mark.
      found->second.event.set_char_end(offset != nullptr ? *offset : -1);
      found->second.event.set_text(found->second.covered);
      found->second.event.set_index(field_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_form_field() = std::move(found->second.event);
      open_fields_.erase(found);
      emit(response);
      return;
    }
    if (!fieldmark.is()) return;
    officev1::FormField event;
    fill_fieldmark(fieldmark, name, &event);
    event.set_char_start(offset != nullptr ? *offset : -1);
    int32_t page_index = -1;
    anchor_at(range, "form field " + name, event.mutable_anchor(),
              &page_index);
    event.set_page_index(page_index);
    if (type == "TextFieldStartEnd") {
      event.set_char_end(event.char_start());
      event.set_index(field_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_form_field() = std::move(event);
      emit(response);
      return;
    }
    open_fields_[name].event = std::move(event);
  }

  // Fills a fieldmark's type, name, and parameters, projecting the
  // well-known checkbox and dropdown parameters into their typed fields.
  void fill_fieldmark(const Reference<css::text::XFormField>& fieldmark,
                      const std::string& name, officev1::FormField* out) {
    out->set_name(stable_mark_name(name));
    out->set_selected_index(-1);
    std::string field_type = utf8(fieldmark->getFieldType());
    out->set_field_type(field_type);
    out->set_kind(form_field_kind(field_type));
    Reference<css::container::XNameContainer> parameters =
        fieldmark->getParameters();
    if (!parameters.is()) return;
    css::uno::Sequence<rtl::OUString> names = parameters->getElementNames();
    for (const rtl::OUString& parameter_name : names) {
      officev1::FormFieldParameter* parameter = out->add_parameters();
      parameter->set_name(utf8(parameter_name));
      css::uno::Any value = parameters->getByName(parameter_name);
      fill_parameter(value, parameter);
      if (parameter->name() == "Checkbox_Checked") {
        out->set_checked(parameter->bool_value());
      } else if (parameter->name() == "Dropdown_ListEntry") {
        for (const std::string& entry : parameter->string_list()) {
          out->add_list_entries(entry);
        }
      } else if (parameter->name() == "Dropdown_Selected") {
        out->set_selected_index(static_cast<int32_t>(parameter->int_value()));
      }
    }
    if (out->kind() == officev1::FORM_FIELD_KIND_DROPDOWN &&
        out->selected_index() >= 0 &&
        out->selected_index() < out->list_entries_size()) {
      out->set_text(out->list_entries(out->selected_index()));
    }
  }

  // Emits every still-open mark. A comment with no end sighting is a point
  // comment; ranged marks whose end was never walked keep char_end -1 and
  // no covered text, so a partial walk never fabricates a span.
  void flush_open() {
    for (auto& entry : open_comments_) {
      officev1::Comment& event = entry.second.event;
      event.set_char_end(event.char_start());
      event.set_index(comment_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_comment() = std::move(event);
      emit(response);
    }
    open_comments_.clear();
    for (auto& entry : open_bookmarks_) {
      officev1::Bookmark& event = entry.second.event;
      event.set_char_end(-1);
      event.set_index(bookmark_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_bookmark() = std::move(event);
      emit(response);
    }
    open_bookmarks_.clear();
    for (auto& entry : open_changes_) {
      officev1::TrackedChange& event = entry.second.event;
      event.set_char_end(-1);
      event.set_index(change_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_tracked_change() = std::move(event);
      emit(response);
    }
    open_changes_.clear();
    for (auto& entry : open_fields_) {
      officev1::FormField& event = entry.second.event;
      event.set_char_end(-1);
      event.set_index(field_index_++);
      officev1::StreamPagesResponse response;
      *response.mutable_form_field() = std::move(event);
      emit(response);
    }
    open_fields_.clear();
  }

  // Emits bookmarks the portion walks never met (nested tables, exotic
  // containers) from the document's bookmark list, with no span.
  void sweep_bookmarks(const Reference<css::text::XTextDocument>& text_doc) {
    try {
      Reference<css::text::XBookmarksSupplier> supplier(text_doc, UNO_QUERY);
      if (!supplier.is()) return;
      Reference<css::container::XNameAccess> bookmarks =
          supplier->getBookmarks();
      if (!bookmarks.is()) return;
      for (const rtl::OUString& name : bookmarks->getElementNames()) {
        std::string bookmark_name = utf8(name);
        if (seen_bookmark_names_.count(bookmark_name) != 0) continue;
        officev1::StreamPagesResponse response;
        officev1::Bookmark* out = response.mutable_bookmark();
        out->set_index(bookmark_index_++);
        out->set_name(stable_mark_name(bookmark_name));
        out->set_char_start(-1);
        out->set_char_end(-1);
        Reference<css::text::XTextContent> content;
        bookmarks->getByName(name) >>= content;
        int32_t page_index = -1;
        if (content.is()) {
          anchor_at(content->getAnchor(), "bookmark " + bookmark_name,
                    out->mutable_anchor(), &page_index);
        }
        out->set_page_index(page_index);
        emit(response);
      }
    } catch (const css::uno::Exception& error) {
      warner_.warn("bookmark sweep failed", error);
    }
  }

  // Emits comments the portion walks never met from the document's text
  // field list, with no span.
  void sweep_comments(const Reference<css::text::XTextDocument>& text_doc) {
    try {
      Reference<css::text::XTextFieldsSupplier> supplier(text_doc, UNO_QUERY);
      if (!supplier.is()) return;
      Reference<css::container::XEnumerationAccess> fields(
          supplier->getTextFields(), UNO_QUERY);
      if (!fields.is()) return;
      Reference<css::container::XEnumeration> it = fields->createEnumeration();
      while (it->hasMoreElements()) {
        Reference<css::text::XTextField> field(it->nextElement(), UNO_QUERY);
        Reference<css::lang::XServiceInfo> services(field, UNO_QUERY);
        if (!services.is() ||
            !services->supportsService(
                "com.sun.star.text.textfield.Annotation")) {
          continue;
        }
        Reference<css::beans::XPropertySet> props(field, UNO_QUERY);
        if (!props.is()) continue;
        rtl::OUString value;
        props->getPropertyValue("Name") >>= value;
        std::string name = utf8(value);
        if (seen_comment_names_.count(name) != 0) continue;
        seen_comment_names_.insert(name);
        officev1::StreamPagesResponse response;
        officev1::Comment* out = response.mutable_comment();
        fill_comment_field(field, props, name, out);
        out->set_index(comment_index_++);
        out->set_char_start(-1);
        out->set_char_end(-1);
        int32_t page_index = -1;
        anchor_at(field->getAnchor(), "comment " + name,
                  out->mutable_anchor(), &page_index);
        out->set_page_index(page_index);
        emit(response);
      }
    } catch (const css::uno::Exception& error) {
      warner_.warn("comment sweep failed", error);
    }
  }

  // Emits tracked changes the portion walks never met (a deletion while
  // deletions are hidden, a change inside a nested table) from the
  // document's redline list, with no span. The hidden deleted text rides
  // changed_text from the redline's stored content.
  void sweep_redlines(const Reference<css::frame::XModel>& model) {
    try {
      Reference<css::document::XRedlinesSupplier> supplier(model, UNO_QUERY);
      if (!supplier.is()) return;
      Reference<css::container::XEnumerationAccess> redlines(
          supplier->getRedlines(), UNO_QUERY);
      if (!redlines.is()) return;
      Reference<css::container::XEnumeration> it = redlines->createEnumeration();
      while (it->hasMoreElements()) {
        Reference<css::beans::XPropertySet> props(it->nextElement(), UNO_QUERY);
        if (!props.is()) continue;
        rtl::OUString value;
        props->getPropertyValue("RedlineIdentifier") >>= value;
        std::string identifier = utf8(value);
        if (seen_change_ids_.count(identifier) != 0) continue;
        seen_change_ids_.insert(identifier);
        officev1::StreamPagesResponse response;
        officev1::TrackedChange* out = response.mutable_tracked_change();
        fill_redline(props, identifier, out);
        out->set_index(change_index_++);
        out->set_char_start(-1);
        out->set_char_end(-1);
        out->set_page_index(-1);
        try {
          Reference<css::text::XText> hidden;
          props->getPropertyValue("RedlineText") >>= hidden;
          if (hidden.is()) out->set_changed_text(utf8(hidden->getString()));
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: only deletions carry stored text.
        }
        try {
          Reference<css::text::XTextRange> start;
          props->getPropertyValue("RedlineStart") >>= start;
          if (start.is()) {
            int32_t page_index = -1;
            anchor_at(start, "tracked change " + stable_change_id(identifier),
                      out->mutable_anchor(), &page_index);
            out->set_page_index(page_index);
          }
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: a fully hidden change has no live range.
        }
        emit(response);
      }
    } catch (const css::uno::Exception& error) {
      warner_.warn("tracked change sweep failed", error);
    }
  }

  bool want_comments_ = false;
  bool want_changes_ = false;
  bool want_bookmarks_ = false;
  bool want_form_fields_ = false;
  Reference<css::text::XTextViewCursor> cursor_;
  CaretSpace* space_ = nullptr;
  EmitFn emit_fn_;
  Warner& warner_;
  bool failed_ = false;
  std::map<std::string, Open<officev1::Comment>> open_comments_;
  std::map<std::string, Open<officev1::TrackedChange>> open_changes_;
  std::map<std::string, Open<officev1::Bookmark>> open_bookmarks_;
  std::map<std::string, Open<officev1::FormField>> open_fields_;
  std::set<std::string> seen_comment_names_;
  std::set<std::string> seen_bookmark_names_;
  std::set<std::string> seen_change_ids_;
  std::map<std::string, int32_t> change_ordinals_;
  int32_t comment_index_ = 0;
  int32_t change_index_ = 0;
  int32_t bookmark_index_ = 0;
  int32_t field_index_ = 0;
};

// Attributes item-local [char_start, char_end) code-point offsets to the
// measured line rectangles by walking the layout's visual lines: the view
// cursor answers XLineCursor (start and end of the current line) and
// XViewCursor (down one line), a model cursor from the range's start to the
// view cursor position converts each stop into a code-point offset, and the
// caret's document-absolute y locates the rectangle the visual line lives
// in. A rectangle covers several visual lines when the office core's
// region compression merged equal-width neighbours, so each rectangle
// accumulates the offsets of every visual line it contains. A walk that
// cannot finish (or fails, or leaves a visual line without a rectangle)
// keeps every boundary at -1, so geometry never degrades. Leaves the view
// cursor collapsed at start.
void measure_line_boundaries(
    const Reference<css::text::XTextViewCursor>& cursor,
    const Reference<css::text::XTextRange>& start,
    const Reference<css::text::XTextRange>& end, CaretSpace* space,
    const std::string& what,
    google::protobuf::RepeatedPtrField<officev1::LineBox>* lines,
    Warner& warner) {
  if (lines->empty()) return;
  if (!cursor.is() || !start.is() || !end.is()) return;
  try {
    Reference<css::view::XLineCursor> line_cursor(cursor, UNO_QUERY);
    Reference<css::view::XViewCursor> view_cursor(cursor, UNO_QUERY);
    Reference<css::text::XText> text = start->getText();
    if (!line_cursor.is() || !view_cursor.is() || !text.is()) return;
    Reference<css::text::XTextCursor> model =
        text->createTextCursorByRange(start);
    if (!model.is()) return;
    auto offset_of = [&](const Reference<css::text::XTextRange>& to) {
      model->gotoRange(start, false);
      model->gotoRange(to, true);
      return codepoints(utf8(model->getString()));
    };
    int64_t total = offset_of(end);
    struct Span {
      int64_t start = -1;
      int64_t end = -1;
    };
    std::vector<Span> spans(lines->size());
    bool finished = false;
    cursor->gotoRange(start, false);
    line_cursor->gotoStartOfLine(false);
    // The bound covers any reasonable paragraph; a walk still unfinished at
    // the bound (or a cursor that stops advancing) discards everything.
    constexpr int kMaxVisualLines = 512;
    int64_t previous_end = -1;
    for (int guard = 0; guard < kMaxVisualLines; guard++) {
      int64_t line_start = offset_of(cursor->getStart());
      // The caret sits at the visual line's start; its document-absolute y
      // picks the rectangle, with the same conversion slack caret readers
      // use because the caret round-trips through 1/100 mm.
      css::awt::Point position = cursor->getPosition();
      std::pair<long, long> origin =
          space != nullptr ? space->origin_for(cursor, warner)
                           : std::pair<long, long>{0, 0};
      long y = hundredth_mm_to_twips(position.Y) + origin.second;
      line_cursor->gotoEndOfLine(false);
      int64_t line_end = offset_of(cursor->getStart());
      if (line_end < line_start || line_start < previous_end) break;
      previous_end = line_end;
      int box_index = -1;
      for (int i = 0; i < lines->size(); i++) {
        const officev1::LineBox& box = lines->Get(i);
        if (y >= box.y_twips() - 30
            && y < box.y_twips() + box.height_twips()) {
          box_index = i;
          break;
        }
      }
      if (box_index < 0) break;
      Span& span = spans[box_index];
      if (span.start < 0 || line_start < span.start) span.start = line_start;
      if (line_end > span.end) span.end = line_end;
      if (line_end >= total) {
        finished = true;
        break;
      }
      // Advance to the next visual line. A soft wrap's end-of-line caret
      // shares its text position with the next line's start, so
      // gotoStartOfLine from it already lands there; only an explicit line
      // break, where the position stays on this line, needs the step down.
      line_cursor->gotoStartOfLine(false);
      if (offset_of(cursor->getStart()) <= line_start) {
        if (!view_cursor->goDown(1, false)) break;
        line_cursor->gotoStartOfLine(false);
      }
    }
    cursor->gotoRange(start, false);
    if (!finished) return;
    for (int i = 0; i < lines->size(); i++) {
      if (spans[i].start < 0) continue;
      lines->Mutable(i)->set_char_start(spans[i].start);
      lines->Mutable(i)->set_char_end(spans[i].end);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("line boundaries of " + what + " failed", error);
  }
}

// Reads the character attributes of one text portion onto its run: font,
// size, weight, slant, underline, strikeout, color, vertical escapement,
// locale, and the hyperlink the portion sits inside. The four attributes
// every text model carries are read unguarded, so a model that cannot answer
// them unwinds to the caller's warning; the optional ones probe.
void fill_run_char_props(const Reference<css::beans::XPropertySet>& props,
                         officev1::TextRun* run) {
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
  try {
    sal_Int16 escapement = 0;
    props->getPropertyValue("CharEscapement") >>= escapement;
    run->set_escapement(escapement);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: not every text model models escapement.
  }
  try {
    sal_Int16 pitch = css::awt::FontPitch::DONTKNOW;
    props->getPropertyValue("CharFontPitch") >>= pitch;
    run->set_monospace(pitch == css::awt::FontPitch::FIXED);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: font pitch is an optional character property.
  }
  try {
    sal_Int16 case_map = css::style::CaseMap::NONE;
    props->getPropertyValue("CharCaseMap") >>= case_map;
    run->set_small_caps(case_map == css::style::CaseMap::SMALLCAPS);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: case mapping is an optional character property.
  }
  try {
    sal_Int16 overline = 0;
    props->getPropertyValue("CharOverline") >>= overline;
    run->set_overline(overline != 0);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: overlining is an optional character property.
  }
  try {
    rtl::OUString style;
    props->getPropertyValue("CharStyleName") >>= style;
    run->set_char_style(utf8(style));
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: only text models with a style catalogue name
    // their character styles.
  }
  // -1 is the office core's own transparent value, and it is what a run
  // without a highlight reports.
  run->set_highlight_rgb(-1);
  try {
    sal_Int32 highlight = -1;
    props->getPropertyValue("CharBackColor") >>= highlight;
    run->set_highlight_rgb(highlight);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: character backgrounds are optional.
  }
  try {
    css::lang::Locale locale;
    props->getPropertyValue("CharLocale") >>= locale;
    std::string tag = utf8(locale.Language);
    if (!tag.empty() && !locale.Country.isEmpty()) {
      tag += "-" + utf8(locale.Country);
    }
    run->set_language(tag);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: character locales are optional.
  }
  try {
    rtl::OUString url;
    props->getPropertyValue("HyperLinkURL") >>= url;
    if (!url.isEmpty()) {
      run->set_hyperlink_url(utf8(url));
      rtl::OUString target;
      props->getPropertyValue("HyperLinkTarget") >>= target;
      run->set_hyperlink_target(utf8(target));
      rtl::OUString link_name;
      props->getPropertyValue("HyperLinkName") >>= link_name;
      run->set_hyperlink_name(utf8(link_name));
    }
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: not every text model carries hyperlink
    // character properties.
  }
}

// The last segment of a text field's programmatic service name, for example
// "PageNumber" for com.sun.star.text.textfield.PageNumber. A field answers
// several service names; the most specific one wins, so a document-info
// field reports "docinfo.Title" rather than the shared base name. Empty when
// the field does not name itself.
namespace {
std::string& source_name_storage() {
  static std::string name;
  return name;
}
}  // namespace

// What a file-name field prints, for the upload rather than the copy the
// worker loaded: that copy sits at <work dir>/doc.<ext>, a temp path that
// means nothing to a reader and names the service's own filesystem. The
// field's display format still decides between the name with and without
// its extension; a path-only format prints nothing.
std::string file_name_field_text(const Reference<css::beans::XPropertySet>& field) {
  sal_Int16 format = css::text::FilenameDisplayFormat::FULL;
  try {
    if (field.is()) field->getPropertyValue("FileFormat") >>= format;
  } catch (const css::uno::Exception&) {
    // Expected probe result: the default full form applies.
  }
  const std::string& name = source_name_storage();
  switch (format) {
    case css::text::FilenameDisplayFormat::PATH:
      return std::string();
    case css::text::FilenameDisplayFormat::NAME: {
      const size_t dot = name.rfind('.');
      return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }
    default:
      return name;
  }
}

std::string text_field_code(const Reference<css::text::XTextField>& field) {
  Reference<css::lang::XServiceInfo> info(field, UNO_QUERY);
  if (!info.is()) return std::string();
  static const std::string kPrefix = "com.sun.star.text.textfield.";
  std::string best;
  for (const rtl::OUString& name : info->getSupportedServiceNames()) {
    std::string service = utf8(name);
    if (!service.starts_with(kPrefix)) continue;
    std::string code = service.substr(kPrefix.size());
    if (code.size() > best.size()) best = code;
  }
  return best;
}

// The name a cross-reference or reference-mark field points at (the
// bookmark, reference mark, or sequence it resolves against); empty for
// every other field.
std::string text_field_target(const Reference<css::text::XTextField>& field) {
  Reference<css::beans::XPropertySet> props(field, UNO_QUERY);
  if (!props.is()) return std::string();
  try {
    rtl::OUString source;
    props->getPropertyValue("SourceName") >>= source;
    return utf8(source);
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: only reference fields point at a name.
  } catch (const css::uno::Exception&) {
    // A field that refuses the read simply has no target to report.
  }
  return std::string();
}

// Appends the rendered result of one field portion as a run tagged with the
// field's own type. Field results are the printed page numbers, dates,
// cross-references, caption numbers, and mail-merge values a reader sees;
// without this they are holes in the extracted text. A field that renders
// nothing adds no run.
void fill_field_run(const Reference<css::text::XTextRange>& range,
                    const Reference<css::beans::XPropertySet>& props,
                    const std::string& label,
                    google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
                    int64_t* offset, MarkerCollector* marks, Warner& warner) {
  Reference<css::text::XTextField> field;
  try {
    props->getPropertyValue("TextField") >>= field;
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: a field portion may not expose its field.
  }
  std::string code = field.is() ? text_field_code(field) : std::string();
  std::string resolved = utf8(range->getString());
  if (code == "FileName") {
    resolved = file_name_field_text(
        Reference<css::beans::XPropertySet>(field, UNO_QUERY));
  } else if (resolved.empty() && field.is()) {
    try {
      resolved = utf8(field->getPresentation(false));
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " field presentation query failed", error);
    }
  }
  if (resolved.empty()) return;
  officev1::TextRun* run = runs->Add();
  run->set_text(resolved);
  int64_t length = codepoints(resolved);
  run->set_char_length(length);
  if (offset != nullptr) {
    run->set_char_offset(*offset);
    *offset += length;
  } else {
    run->set_char_offset(-1);
  }
  // A field whose service names say nothing is still a field; the generic
  // code keeps generated text distinguishable from authored text.
  run->set_field_code(code.empty() ? "TextField" : code);
  if (field.is()) run->set_field_target(text_field_target(field));
  try {
    fill_run_char_props(props, run);
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " field run character properties failed", error);
  }
  if (marks != nullptr) marks->on_run_text(run->text());
}

// Appends one run per uniformly formatted text portion. When offset is null
// the runs are outside the body flow and carry char_offset -1; otherwise
// *offset is the running position in the annotation text space and advances
// by each run's length. Field portions contribute their rendered result as
// a tagged run, so the annotation space and the extracted text both hold
// what the page shows. marks, when non-null, observes the remaining non-Text
// portions (comment, bookmark, tracked-change, and form-field boundaries)
// and the walked run text.
void fill_runs(const Reference<css::container::XEnumerationAccess>& paragraph,
               const std::string& label,
               google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
               int64_t* offset, MarkerCollector* marks, Warner& warner) {
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
      if (portion_type != "Text") {
        if (portion_type == "TextField") {
          fill_field_run(range, props, label, runs, offset, marks, warner);
          continue;
        }
        if (marks != nullptr) {
          marks->on_portion(portion_type, range, props, offset);
        }
        continue;
      }
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
      fill_run_char_props(props, run);
      if (marks != nullptr) marks->on_run_text(run->text());
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
// offset non-null). marks, when non-null, observes the paragraph's marker
// portions.
bool fill_paragraph(const css::uno::Any& element, int32_t index,
                    const Reference<css::text::XTextViewCursor>& cursor,
                    CaretSpace* space, int64_t* offset, SelectionProbe* probe,
                    MarkerCollector* marks, officev1::Paragraph* out,
                    Warner& warner) {
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
    caret_at(cursor, range->getStart(), label + " start", space,
             out->mutable_start(), &page_index, warner);
    caret_at(cursor, range->getEnd(), label + " end", space,
             out->mutable_end(), nullptr, warner);
    out->set_page_index(page_index);
    measure_line_rects(cursor, range->getStart(), range->getEnd(), probe,
                       label, out->mutable_line_rects(), warner);
    measure_line_boundaries(cursor, range->getStart(), range->getEnd(), space,
                            label, out->mutable_line_rects(), warner);
  }
  fill_runs(paragraph, label, out->mutable_runs(), offset, marks, warner);
  if (offset != nullptr) {
    *offset += 1;  // The newline after each body paragraph.
    if (marks != nullptr) marks->on_paragraph_break();
  }
  return true;
}

// "B7" -> row 6, column 1. Split-cell names ("B7.1.2") have no base-grid
// position of their own and report -1/-1; the name itself stays on the wire,
// and the anchoring cell it starts from is recoverable from it.
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
  size_t digit = pos;
  for (; digit < name.size() &&
         std::isdigit(static_cast<unsigned char>(name[digit]));
       digit++) {
    row_number = row_number * 10 + (name[digit] - '0');
  }
  if (digit == pos || row_number <= 0 || digit != name.size()) return;
  *row = static_cast<int32_t>(row_number - 1);
  *column = static_cast<int32_t>(col - 1);
}

// Derives each cell's column span from the gaps between the cell names of
// its row: a horizontal merge is the only thing that makes a text table's
// row jump from A1 straight to C1, and the width of the jump is the merged
// cell's span. Cells with no base-grid position of their own keep span 1.
void fill_column_spans(officev1::TableData* table) {
  std::map<int32_t, std::vector<officev1::TableCellData*>> rows;
  for (officev1::TableCellData& cell : *table->mutable_cells()) {
    if (cell.row() < 0 || cell.column() < 0) continue;
    rows[cell.row()].push_back(&cell);
  }
  for (auto& [row, cells] : rows) {
    std::ranges::sort(cells, {}, [](const officev1::TableCellData* cell) {
      return cell->column();
    });
    for (size_t i = 0; i < cells.size(); i++) {
      int32_t next = i + 1 < cells.size() ? cells[i + 1]->column()
                                          : table->columns();
      int32_t span = next - cells[i]->column();
      cells[i]->set_column_span(span > 1 ? span : 1);
    }
  }
}

// Lays a text table's cells onto the column grid its rows share. A Writer
// table names cells per row (a row of three cells is A, B, C whatever the
// row above holds), so names alone cannot say that a three-cell header sits
// over five columns; the rows' column separators can. Every row's
// separators, in the table's relative width space, merge into one ordered
// edge set; a cell's column and span are its left and right edges' places
// in that set. False when the separators are unavailable, leaving the
// name-derived positions in place.
bool fill_grid_from_separators(const Reference<css::text::XTextTable>& table,
                               officev1::TableData* out) {
  Reference<css::container::XIndexAccess> rows(table->getRows(), UNO_QUERY);
  Reference<css::beans::XPropertySet> table_props(table, UNO_QUERY);
  if (!rows.is() || !table_props.is()) return false;
  sal_Int16 relative_sum = 0;
  try {
    table_props->getPropertyValue("TableColumnRelativeSum") >>= relative_sum;
  } catch (const css::uno::Exception&) {
    return false;
  }
  if (relative_sum <= 0) return false;
  std::vector<std::vector<sal_Int32>> row_edges(rows->getCount());
  for (sal_Int32 r = 0; r < rows->getCount(); r++) {
    css::uno::Sequence<css::text::TableColumnSeparator> separators;
    try {
      Reference<css::beans::XPropertySet> row_props(rows->getByIndex(r), UNO_QUERY);
      if (!row_props.is()) return false;
      row_props->getPropertyValue("TableColumnSeparators") >>= separators;
    } catch (const css::uno::Exception&) {
      return false;
    }
    std::vector<sal_Int32>& edges = row_edges[r];
    edges.push_back(0);
    for (const css::text::TableColumnSeparator& separator : separators) {
      edges.push_back(separator.Position);
    }
    edges.push_back(relative_sum);
    std::ranges::sort(edges);
  }
  // Edges of different rows within half a percent of each other are one
  // edge: the importers round separator positions per row. Two edges of the
  // same row are always two edges, however close: a narrow column (a Word
  // gridAfter of a few twips) is still a column its cell occupies.
  struct Edge {
    sal_Int32 position;
    size_t row;
    size_t place;
  };
  std::vector<Edge> all_edges;
  for (size_t r = 0; r < row_edges.size(); r++) {
    for (size_t i = 0; i < row_edges[r].size(); i++) {
      all_edges.push_back({row_edges[r][i], r, i});
    }
  }
  std::ranges::sort(all_edges, {}, &Edge::position);
  const sal_Int32 tolerance = std::max<sal_Int32>(1, relative_sum / 200);
  std::vector<std::vector<int32_t>> edge_column(row_edges.size());
  for (size_t r = 0; r < row_edges.size(); r++) {
    edge_column[r].assign(row_edges[r].size(), 0);
  }
  int32_t grid_lines = 0;
  sal_Int32 cluster_start = 0;
  std::set<size_t> cluster_rows;
  for (const Edge& edge : all_edges) {
    if (grid_lines == 0 || edge.position - cluster_start > tolerance ||
        cluster_rows.contains(edge.row)) {
      grid_lines++;
      cluster_start = edge.position;
      cluster_rows.clear();
    }
    cluster_rows.insert(edge.row);
    edge_column[edge.row][edge.place] = grid_lines - 1;
  }
  if (grid_lines < 2) return false;
  std::map<int32_t, std::vector<officev1::TableCellData*>> by_row;
  for (officev1::TableCellData& cell : *out->mutable_cells()) {
    if (cell.row() < 0 || cell.column() < 0) continue;
    by_row[cell.row()].push_back(&cell);
  }
  for (auto& [row, cells] : by_row) {
    if (row < 0 || static_cast<size_t>(row) >= row_edges.size()) continue;
    const std::vector<sal_Int32>& edges = row_edges[row];
    // A cell's name letter is its box's place in the row, and the row's
    // separators bound those boxes. The boxes a vertical merge covers are
    // not named, so a row under a merged cell names fewer cells than it has
    // boxes ("B2", "C2" beside a covered A2); indexing the edges by the name
    // keeps those cells on their own columns. A row whose boxes and
    // separators disagree (split cells) keeps its name-derived positions.
    int32_t last_box = -1;
    for (const officev1::TableCellData* cell : cells) {
      last_box = std::max(last_box, cell->column());
    }
    if (edges.size() < static_cast<size_t>(last_box) + 2) continue;
    for (officev1::TableCellData* cell : cells) {
      const size_t box = static_cast<size_t>(cell->column());
      const int32_t left = edge_column[row][box];
      const int32_t right = edge_column[row][box + 1];
      cell->set_column(left);
      cell->set_column_span(std::max<int32_t>(1, right - left));
    }
  }
  out->set_columns(grid_lines - 1);
  return true;
}

bool emit_table(const Reference<css::text::XTextTable>& table, int32_t index,
                const Reference<css::text::XTextViewCursor>& cursor,
                CaretSpace* space, const PartSelection& parts,
                SelectionProbe* probe, const EmitFn& emit_fn, Warner& warner) {
  officev1::StreamPagesResponse event;
  officev1::TableData* out = event.mutable_table();
  out->set_index(index);
  out->set_page_index(-1);
  std::string label = "table " + std::to_string(index);
  try {
    out->set_rows(table->getRows()->getCount());
    out->set_columns(table->getColumns()->getCount());
    css::uno::Sequence<rtl::OUString> names = table->getCellNames();
    // A whole-table selection is impossible through the view cursor (the
    // office core forbids expanding a selection across cell boundaries), so
    // line rectangles are measured cell by cell, once per cell, and routed
    // to the requested targets. The table-level pool is bounded to small
    // tables so its cost under the default part selection stays negligible;
    // the explicit per-cell part carries no bound because its cost was
    // opted into.
    bool measurable = probe != nullptr && probe->reschedule != nullptr;
    bool want_cell = measurable &&
        parts.explicit_wants(officev1::DOCUMENT_PART_CELL_LINE_RECTS);
    bool want_pool = measurable &&
        parts.wants(officev1::DOCUMENT_PART_LINE_RECTS) &&
        names.getLength() <= 64;
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
      cell_out->set_row_span(1);
      cell_out->set_column_span(1);
      Reference<css::beans::XPropertySet> cell_props(cell, UNO_QUERY);
      if (cell_props.is()) {
        try {
          sal_Int32 span = 1;
          cell_props->getPropertyValue("RowSpan") >>= span;
          // The office core reports 0 or a negative count on the cells a
          // vertical merge covers; the anchor carries the whole block.
          cell_out->set_row_span(span > 0 ? static_cast<int32_t>(span) : 0);
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: older office cores expose no row span.
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " cell " + cell_name + " row span query failed",
                      error);
        }
      }
      if (want_cell || want_pool) {
        auto* target = want_cell ? cell_out->mutable_line_rects()
                                 : out->mutable_line_rects();
        measure_line_rects(cursor, cell->getStart(), cell->getEnd(), probe,
                           label + " cell " + cell_name, target, warner);
        if (want_cell && want_pool) {
          for (const officev1::LineBox& box : cell_out->line_rects()) {
            *out->add_line_rects() = box;
          }
        }
      }
    }
    if (!fill_grid_from_separators(table, out)) fill_column_spans(out);
    if (names.hasElements()) {
      Reference<css::text::XText> first(table->getCellByName(names[0]), UNO_QUERY);
      Reference<css::text::XText> last(
          table->getCellByName(names[names.getLength() - 1]), UNO_QUERY);
      int32_t page_index = -1;
      if (first.is()) {
        caret_at(cursor, first->getStart(), label + " start", space,
                 out->mutable_start(), &page_index, warner);
      }
      if (last.is()) {
        caret_at(cursor, last->getEnd(), label + " end", space,
                 out->mutable_end(), nullptr, warner);
      }
      out->set_page_index(page_index);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " extraction failed", error);
  }
  return emit_fn(event);
}

// Walks a table's cell paragraphs for marker portions only. Cell text is
// outside the annotation space, so the marks carry no offsets; the walk
// still pairs range boundaries and accumulates covered text.
void walk_table_marks(const Reference<css::text::XTextTable>& table,
                      const std::string& label, MarkerCollector* marks,
                      Warner& warner) {
  try {
    css::uno::Sequence<rtl::OUString> names = table->getCellNames();
    for (const rtl::OUString& name : names) {
      Reference<css::container::XEnumerationAccess> cell(
          table->getCellByName(name), UNO_QUERY);
      if (!cell.is()) continue;
      Reference<css::container::XEnumeration> paragraphs =
          cell->createEnumeration();
      while (paragraphs->hasMoreElements()) {
        Reference<css::container::XEnumerationAccess> paragraph(
            paragraphs->nextElement(), UNO_QUERY);
        if (!paragraph.is()) continue;  // A nested table; the sweeps cover it.
        google::protobuf::RepeatedPtrField<officev1::TextRun> scratch;
        fill_runs(paragraph, label + " cell " + utf8(name), &scratch, nullptr,
                  marks, warner);
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " marker walk failed", error);
  }
}

// Flattens the paragraphs of an arbitrary text (footnote body, generated
// index, frame or shape text) into runs outside the annotation space.
// marks, when non-null, observes the walked marker portions with no
// offsets.
void flatten_text_runs(const Reference<css::text::XText>& text,
                       const std::string& label,
                       google::protobuf::RepeatedPtrField<officev1::TextRun>* runs,
                       MarkerCollector* marks, Warner& warner) {
  Reference<css::container::XEnumerationAccess> access(text, UNO_QUERY);
  if (!access.is()) return;
  Reference<css::container::XEnumeration> paragraphs = access->createEnumeration();
  while (paragraphs->hasMoreElements()) {
    Reference<css::container::XEnumerationAccess> paragraph(
        paragraphs->nextElement(), UNO_QUERY);
    if (!paragraph.is()) continue;
    // Paragraphs stay apart in the flattened text: a line break between
    // one paragraph's runs and the next, as the page shows them, rather
    // than the last word of one running into the first of the next.
    const int before = runs->size();
    if (before > 0) {
      // The break carries the preceding run's character formatting, so a
      // uniformly formatted text stays uniform, but none of its link or
      // field identity.
      officev1::TextRun* separator = runs->Add();
      *separator = runs->Get(before - 1);
      separator->set_text("\n");
      separator->set_char_offset(-1);
      separator->set_char_length(1);
      separator->clear_hyperlink_url();
      separator->clear_hyperlink_target();
      separator->clear_hyperlink_name();
      separator->clear_field_code();
      separator->clear_field_target();
      separator->clear_escapement();
    }
    fill_runs(paragraph, label, runs, nullptr, marks, warner);
    if (before > 0 && runs->size() == before + 1) runs->RemoveLast();
  }
}

// Reads a shape's accessibility title and description, the alt text an
// author writes for a reader who cannot see the picture. Both properties
// are optional across every shape family, so an absent one is not a
// problem worth a warning.
void fill_alt_text(const Reference<css::beans::XPropertySet>& props,
                   std::string* title, std::string* description) {
  if (!props.is()) return;
  try {
    rtl::OUString value;
    props->getPropertyValue("Title") >>= value;
    *title = utf8(value);
  } catch (const css::uno::Exception&) {
    // Expected probe result: not every shape carries a title.
  }
  try {
    rtl::OUString value;
    props->getPropertyValue("Description") >>= value;
    *description = utf8(value);
  } catch (const css::uno::Exception&) {
    // Expected probe result: not every shape carries a description.
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
    warner.note("graphic provider unavailable, image bytes will be missing",
                error);
  }
  return provider;
}

// Re-encodes a graphic through the provider entirely in memory, preferring
// the graphic's source format and falling back to PNG. Leaves mime_type and
// data empty when no encoding succeeds or no provider is available.
// True for a graphic format a consumer outside the office core can decode:
// the web rasters and SVG. The office core's own metafile spellings
// (image/x-vclgraphic, image/x-svm, image/x-wmf, image/x-emf) and anything
// else round-trip through the graphic provider but decode nowhere else, so
// they are re-encoded as PNG rather than shipped verbatim.
bool portable_graphic_mime(const rtl::OUString& mime) {
  static const char* const kPortable[] = {
      "image/png",  "image/jpeg", "image/gif",    "image/bmp",
      "image/tiff", "image/webp", "image/svg+xml"};
  for (const char* candidate : kPortable) {
    if (mime.equalsAscii(candidate)) return true;
  }
  return false;
}

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
    warner.note(label + " mime type query failed", error);
  }
  if (!portable_graphic_mime(mime)) mime = "image/png";
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
    // The office core's GIF writer pads colour tables with uninitialised
    // memory; see gif_palette.h.
    if (*mime_type == "image/gif") grlibre::scrub_unused_gif_palette(data);
  } catch (const css::uno::Exception& original_error) {
    warner.note(label + " does not round-trip as " + utf8(mime) +
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
      warner.note(label + " could not be encoded at all", error);
    }
  }
}

// Shared state of one Writer draw-page walk, threaded through the group
// recursion so image and shape numbering stay document-global.
struct WriterShapeWalk {
  Reference<css::text::XTextViewCursor> cursor;
  CaretSpace* space = nullptr;
  SelectionProbe* probe = nullptr;
  Reference<css::graphic::XGraphicProvider> provider;
  bool want_images = false;
  bool want_shapes = false;
  bool want_form_fields = false;
  MarkerCollector* marks = nullptr;
  int32_t image_index = 0;
  int32_t shape_index = 0;
};

// Sets the shape model's position, converted to twips, on out. Group
// children have no caret anchor, so this is their geometry anchor; for
// anchored top-level shapes it rides along as model geometry.
void fill_shape_position(const Reference<css::drawing::XShape>& shape,
                         const std::string& label, officev1::TwipsPoint* out,
                         Warner& warner) {
  if (!shape.is()) return;
  try {
    css::awt::Point position = shape->getPosition();
    out->set_x(hundredth_mm_to_twips(position.X));
    out->set_y(hundredth_mm_to_twips(position.Y));
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " position query failed", error);
  }
}

// Emits one draw-page form control (a legacy Writer form checkbox, text
// field, or list box) as a FormField with geometry instead of a character
// span. The control's semantic kind comes from its model's form component
// service; unrecognized components still emit with the service name.
void emit_form_control(const Reference<css::beans::XPropertySet>& shape_props,
                       const std::string& label, WriterShapeWalk* walk,
                       Warner& warner) {
  try {
    Reference<css::drawing::XControlShape> control_shape(shape_props, UNO_QUERY);
    if (!control_shape.is()) return;
    Reference<css::awt::XControlModel> model = control_shape->getControl();
    Reference<css::beans::XPropertySet> model_props(model, UNO_QUERY);
    if (!model_props.is()) return;
    officev1::FormField field;
    field.set_control(true);
    field.set_char_start(-1);
    field.set_char_end(-1);
    field.set_selected_index(-1);
    Reference<css::lang::XServiceInfo> services(model, UNO_QUERY);
    if (services.is()) {
      for (const rtl::OUString& service : services->getSupportedServiceNames()) {
        std::string name = utf8(service);
        if (name.starts_with("com.sun.star.form.component.")) {
          field.set_field_type(name);
          break;
        }
      }
    }
    field.set_kind(form_field_kind(field.field_type()));
    Reference<css::beans::XPropertySetInfo> info =
        model_props->getPropertySetInfo();
    auto has_property = [&](const char* name) {
      return info.is() && info->hasPropertyByName(
                              rtl::OUString::createFromAscii(name));
    };
    rtl::OUString text;
    if (has_property("Name")) {
      model_props->getPropertyValue("Name") >>= text;
      field.set_name(utf8(text));
    }
    if (has_property("Label")) {
      text = rtl::OUString();
      model_props->getPropertyValue("Label") >>= text;
      field.set_label(utf8(text));
    }
    if (has_property("Text")) {
      text = rtl::OUString();
      model_props->getPropertyValue("Text") >>= text;
      field.set_text(utf8(text));
    }
    if (has_property("State")) {
      sal_Int16 state = 0;
      model_props->getPropertyValue("State") >>= state;
      field.set_checked(state == 1);
    }
    if (has_property("StringItemList")) {
      css::uno::Sequence<rtl::OUString> entries;
      model_props->getPropertyValue("StringItemList") >>= entries;
      for (const rtl::OUString& entry : entries) {
        field.add_list_entries(utf8(entry));
      }
    }
    if (has_property("SelectedItems")) {
      css::uno::Sequence<sal_Int16> selected;
      model_props->getPropertyValue("SelectedItems") >>= selected;
      if (selected.hasElements()) {
        field.set_selected_index(selected[0]);
        std::string joined;
        for (sal_Int16 item : selected) {
          if (item < 0 || item >= field.list_entries_size()) continue;
          if (!joined.empty()) joined += "\n";
          joined += field.list_entries(item);
        }
        if (!joined.empty()) field.set_text(joined);
      }
    }
    Reference<css::drawing::XShape> shape(shape_props, UNO_QUERY);
    if (shape.is()) {
      css::awt::Size size = shape->getSize();
      field.set_width_twips(hundredth_mm_to_twips(size.Width));
      field.set_height_twips(hundredth_mm_to_twips(size.Height));
      fill_shape_position(shape, label, field.mutable_anchor(), warner);
    }
    field.set_page_index(-1);
    try {
      Reference<css::text::XTextContent> content(shape_props, UNO_QUERY);
      if (content.is()) {
        int32_t page_index = -1;
        caret_at(walk->cursor, content->getAnchor(), label + " anchor",
                 walk->space, field.mutable_anchor(), &page_index, warner);
        field.set_page_index(page_index);
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " anchor query failed", error);
    }
    walk->marks->emit_form_control(std::move(field));
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " form control extraction failed", error);
  }
}

// Walks one shape container of the text document's draw page in paint
// order and classifies each shape: graphic objects emit EmbeddedImage
// (gated on IMAGES), draw-page SwXTextFrame flys are skipped because
// emit_text_frames owns them, group containers emit a Shape event with
// is_group set (gated on SHAPES) and are recursed so grouped content is
// never dropped, and the remaining text-bearing shapes (custom shapes,
// text shapes, imported textboxes whose XText resolves to their hidden
// backing frame) emit Shape events (gated on SHAPES). Control shapes and
// empty-text shapes are skipped silently. group_path names the ancestor
// chain, empty at the top level; groups are recursed even when only IMAGES
// is selected so nested image shapes are found. depth counts the enclosing
// groups; children past kMaxShapeGroupDepth are not descended.
bool emit_writer_shapes(const Reference<css::container::XIndexAccess>& shapes,
                        const std::string& group_path, WriterShapeWalk* walk,
                        const EmitFn& emit_fn, Warner& warner,
                        int depth = 0) {
  for (sal_Int32 i = 0; i < shapes->getCount(); i++) {
    std::string slot = group_path.empty()
        ? std::to_string(i) : group_path + "/" + std::to_string(i);
    std::string label = "shape " + slot;
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

    if (services->supportsService("com.sun.star.drawing.GroupShape")) {
      if (walk->want_shapes) {
        officev1::StreamPagesResponse event;
        officev1::Shape* out = event.mutable_shape();
        out->set_index(walk->shape_index);
        out->set_page_index(-1);
        out->set_z_order(i);
        out->set_group_path(group_path);
        out->set_is_group(true);
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
          fill_shape_position(shape, label, out->mutable_position(), warner);
        }
        // Only a top-level group is a text content with a caret anchor.
        try {
          Reference<css::text::XTextContent> content(props, UNO_QUERY);
          if (content.is()) {
            int32_t page_index = -1;
            caret_at(walk->cursor, content->getAnchor(), label + " anchor",
                     walk->space, out->mutable_anchor(), &page_index, warner);
            out->set_page_index(page_index);
            out->set_in_header_footer(anchored_in_header_footer(
                content->getAnchor(), label + " anchor", warner));
          }
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " anchor query failed", error);
        }
        if (!emit_fn(event)) return false;
        walk->shape_index++;
      }
      Reference<css::container::XIndexAccess> children(props, UNO_QUERY);
      if (children.is() && depth >= kMaxShapeGroupDepth) {
        warner.warn_once("shape groups nested deeper than "
                         + std::to_string(kMaxShapeGroupDepth)
                         + " levels were not descended");
      } else if (children.is()) {
        if (!emit_writer_shapes(children, slot, walk, emit_fn, warner,
                                depth + 1)) {
          return false;
        }
      }
      continue;
    }

    if (services->supportsService("com.sun.star.text.TextGraphicObject") ||
        services->supportsService("com.sun.star.drawing.GraphicObjectShape")) {
      if (!walk->want_images) continue;
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
      out->set_index(walk->image_index);
      out->set_page_index(-1);
      out->set_group_path(group_path);
      Reference<css::container::XNamed> named(props, UNO_QUERY);
      if (named.is()) out->set_name(utf8(named->getName()));
      fill_alt_text(props, out->mutable_title(), out->mutable_description());
      std::string image_label = "image " + std::to_string(walk->image_index);

      encode_graphic(graphic, walk->provider, image_label,
                     out->mutable_mime_type(), out->mutable_data(), warner);

      try {
        Reference<css::text::XTextContent> content(props, UNO_QUERY);
        if (content.is()) {
          int32_t page_index = -1;
          caret_at(walk->cursor, content->getAnchor(), image_label + " anchor",
                   walk->space, out->mutable_anchor(), &page_index, warner);
          out->set_page_index(page_index);
          out->set_in_header_footer(anchored_in_header_footer(
              content->getAnchor(), image_label + " anchor", warner));
          // Best effort: select over the anchor character so an as-char
          // image yields its line box. Floating anchors keep width, height,
          // and anchor as the authoritative geometry.
          if (walk->probe != nullptr && walk->probe->reschedule != nullptr &&
              walk->cursor.is()) {
            Reference<css::text::XTextRange> anchor = content->getAnchor();
            if (anchor.is()) {
              walk->probe->last_payload.clear();
              walk->cursor->gotoRange(anchor->getStart(), false);
              walk->cursor->goRight(1, true);
              walk->probe->flush();
              collect_line_rects(walk->probe, out->mutable_line_rects());
              walk->cursor->gotoRange(anchor->getStart(), false);
              walk->probe->flush();
              walk->probe->last_payload.clear();
            }
          }
        }
        // Grouped graphic shapes are not text contents and carry no
        // LayoutSize property; their model size is the laid-out size.
        Reference<css::beans::XPropertySetInfo> info =
            props->getPropertySetInfo();
        if (info.is() && info->hasPropertyByName("LayoutSize")) {
          css::awt::Size layout_size;
          props->getPropertyValue("LayoutSize") >>= layout_size;
          out->set_width_twips(hundredth_mm_to_twips(layout_size.Width));
          out->set_height_twips(hundredth_mm_to_twips(layout_size.Height));
        } else {
          Reference<css::drawing::XShape> shape(props, UNO_QUERY);
          if (shape.is()) {
            css::awt::Size size = shape->getSize();
            out->set_width_twips(hundredth_mm_to_twips(size.Width));
            out->set_height_twips(hundredth_mm_to_twips(size.Height));
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(image_label + " geometry query failed", error);
      }

      if (!emit_fn(event)) return false;
      walk->image_index++;
      continue;
    }

    // Writer text frames surface on the draw page as SwXTextFrame too;
    // emit_text_frames owns them, so skipping here is the dedup that keeps
    // each frame a single event.
    if (services->supportsService("com.sun.star.text.TextFrame")) continue;
    if (services->supportsService("com.sun.star.drawing.ControlShape")) {
      if (walk->want_form_fields && walk->marks != nullptr) {
        emit_form_control(props, label, walk, warner);
        if (walk->marks->failed()) return false;
      }
      continue;
    }
    if (!walk->want_shapes) continue;
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
    out->set_index(walk->shape_index);
    out->set_page_index(-1);
    out->set_z_order(i);
    out->set_group_path(group_path);
    Reference<css::container::XNamed> named(props, UNO_QUERY);
    if (named.is()) out->set_name(utf8(named->getName()));
    fill_alt_text(props, out->mutable_title(), out->mutable_description());
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
      fill_shape_position(shape, label, out->mutable_position(), warner);
    }
    try {
      Reference<css::text::XTextContent> content(props, UNO_QUERY);
      if (content.is()) {
        int32_t page_index = -1;
        caret_at(walk->cursor, content->getAnchor(), label + " anchor",
                 walk->space, out->mutable_anchor(), &page_index, warner);
        out->set_page_index(page_index);
        out->set_in_header_footer(anchored_in_header_footer(
            content->getAnchor(), label + " anchor", warner));
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
    flatten_text_runs(text, label, out->mutable_runs(), walk->marks, warner);
    if (!emit_fn(event)) return false;
    walk->shape_index++;
  }
  return true;
}

// Entry point of the draw-page walk: resolves the page and the graphic
// provider, then hands the top-level container to the group recursion.
bool emit_draw_shapes(const Reference<css::text::XTextDocument>& text_doc,
                      const Reference<css::uno::XComponentContext>& context,
                      const Reference<css::text::XTextViewCursor>& cursor,
                      CaretSpace* space, const PartSelection& parts,
                      SelectionProbe* probe, MarkerCollector* marks,
                      const EmitFn& emit_fn, Warner& warner) {
  Reference<css::drawing::XDrawPageSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::container::XIndexAccess> shapes(supplier->getDrawPage(), UNO_QUERY);
  if (!shapes.is()) return true;
  WriterShapeWalk walk;
  walk.cursor = cursor;
  walk.space = space;
  walk.probe = probe;
  walk.want_images = parts.wants(officev1::DOCUMENT_PART_IMAGES);
  walk.want_shapes = parts.wants(officev1::DOCUMENT_PART_SHAPES);
  walk.want_form_fields =
      parts.wants(officev1::DOCUMENT_PART_FORM_FIELDS) && marks != nullptr;
  walk.marks = marks;
  if (walk.want_images) walk.provider = graphic_provider(context, warner);
  return emit_writer_shapes(shapes, "", &walk, emit_fn, warner);
}

// Emits every Writer text frame with its runs, chain names, layout anchor,
// and laid-out geometry. Textbox-backing hidden frames are not in this
// enumeration (the office core excludes them); their text arrives through
// the draw-shape pass instead, so the two passes never overlap. A table in
// a frame (a Word floating table imports as a frame holding just the table)
// is no paragraph of the frame's text: it streams as a table of its own
// after the frame, numbered on from *table_index, when table_parts is
// non-null (the tables part is selected).
bool emit_text_frames(const Reference<css::text::XTextDocument>& text_doc,
                      const Reference<css::text::XTextViewCursor>& cursor,
                      CaretSpace* space, MarkerCollector* marks,
                      const PartSelection* table_parts, SelectionProbe* probe,
                      int32_t* table_index, const EmitFn& emit_fn,
                      Warner& warner) {
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
        caret_at(cursor, content->getAnchor(), label + " anchor", space,
                 out->mutable_anchor(), &page_index, warner);
        out->set_page_index(page_index);
        out->set_in_header_footer(anchored_in_header_footer(
            content->getAnchor(), label + " anchor", warner));
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
      flatten_text_runs(frame->getText(), label, out->mutable_runs(), marks,
                        warner);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " extraction failed", error);
    }
    if (!emit_fn(event)) return false;
    if (table_parts == nullptr) continue;
    std::vector<Reference<css::text::XTextTable>> tables;
    try {
      Reference<css::text::XTextFrame> frame(frames->getByIndex(i), UNO_QUERY);
      Reference<css::container::XEnumerationAccess> access(
          frame.is() ? frame->getText() : Reference<css::text::XText>(),
          UNO_QUERY);
      if (access.is()) {
        Reference<css::container::XEnumeration> elements =
            access->createEnumeration();
        while (elements->hasMoreElements()) {
          Reference<css::text::XTextTable> table(elements->nextElement(),
                                                 UNO_QUERY);
          if (table.is()) tables.push_back(table);
        }
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " table walk failed", error);
    }
    for (const Reference<css::text::XTextTable>& table : tables) {
      if (!emit_table(table, *table_index, cursor, space, *table_parts, probe,
                      emit_fn, warner)) {
        return false;
      }
      (*table_index)++;
    }
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
// are found. depth counts the enclosing groups; children past
// kMaxShapeGroupDepth are not descended.
bool emit_shapes(const Reference<css::drawing::XShapes>& shapes,
                 int32_t page_index, const std::string& group_path,
                 const Reference<css::graphic::XGraphicProvider>& provider,
                 int32_t* image_counter, const PartSelection& parts,
                 const EmitFn& emit_fn, Warner& warner, int depth = 0) {
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
    fill_alt_text(props, out->mutable_title(), out->mutable_description());
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
        flatten_text_runs(text, label, out->mutable_runs(), nullptr, warner);
      }
    }
    Reference<css::drawing::XShapes> children;
    if (shape_type == "com.sun.star.drawing.GroupShape") {
      children = Reference<css::drawing::XShapes>(shape, UNO_QUERY);
    }
    out->set_is_group(children.is());
    if (want_shapes && !emit_fn(event)) return false;
    if (children.is()) {
      if (depth >= kMaxShapeGroupDepth) {
        warner.warn_once("shape groups nested deeper than "
                         + std::to_string(kMaxShapeGroupDepth)
                         + " levels were not descended");
      } else if (!emit_shapes(children, page_index, shape_path, provider,
                              image_counter, parts, emit_fn, warner,
                              depth + 1)) {
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
        image->set_title(out->title());
        image->set_description(out->description());
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

// Maps the office core's shape type string to the placeholder role. The
// type string is the reliable discriminator: SdXShape does not advertise
// SubtitleShape or NotesShape through supportsService.
officev1::PlaceholderRole placeholder_role_for(const std::string& shape_type) {
  if (shape_type.ends_with(".TitleTextShape")) {
    return officev1::PLACEHOLDER_ROLE_TITLE;
  }
  if (shape_type.ends_with(".OutlinerShape")) {
    return officev1::PLACEHOLDER_ROLE_OUTLINE;
  }
  if (shape_type.ends_with(".SubtitleShape")) {
    return officev1::PLACEHOLDER_ROLE_SUBTITLE;
  }
  if (shape_type.ends_with(".NotesShape")) {
    return officev1::PLACEHOLDER_ROLE_NOTES;
  }
  return officev1::PLACEHOLDER_ROLE_NONE;
}

// Emits one SlideShape for a slide or notes-page shape and recurses through
// groups so nested placeholders keep their paint order. Graphic, OLE, chart,
// and table shapes emit only this header; their heavy content (image bytes,
// chart data, table grid) belongs to the embedded-objects work.
// "A1" for row 0, column 0; the office core's own cell naming, rebuilt here
// because a drawing table's cells are addressed by position only.
std::string cell_name_for(int32_t row, int32_t column) {
  std::string name;
  for (int32_t c = column; c >= 0; c = c / 26 - 1) {
    name.insert(name.begin(), static_cast<char>('A' + c % 26));
  }
  return name + std::to_string(row + 1);
}

// Fills a table shape's cell grid from its table model. A drawing table
// exposes its content through XTable rather than through the shape's own
// text interface, which is why a slide table's content is invisible to a
// text walk. Returns false when the shape is not a table shape.
bool fill_slide_table(const Reference<css::beans::XPropertySet>& props,
                      const std::string& label, officev1::TableData* out,
                      Warner& warner) {
  if (!props.is()) return false;
  Reference<css::table::XTable> model;
  try {
    props->getPropertyValue("Model") >>= model;
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: only a table shape carries a table model.
    return false;
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " table model query failed", error);
    return false;
  }
  if (!model.is()) return false;
  out->set_index(-1);
  out->set_page_index(-1);
  try {
    int32_t rows = static_cast<int32_t>(model->getRowCount());
    int32_t columns = static_cast<int32_t>(model->getColumnCount());
    out->set_rows(rows);
    out->set_columns(columns);
    for (int32_t r = 0; r < rows; r++) {
      for (int32_t c = 0; c < columns; c++) {
        Reference<css::table::XCell> cell = model->getCellByPosition(c, r);
        if (!cell.is()) continue;
        Reference<css::table::XMergeableCell> merge(cell, UNO_QUERY);
        // A covered cell repeats its anchor's content; only the anchor is
        // emitted, and it carries the whole block's spans.
        if (merge.is() && merge->isMerged()) continue;
        officev1::TableCellData* out_cell = out->add_cells();
        out_cell->set_row(r);
        out_cell->set_column(c);
        out_cell->set_name(cell_name_for(r, c));
        out_cell->set_row_span(
            merge.is() ? std::max<int32_t>(1, merge->getRowSpan()) : 1);
        out_cell->set_column_span(
            merge.is() ? std::max<int32_t>(1, merge->getColumnSpan()) : 1);
        Reference<css::text::XText> text(cell, UNO_QUERY);
        if (text.is()) out_cell->set_text(utf8(text->getString()));
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " table walk failed", error);
  }
  return true;
}

bool emit_slide_shape(const Reference<css::drawing::XShape>& shape,
                      int32_t slide_index, int32_t z_order, bool notes,
                      bool want_slides,
                      const Reference<css::graphic::XGraphicProvider>& provider,
                      int32_t* image_counter, const EmitFn& emit_fn,
                      Warner& warner, int depth = 0) {
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
  fill_alt_text(props, out->mutable_title(), out->mutable_description());
  if (shape_type.ends_with(".TableShape")) {
    fill_slide_table(props, label, out->mutable_table(), warner);
  }
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
                para->mutable_runs(), nullptr, nullptr, warner);
      paragraph_index++;
    }
  }
  // An images-only selection still walks the shapes, to find the pictures
  // inside them, but emits no shape events of its own.
  if (want_slides && !emit_fn(event)) return false;

  // A slide picture reaches the consumer as bytes through the same
  // EmbeddedImage event the text and drawing walks emit; without it a deck's
  // pictures arrive as empty placeholders.
  if (image_counter != nullptr && props.is() &&
      shape_type.ends_with(".GraphicObjectShape")) {
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
      // Slide geometry is page-local, and the slide index is the page.
      image->set_page_index(notes ? -1 : slide_index);
      Reference<css::container::XNamed> named(shape, UNO_QUERY);
      if (named.is()) image->set_name(utf8(named->getName()));
      image->set_title(out->title());
      image->set_description(out->description());
      *image->mutable_anchor() = out->position();
      image->set_width_twips(out->width_twips());
      image->set_height_twips(out->height_twips());
      encode_graphic(graphic, provider,
                     "image " + std::to_string(image->index()),
                     image->mutable_mime_type(), image->mutable_data(),
                     warner);
      if (!emit_fn(image_event)) return false;
    }
  }

  Reference<css::drawing::XShapes> children;
  if (shape_type.ends_with(".GroupShape")) {
    children = Reference<css::drawing::XShapes>(shape, UNO_QUERY);
  }
  if (children.is() && depth >= kMaxShapeGroupDepth) {
    warner.warn_once("shape groups nested deeper than "
                     + std::to_string(kMaxShapeGroupDepth)
                     + " levels were not descended");
    children.clear();
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
                            want_slides, provider, image_counter, emit_fn,
                            warner, depth + 1)) {
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
// Emits every annotation of one slide as a Comment event with the slide's
// index as its page and the annotation's slide-local position as its
// anchor.
bool emit_slide_annotations(const Reference<css::drawing::XDrawPage>& slide,
                            int32_t slide_index, int32_t* comment_index,
                            const EmitFn& emit_fn, Warner& warner) {
  std::string label = "slide " + std::to_string(slide_index) + " annotations";
  try {
    Reference<css::office::XAnnotationAccess> access(slide, UNO_QUERY);
    if (!access.is()) return true;
    Reference<css::office::XAnnotationEnumeration> annotations =
        access->createAnnotationEnumeration();
    if (!annotations.is()) return true;
    while (annotations->hasMoreElements()) {
      Reference<css::office::XAnnotation> annotation =
          annotations->nextElement();
      if (!annotation.is()) continue;
      officev1::StreamPagesResponse event;
      officev1::Comment* out = event.mutable_comment();
      out->set_index((*comment_index)++);
      out->set_page_index(slide_index);
      out->set_char_start(-1);
      out->set_char_end(-1);
      out->set_author(utf8(annotation->getAuthor()));
      out->set_initials(utf8(annotation->getInitials()));
      out->set_epoch_ms(datetime_epoch_ms(annotation->getDateTime()));
      css::geometry::RealPoint2D position = annotation->getPosition();
      // Annotation positions are in millimeters (the office core's
      // RealPoint2D convention for annotations), converted to twips like
      // every other coordinate: 1 mm = 100 hundredths.
      out->mutable_anchor()->set_x(
          hundredth_mm_to_twips(static_cast<long>(position.X * 100.0)));
      out->mutable_anchor()->set_y(
          hundredth_mm_to_twips(static_cast<long>(position.Y * 100.0)));
      Reference<css::text::XText> text(annotation->getTextRange());
      if (text.is()) out->set_text(utf8(text->getString()));
      if (!emit_fn(event)) return false;
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " walk failed", error);
  }
  return true;
}

bool emit_presentation_content(
    const Reference<css::drawing::XDrawPagesSupplier>& supplier,
    const Reference<css::uno::XComponentContext>& context,
    const PartSelection& parts, const EmitFn& emit_fn, Warner& warner) {
  int32_t comment_index = 0;
  int32_t image_index = 0;
  // Slide pictures are gated on the images part like every other picture
  // walk; a null counter switches the walk's image emission off entirely.
  bool want_images = parts.wants(officev1::DOCUMENT_PART_IMAGES);
  Reference<css::graphic::XGraphicProvider> provider;
  if (want_images) provider = graphic_provider(context, warner);
  int32_t* image_counter = want_images ? &image_index : nullptr;
  bool want_slides = parts.wants(officev1::DOCUMENT_PART_SLIDES);
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

    // A selection asking for neither slides nor their pictures walks just
    // the annotations.
    if (!want_slides && !want_images) {
      if (!emit_slide_annotations(slide, static_cast<int32_t>(i),
                                  &comment_index, emit_fn, warner)) {
        return false;
      }
      continue;
    }

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
    if (want_slides && !emit_fn(slide_event)) return false;

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
                              static_cast<int32_t>(z), false, want_slides,
                              provider, image_counter, emit_fn, warner)) {
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
            if (!utf8(shape->getShapeType()).ends_with(".NotesShape")) {
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
                                  static_cast<int32_t>(z), true, want_slides,
                                  provider, image_counter, emit_fn, warner)) {
              return false;
            }
          }
        }
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " notes page walk failed", error);
    }

    if (parts.wants(officev1::DOCUMENT_PART_COMMENTS)) {
      if (!emit_slide_annotations(slide, static_cast<int32_t>(i),
                                  &comment_index, emit_fn, warner)) {
        return false;
      }
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
  // One entry per number-format key: its code string and its category
  // flags, both resolved on first sight. The category is what tells a date
  // serial from a plain quantity and a logical from the number 1.
  struct NumberFormatInfo {
    std::string code;
    sal_Int16 category = 0;
  };
  std::map<sal_Int32, NumberFormatInfo> format_cache;
  auto format_info = [&](sal_Int32 key) -> const NumberFormatInfo& {
    static const NumberFormatInfo kGeneral;
    if (key == 0 || !formats.is()) return kGeneral;
    if (auto found = format_cache.find(key); found != format_cache.end()) {
      return found->second;
    }
    NumberFormatInfo info;
    try {
      Reference<css::beans::XPropertySet> props = formats->getByKey(key);
      if (props.is()) {
        rtl::OUString text;
        props->getPropertyValue("FormatString") >>= text;
        info.code = utf8(text);
        props->getPropertyValue("Type") >>= info.category;
      }
    } catch (const css::uno::Exception& error) {
      warner.warn("number format " + std::to_string(key) + " query failed",
                  error);
    }
    return format_cache.emplace(key, std::move(info)).first->second;
  };

  // Serial dates count days from the document's own null date, 1899-12-30
  // unless the document says otherwise, so the epoch conversion has to ask.
  css::util::Date null_date;
  null_date.Year = 1899;
  null_date.Month = 12;
  null_date.Day = 30;
  if (want_sheets) {
    try {
      Reference<css::beans::XPropertySet> doc_props(model, UNO_QUERY);
      if (doc_props.is()) doc_props->getPropertyValue("NullDate") >>= null_date;
    } catch (const css::uno::Exception& error) {
      warner.warn("null date query failed, serial dates assume 1899-12-30",
                  error);
    }
  }
  const int64_t null_date_epoch_ms = date_midnight_epoch_ms(null_date);

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
            out->set_sheet_index(-1);
            // A name that refers to cells resolves to them; a name holding
            // an expression does not, and keeps only its content string.
            try {
              Reference<css::sheet::XCellRangeReferrer> referrer(range,
                                                                 UNO_QUERY);
              Reference<css::sheet::XCellRangeAddressable> address;
              if (referrer.is()) {
                address = Reference<css::sheet::XCellRangeAddressable>(
                    referrer->getReferredCells(), UNO_QUERY);
              }
              if (address.is()) {
                css::table::CellRangeAddress cells = address->getRangeAddress();
                fill_range_ref(cells, out->mutable_range());
                out->set_sheet_index(cells.Sheet);
              }
            } catch (const css::uno::Exception& error) {
              warner.warn("named range " + out->name() +
                              " does not resolve to cells",
                          error);
            }
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

    // Database ranges live next to the named ranges on the model. Each one
    // is a DatabaseRange service: XDatabaseRange for the data area, XNamed
    // for the name, and optional boolean properties for the header, totals,
    // and filter settings.
    try {
      Reference<css::beans::XPropertySet> doc_props(model, UNO_QUERY);
      if (doc_props.is()) {
        Reference<css::container::XIndexAccess> ranges;
        doc_props->getPropertyValue("DatabaseRanges") >>= ranges;
        if (ranges.is()) {
          for (sal_Int32 i = 0; i < ranges->getCount(); i++) {
            css::uno::Any entry = ranges->getByIndex(i);
            Reference<css::sheet::XDatabaseRange> range(entry, UNO_QUERY);
            if (!range.is()) continue;
            officev1::StreamPagesResponse event;
            officev1::SheetDatabaseRange* out =
                event.mutable_sheet_database_range();
            Reference<css::container::XNamed> named(entry, UNO_QUERY);
            if (named.is()) out->set_name(utf8(named->getName()));
            css::table::CellRangeAddress area = range->getDataArea();
            out->set_sheet_index(area.Sheet);
            fill_range_ref(area, out->mutable_range());
            Reference<css::beans::XPropertySet> props(entry, UNO_QUERY);
            if (props.is()) {
              auto flag = [&](const char* name, bool* value) {
                try {
                  sal_Bool raw = false;
                  if (props->getPropertyValue(
                          rtl::OUString::createFromAscii(name)) >>= raw) {
                    *value = raw;
                  }
                } catch (const css::beans::UnknownPropertyException&) {
                  // Expected probe result: these are optional properties of
                  // the DatabaseRange service.
                }
              };
              bool contains_header = false;
              bool totals_row = false;
              bool auto_filter = false;
              flag("ContainsHeader", &contains_header);
              flag("TotalsRow", &totals_row);
              flag("AutoFilter", &auto_filter);
              out->set_contains_header(contains_header);
              out->set_totals_row(totals_row);
              out->set_auto_filter(auto_filter);
            }
            if (!emit_fn(event)) return false;
          }
        }
      }
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: DatabaseRanges is a service property that a
      // minimal model may not carry.
    } catch (const css::uno::Exception& error) {
      warner.warn("database ranges query failed", error);
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
      try {
        Reference<css::table::XColumnRowRange> grid(sheet, UNO_QUERY);
        if (grid.is()) {
          Reference<css::table::XTableColumns> columns = grid->getColumns();
          if (columns.is()) {
            sal_Int32 last = std::min<sal_Int32>(used.EndColumn,
                                                 columns->getCount() - 1);
            for (sal_Int32 c = 0; c <= last; c++) {
              Reference<css::beans::XPropertySet> column_props(
                  columns->getByIndex(c), UNO_QUERY);
              sal_Int32 width = 0;
              if (column_props.is()) {
                column_props->getPropertyValue("Width") >>= width;
              }
              out->add_column_widths_twips(
                  static_cast<int32_t>(hundredth_mm_to_twips(width)));
            }
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " column widths query failed", error);
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
            const NumberFormatInfo& format = format_info(key);
            out_cell->set_number_format_string(format.code);
            if ((format.category & css::util::NumberFormat::LOGICAL) != 0) {
              out_cell->set_is_boolean(true);
            } else if ((format.category & css::util::NumberFormat::DATETIME)
                       != 0) {
              // DATETIME is DATE|TIME, so the mask catches all three. The
              // serial counts days from the null date; the wall-clock
              // components fall out of that sum with no timezone claimed.
              out_cell->set_is_datetime(true);
              fill_cell_datetime(
                  null_date_epoch_ms +
                      static_cast<int64_t>(
                          std::llround(cell->getValue() * 86400000.0)),
                  out_cell->mutable_datetime());
            }
            if (type == css::table::CellContentType_FORMULA) {
              out_cell->set_error_code(cell->getError());
            }
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

// Formats a double for the tabular chart projection. The typed series keep
// the values numeric; this text is only the grid projection.
std::string double_text(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%g", value);
  return buffer;
}

// Concatenates a chart title's formatted string parts.
std::string chart_title_text(const Reference<css::chart2::XTitled>& titled) {
  std::string text;
  if (!titled.is()) return text;
  Reference<css::chart2::XTitle> title = titled->getTitleObject();
  if (!title.is()) return text;
  for (const Reference<css::chart2::XFormattedString>& part : title->getText()) {
    if (part.is()) text += utf8(part->getString());
  }
  return text;
}

// Maps the raw chart2 chart-type service name to the coarse wire kind.
officev1::EmbeddedChartKind chart_kind_for(const std::string& service) {
  if (service.ends_with(".BarChartType")) return officev1::EMBEDDED_CHART_KIND_BAR;
  if (service.ends_with(".ColumnChartType")) {
    return officev1::EMBEDDED_CHART_KIND_COLUMN;
  }
  if (service.ends_with(".LineChartType")) return officev1::EMBEDDED_CHART_KIND_LINE;
  if (service.ends_with(".AreaChartType")) return officev1::EMBEDDED_CHART_KIND_AREA;
  if (service.ends_with(".PieChartType")) return officev1::EMBEDDED_CHART_KIND_PIE;
  if (service.ends_with(".ScatterChartType")) {
    return officev1::EMBEDDED_CHART_KIND_SCATTER;
  }
  if (service.ends_with(".BubbleChartType")) {
    return officev1::EMBEDDED_CHART_KIND_BUBBLE;
  }
  if (service.ends_with(".NetChartType")) return officev1::EMBEDDED_CHART_KIND_NET;
  if (service.ends_with(".CandleStickChartType")) {
    return officev1::EMBEDDED_CHART_KIND_CANDLESTICK;
  }
  return officev1::EMBEDDED_CHART_KIND_OTHER;
}

// Walks an embedded chart's live chart2 model: type, titles, category axis,
// and per-series numeric sequences routed by their Role property, then a
// tabular grid projection that is always populated.
void fill_chart(const Reference<css::chart2::XChartDocument>& chart_doc,
                const std::string& label, officev1::EmbeddedChart* out,
                Warner& warner) {
  try {
    out->set_title(
        chart_title_text(Reference<css::chart2::XTitled>(chart_doc, UNO_QUERY)));
    Reference<css::chart2::XDiagram> diagram = chart_doc->getFirstDiagram();
    if (!diagram.is()) return;
    Reference<css::chart2::XCoordinateSystemContainer> systems(diagram,
                                                               UNO_QUERY);
    if (!systems.is()) return;
    for (const Reference<css::chart2::XCoordinateSystem>& coord :
         systems->getCoordinateSystems()) {
      if (!coord.is()) continue;
      // Axis titles and the category labels, which live on the axis scale,
      // not on the series.
      for (sal_Int32 dimension = 0;
           dimension < std::min<sal_Int32>(2, coord->getDimension());
           dimension++) {
        try {
          Reference<css::chart2::XAxis> axis =
              coord->getAxisByDimension(dimension, 0);
          if (!axis.is()) continue;
          std::string title = chart_title_text(
              Reference<css::chart2::XTitled>(axis, UNO_QUERY));
          if (dimension == 0 && out->x_axis_title().empty()) {
            out->set_x_axis_title(title);
          }
          if (dimension == 1 && out->y_axis_title().empty()) {
            out->set_y_axis_title(title);
          }
          if (dimension == 0 && out->categories().empty()) {
            css::chart2::ScaleData scale = axis->getScaleData();
            if (scale.Categories.is()) {
              Reference<css::chart2::data::XTextualDataSequence> texts(
                  scale.Categories->getValues(), UNO_QUERY);
              if (texts.is()) {
                for (const rtl::OUString& category : texts->getTextualData()) {
                  out->add_categories(utf8(category));
                }
              }
            }
          }
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " axis " + std::to_string(dimension) +
                          " query failed",
                      error);
        }
      }
      Reference<css::chart2::XChartTypeContainer> types(coord, UNO_QUERY);
      if (!types.is()) continue;
      for (const Reference<css::chart2::XChartType>& type :
           types->getChartTypes()) {
        if (!type.is()) continue;
        if (out->chart_type_service().empty()) {
          std::string service = utf8(type->getChartType());
          out->set_chart_type_service(service);
          out->set_kind(chart_kind_for(service));
        }
        Reference<css::chart2::XDataSeriesContainer> series_container(type,
                                                                      UNO_QUERY);
        if (!series_container.is()) continue;
        for (const Reference<css::chart2::XDataSeries>& series :
             series_container->getDataSeries()) {
          Reference<css::chart2::data::XDataSource> source(series, UNO_QUERY);
          if (!source.is()) continue;
          officev1::EmbeddedChartSeries* out_series = out->add_series();
          for (const Reference<css::chart2::data::XLabeledDataSequence>&
                   labeled : source->getDataSequences()) {
            if (!labeled.is()) continue;
            if (out_series->label().empty()) {
              Reference<css::chart2::data::XTextualDataSequence> label_text(
                  labeled->getLabel(), UNO_QUERY);
              if (label_text.is()) {
                for (const rtl::OUString& part : label_text->getTextualData()) {
                  out_series->set_label(out_series->label() + utf8(part));
                }
              }
            }
            Reference<css::chart2::data::XDataSequence> values =
                labeled->getValues();
            if (!values.is()) continue;
            rtl::OUString role;
            try {
              Reference<css::beans::XPropertySet> value_props(values,
                                                              UNO_QUERY);
              if (value_props.is()) {
                value_props->getPropertyValue("Role") >>= role;
              }
            } catch (const css::beans::UnknownPropertyException&) {
              // Expected probe result: a bare data sequence carries no role.
            }
            Reference<css::chart2::data::XNumericalDataSequence> numbers(
                values, UNO_QUERY);
            if (!numbers.is()) continue;
            google::protobuf::RepeatedField<double>* target = nullptr;
            if (role == "values-x") {
              target = out_series->mutable_values_x();
            } else if (role == "sizes") {
              target = out_series->mutable_sizes();
            } else {
              // values-y and unlabeled numeric sequences are the y series.
              target = out_series->mutable_values_y();
            }
            for (double value : numbers->getNumericalData()) {
              target->Add(value);
            }
          }
        }
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " chart walk failed", error);
  }
  // The tabular projection: a label row on top, categories down the first
  // column, one series per further column.
  officev1::TableData* grid = out->mutable_tabular();
  int rows = out->categories_size();
  for (const officev1::EmbeddedChartSeries& series : out->series()) {
    rows = std::max(rows, series.values_y_size());
  }
  grid->set_rows(rows + 1);
  grid->set_columns(out->series_size() + 1);
  for (int column = 0; column < out->series_size(); column++) {
    officev1::TableCellData* cell = grid->add_cells();
    cell->set_row(0);
    cell->set_column(column + 1);
    cell->set_text(out->series(column).label());
  }
  for (int row = 0; row < rows; row++) {
    officev1::TableCellData* head = grid->add_cells();
    head->set_row(row + 1);
    head->set_column(0);
    if (row < out->categories_size()) head->set_text(out->categories(row));
    for (int column = 0; column < out->series_size(); column++) {
      const officev1::EmbeddedChartSeries& series = out->series(column);
      if (row >= series.values_y_size()) continue;
      officev1::TableCellData* cell = grid->add_cells();
      cell->set_row(row + 1);
      cell->set_column(column + 1);
      cell->set_text(double_text(series.values_y(row)));
    }
  }
}

// Projects an embedded spreadsheet's first non-empty sheet used range into
// a plain cell grid.
void fill_inner_table(
    const Reference<css::sheet::XSpreadsheetDocument>& inner_doc,
    const std::string& label, officev1::TableData* out, Warner& warner) {
  try {
    Reference<css::container::XIndexAccess> sheets(inner_doc->getSheets(),
                                                   UNO_QUERY);
    if (!sheets.is()) return;
    for (sal_Int32 s = 0; s < sheets->getCount(); s++) {
      Reference<css::sheet::XSpreadsheet> sheet(sheets->getByIndex(s),
                                                UNO_QUERY);
      if (!sheet.is()) continue;
      Reference<css::sheet::XSheetCellCursor> cursor = sheet->createCursor();
      Reference<css::sheet::XUsedAreaCursor> area(cursor, UNO_QUERY);
      Reference<css::sheet::XCellRangeAddressable> addressable(cursor,
                                                               UNO_QUERY);
      if (!area.is() || !addressable.is()) continue;
      area->gotoStartOfUsedArea(false);
      area->gotoEndOfUsedArea(true);
      css::table::CellRangeAddress used = addressable->getRangeAddress();
      bool any = false;
      for (sal_Int32 r = used.StartRow; r <= used.EndRow; r++) {
        for (sal_Int32 c = used.StartColumn; c <= used.EndColumn; c++) {
          Reference<css::table::XCell> cell = sheet->getCellByPosition(c, r);
          if (!cell.is() ||
              cell->getType() == css::table::CellContentType_EMPTY) {
            continue;
          }
          Reference<css::text::XText> text(cell, UNO_QUERY);
          officev1::TableCellData* out_cell = out->add_cells();
          out_cell->set_row(r - used.StartRow);
          out_cell->set_column(c - used.StartColumn);
          if (text.is()) out_cell->set_text(utf8(text->getString()));
          any = true;
        }
      }
      if (any) {
        out->set_rows(used.EndRow - used.StartRow + 1);
        out->set_columns(used.EndColumn - used.StartColumn + 1);
        return;  // The first non-empty sheet is the projection.
      }
      out->clear_cells();
    }
  } catch (const css::uno::Exception& error) {
    warner.warn(label + " inner table walk failed", error);
  }
}

// Classifies an embedded object's inner model and fills its typed content.
void classify_embedded(const Reference<css::frame::XModel>& inner,
                       const std::string& label, officev1::EmbeddedObject* out,
                       Warner& warner) {
  if (!inner.is()) {
    // No inner Office model: a foreign OLE payload.
    out->set_kind(officev1::EMBEDDED_OBJECT_KIND_OLE_OTHER);
    return;
  }
  Reference<css::chart2::XChartDocument> chart(inner, UNO_QUERY);
  if (chart.is()) {
    out->set_kind(officev1::EMBEDDED_OBJECT_KIND_CHART);
    fill_chart(chart, label, out->mutable_chart(), warner);
    return;
  }
  Reference<css::sheet::XSpreadsheetDocument> inner_sheet(inner, UNO_QUERY);
  if (inner_sheet.is()) {
    out->set_kind(officev1::EMBEDDED_OBJECT_KIND_SPREADSHEET);
    fill_inner_table(inner_sheet, label, out->mutable_inner_table(), warner);
    return;
  }
  Reference<css::lang::XServiceInfo> info(inner, UNO_QUERY);
  if (info.is() &&
      info->supportsService("com.sun.star.formula.FormulaProperties")) {
    out->set_kind(officev1::EMBEDDED_OBJECT_KIND_FORMULA);
    try {
      Reference<css::beans::XPropertySet> props(inner, UNO_QUERY);
      if (props.is()) {
        rtl::OUString formula;
        props->getPropertyValue("Formula") >>= formula;
        out->set_formula(utf8(formula));
      }
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " formula query failed", error);
    }
    return;
  }
  out->set_kind(officev1::EMBEDDED_OBJECT_KIND_OLE_OTHER);
}

// Emits every embedded object of the loaded document as a typed
// EmbeddedObject event: Writer objects through the text-embedded-objects
// collection with caret anchors, other document classes through their draw
// pages' OLE2 shapes with twips geometry. The inner content is classified
// off the already-instantiated Model property, which never forces the
// object into a running state.
bool emit_embedded_objects(const Reference<css::frame::XModel>& model,
                           const Reference<css::uno::XComponentContext>& context,
                           const std::vector<PageBox>* page_boxes,
                           const EmitFn& emit_fn, Warner& warner) {
  Reference<css::graphic::XGraphicProvider> provider =
      graphic_provider(context, warner);
  int32_t emitted = 0;

  Reference<css::text::XTextDocument> text_doc(model, UNO_QUERY);
  if (text_doc.is()) {
    Reference<css::text::XTextEmbeddedObjectsSupplier> supplier(text_doc,
                                                                UNO_QUERY);
    if (!supplier.is()) return true;
    Reference<css::container::XIndexAccess> objects(
        supplier->getEmbeddedObjects(), UNO_QUERY);
    if (!objects.is()) return true;
    Reference<css::text::XTextViewCursor> cursor;
    Reference<css::text::XTextViewCursorSupplier> cursor_supplier(
        model->getCurrentController(), UNO_QUERY);
    if (cursor_supplier.is()) cursor = cursor_supplier->getViewCursor();
    CaretSpace caret_space(model, page_boxes);
    CaretSpace* space = &caret_space;
    for (sal_Int32 i = 0; i < objects->getCount(); i++) {
      std::string label = "embedded object " + std::to_string(i);
      officev1::StreamPagesResponse event;
      officev1::EmbeddedObject* out = event.mutable_embedded_object();
      out->set_index(emitted);
      out->set_page_index(-1);
      try {
        Reference<css::beans::XPropertySet> props(objects->getByIndex(i),
                                                  UNO_QUERY);
        if (!props.is()) continue;
        Reference<css::container::XNamed> named(props, UNO_QUERY);
        if (named.is()) out->set_name(utf8(named->getName()));
        try {
          rtl::OUString clsid;
          props->getPropertyValue("CLSID") >>= clsid;
          out->set_clsid(utf8(clsid));
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: not every entry stores a class id.
        }
        try {
          sal_Int32 width = 0, height = 0;
          props->getPropertyValue("Width") >>= width;
          props->getPropertyValue("Height") >>= height;
          out->set_width_twips(hundredth_mm_to_twips(width));
          out->set_height_twips(hundredth_mm_to_twips(height));
        } catch (const css::uno::Exception& error) {
          warner.warn(label + " geometry query failed", error);
        }
        Reference<css::text::XTextContent> content(props, UNO_QUERY);
        if (content.is()) {
          int32_t page_index = -1;
          caret_at(cursor, content->getAnchor(), label + " anchor", space,
                   out->mutable_anchor(), &page_index, warner);
          out->set_page_index(page_index);
          out->set_in_header_footer(anchored_in_header_footer(
              content->getAnchor(), label + " anchor", warner));
        }
        Reference<css::frame::XModel> inner;
        try {
          props->getPropertyValue("Model") >>= inner;
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: a foreign OLE entry has no inner model.
        }
        classify_embedded(inner, label, out, warner);
        Reference<css::document::XEmbeddedObjectSupplier2> replacement(props,
                                                                       UNO_QUERY);
        if (replacement.is()) {
          Reference<css::graphic::XGraphic> graphic =
              replacement->getReplacementGraphic();
          if (graphic.is()) {
            encode_graphic(graphic, provider, label,
                           out->mutable_replacement_mime_type(),
                           out->mutable_replacement_image(), warner);
          }
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " extraction failed", error);
      }
      if (!emit_fn(event)) return false;
      emitted++;
    }
    return true;
  }

  Reference<css::drawing::XDrawPagesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) return true;
  Reference<css::drawing::XDrawPages> pages = supplier->getDrawPages();
  if (!pages.is()) return true;
  for (sal_Int32 p = 0; p < pages->getCount(); p++) {
    Reference<css::container::XIndexAccess> shapes(pages->getByIndex(p),
                                                   UNO_QUERY);
    if (!shapes.is()) continue;
    for (sal_Int32 i = 0; i < shapes->getCount(); i++) {
      std::string label = "page " + std::to_string(p) + " embedded object " +
                          std::to_string(i);
      Reference<css::beans::XPropertySet> props;
      try {
        props = Reference<css::beans::XPropertySet>(shapes->getByIndex(i),
                                                    UNO_QUERY);
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " is not reachable", error);
        continue;
      }
      if (!props.is()) continue;
      Reference<css::lang::XServiceInfo> services(props, UNO_QUERY);
      if (!services.is() ||
          !services->supportsService("com.sun.star.drawing.OLE2Shape")) {
        continue;
      }
      officev1::StreamPagesResponse event;
      officev1::EmbeddedObject* out = event.mutable_embedded_object();
      out->set_index(emitted);
      out->set_page_index(static_cast<int32_t>(p));
      try {
        Reference<css::container::XNamed> named(props, UNO_QUERY);
        if (named.is()) out->set_name(utf8(named->getName()));
        try {
          rtl::OUString clsid;
          props->getPropertyValue("CLSID") >>= clsid;
          out->set_clsid(utf8(clsid));
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: not every shape stores a class id.
        }
        Reference<css::drawing::XShape> shape(props, UNO_QUERY);
        if (shape.is()) {
          css::awt::Point position = shape->getPosition();
          css::awt::Size size = shape->getSize();
          out->mutable_position()->set_x(hundredth_mm_to_twips(position.X));
          out->mutable_position()->set_y(hundredth_mm_to_twips(position.Y));
          out->set_width_twips(hundredth_mm_to_twips(size.Width));
          out->set_height_twips(hundredth_mm_to_twips(size.Height));
        }
        Reference<css::frame::XModel> inner;
        try {
          props->getPropertyValue("Model") >>= inner;
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: a foreign OLE shape has no inner model.
        }
        classify_embedded(inner, label, out, warner);
        // Draw-page OLE shapes have no ReplacementGraphic property; the
        // thumbnail (or graphic) is the replacement image.
        Reference<css::graphic::XGraphic> graphic;
        try {
          props->getPropertyValue("ThumbnailGraphic") >>= graphic;
        } catch (const css::beans::UnknownPropertyException&) {
          // Expected probe result: no thumbnail stored.
        }
        if (!graphic.is()) {
          try {
            props->getPropertyValue("Graphic") >>= graphic;
          } catch (const css::beans::UnknownPropertyException&) {
            // Expected probe result: no replacement graphic at all.
          }
        }
        if (graphic.is()) {
          encode_graphic(graphic, provider, label,
                         out->mutable_replacement_mime_type(),
                         out->mutable_replacement_image(), warner);
        }
      } catch (const css::uno::Exception& error) {
        warner.warn(label + " extraction failed", error);
      }
      if (!emit_fn(event)) return false;
      emitted++;
    }
  }
  return true;
}

bool emit_notes(const Reference<css::text::XTextDocument>& text_doc,
                const Reference<css::text::XTextViewCursor>& cursor,
                CaretSpace* space, bool endnotes, MarkerCollector* marks,
                const EmitFn& emit_fn, Warner& warner) {
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
      caret_at(cursor, anchor, label + " anchor", space,
               out->mutable_anchor(), &page_index, warner);
      out->set_page_index(page_index);
      flatten_text_runs(Reference<css::text::XText>(note, UNO_QUERY), label,
                        out->mutable_runs(), marks, warner);
    } catch (const css::uno::Exception& error) {
      warner.warn(label + " extraction failed", error);
    }
    if (!emit_fn(event)) return false;
  }
  return true;
}

bool emit_document_indexes(const Reference<css::text::XTextDocument>& text_doc,
                           const Reference<css::text::XTextViewCursor>& cursor,
                           CaretSpace* space, MarkerCollector* marks,
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
      caret_at(cursor, anchor, label + " anchor", space,
               out->mutable_anchor(), &page_index, warner);
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
                fill_runs(paragraph, label, out->mutable_runs(), nullptr,
                          marks, warner);
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
                      const PartSelection& parts, MarkerCollector* marks,
                      const EmitFn& emit_fn, Warner& warner) {
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
                               nullptr, nullptr, nullptr, marks, &paragraph,
                               warner)) {
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
                       const PartSelection& parts, SelectionProbe* probe,
                       const EmitFn& emit_fn, Warner& warner) {
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
  CaretSpace caret_space(model, probe != nullptr ? &probe->pages : nullptr);

  bool want_paragraphs = parts.wants(officev1::DOCUMENT_PART_PARAGRAPHS);
  bool want_tables = parts.wants(officev1::DOCUMENT_PART_TABLES);
  bool want_markers = parts.wants(officev1::DOCUMENT_PART_COMMENTS) ||
                      parts.wants(officev1::DOCUMENT_PART_TRACKED_CHANGES) ||
                      parts.wants(officev1::DOCUMENT_PART_BOOKMARKS) ||
                      parts.wants(officev1::DOCUMENT_PART_FORM_FIELDS);
  // The marker collector rides every portion walk, so comment, bookmark,
  // tracked-change, and form-field boundaries are observed exactly where
  // the annotation-space offsets are counted.
  std::optional<MarkerCollector> collector;
  MarkerCollector* marks = nullptr;
  if (want_markers) {
    collector.emplace(parts, cursor, &caret_space, emit_fn, warner);
    marks = &*collector;
  }
  // The probe may be live for the explicit per-cell table part alone; the
  // paragraph and image measurements still key on LINE_RECTS.
  SelectionProbe* line_probe =
      parts.wants(officev1::DOCUMENT_PART_LINE_RECTS) ? probe : nullptr;
  // Body tables first, then the tables text frames hold, in one index space.
  int32_t table_index = 0;
  if (want_paragraphs || want_tables || want_markers) {
    Reference<css::container::XEnumerationAccess> body(text_doc->getText(),
                                                       UNO_QUERY);
    if (!body.is()) {
      warner.warn("document body is not enumerable");
      return true;
    }
    Reference<css::container::XEnumeration> elements = body->createEnumeration();
    int32_t paragraph_index = 0;
    int64_t annotation_offset = 0;
    // When only markers want the body walk, the caret and line-rectangle
    // measurements behind the unselected paragraph part stay skipped.
    Reference<css::text::XTextViewCursor> paragraph_cursor =
        want_paragraphs ? cursor : Reference<css::text::XTextViewCursor>();
    while (elements->hasMoreElements()) {
      css::uno::Any element = elements->nextElement();
      Reference<css::text::XTextTable> table(element, UNO_QUERY);
      if (table.is()) {
        if (want_tables) {
          if (!emit_table(table, table_index, cursor, &caret_space, parts,
                          probe, emit_fn, warner)) {
            return false;
          }
        }
        if (marks != nullptr) {
          walk_table_marks(table, "table " + std::to_string(table_index),
                           marks, warner);
          if (marks->failed()) return false;
        }
        table_index++;
        continue;
      }
      if (!want_paragraphs && marks == nullptr) continue;
      officev1::StreamPagesResponse event;
      if (!fill_paragraph(element, paragraph_index, paragraph_cursor,
                          &caret_space, &annotation_offset, line_probe, marks,
                          event.mutable_paragraph(), warner)) {
        continue;
      }
      if (want_paragraphs && !emit_fn(event)) return false;
      if (marks != nullptr && marks->failed()) return false;
      paragraph_index++;
    }
  }
  if (parts.wants(officev1::DOCUMENT_PART_FOOTNOTES)) {
    if (!emit_notes(text_doc, cursor, &caret_space, false, marks, emit_fn,
                    warner)) {
      return false;
    }
    if (!emit_notes(text_doc, cursor, &caret_space, true, marks, emit_fn,
                    warner)) {
      return false;
    }
  }
  if (parts.wants(officev1::DOCUMENT_PART_INDEXES)) {
    if (!emit_document_indexes(text_doc, cursor, &caret_space, marks, emit_fn,
                               warner)) {
      return false;
    }
  }
  if (parts.wants(officev1::DOCUMENT_PART_PAGE_STYLES) ||
      parts.wants(officev1::DOCUMENT_PART_HEADERS_FOOTERS)) {
    if (!emit_page_styles(model, parts, marks, emit_fn, warner)) return false;
  }
  if (parts.wants(officev1::DOCUMENT_PART_TEXT_FRAMES)) {
    if (!emit_text_frames(text_doc, cursor, &caret_space, marks,
                          want_tables ? &parts : nullptr, probe, &table_index,
                          emit_fn, warner)) {
      return false;
    }
  }
  if (parts.wants(officev1::DOCUMENT_PART_IMAGES) ||
      parts.wants(officev1::DOCUMENT_PART_SHAPES) ||
      (parts.wants(officev1::DOCUMENT_PART_FORM_FIELDS) && marks != nullptr)) {
    if (!emit_draw_shapes(text_doc, context, cursor, &caret_space, parts,
                          line_probe, marks, emit_fn, warner)) {
      return false;
    }
  }
  if (marks != nullptr) {
    return marks->finish(text_doc, model);
  }
  return true;
}

// emit_typed_content over a caller's Warner, so the redaction check can
// count what the walk failed to read.
bool emit_typed_content_with(const PartSelection& parts, SelectionProbe* probe,
                             const EmitFn& emit_fn, Warner& warner) {
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) {
      warner.warn("no in-process UNO context, typed content unavailable");
      return true;
    }
    Reference<css::frame::XModel> model = find_loaded_model(context);
    if (!model.is()) {
      warner.warn("loaded document not found on the desktop, typed content unavailable");
      return true;
    }
    if (parts.wants(officev1::DOCUMENT_PART_METADATA)) {
      if (!emit_metadata(model, emit_fn, warner)) return false;
    }
    if (parts.wants(officev1::DOCUMENT_PART_EMBEDDED_OBJECTS)) {
      if (!emit_embedded_objects(model, context,
                                 probe != nullptr ? &probe->pages : nullptr,
                                 emit_fn, warner)) {
        return false;
      }
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
          parts.wants(officev1::DOCUMENT_PART_SHAPES) ||
          parts.wants(officev1::DOCUMENT_PART_COMMENTS) ||
          parts.wants(officev1::DOCUMENT_PART_TRACKED_CHANGES) ||
          parts.wants(officev1::DOCUMENT_PART_BOOKMARKS) ||
          parts.wants(officev1::DOCUMENT_PART_FORM_FIELDS);
      if (!wants_text_part) return true;
      return emit_text_content(text_doc, context, parts, probe, emit_fn,
                               warner);
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
      if (!parts.wants(officev1::DOCUMENT_PART_SLIDES) &&
          !parts.wants(officev1::DOCUMENT_PART_COMMENTS) &&
          !parts.wants(officev1::DOCUMENT_PART_IMAGES)) {
        return true;
      }
      Reference<css::drawing::XDrawPagesSupplier> slides(model, UNO_QUERY);
      if (slides.is()) {
        return emit_presentation_content(slides, context, parts, emit_fn,
                                         warner);
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

}  // namespace

bool emit_typed_content(const PartSelection& parts, SelectionProbe* probe,
                        const EmitFn& emit_fn,
                        std::vector<std::string>* warnings) {
  Warner warner(warnings);
  return emit_typed_content_with(parts, probe, emit_fn, warner);
}

bool export_pdf_stream(const std::string& filter_name, size_t chunk_limit,
                       const std::function<bool(std::string&&)>& emit_chunk,
                       long* total_bytes, std::string* error,
                       const PdfExportOptions& pdf) {
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) {
      *error = "no in-process UNO context for PDF export";
      return false;
    }
    Reference<css::frame::XModel> model = find_loaded_model(context);
    Reference<css::frame::XStorable> storable(model, UNO_QUERY);
    if (!storable.is()) {
      *error = "loaded document not found on the desktop for PDF export";
      return false;
    }
    rtl::Reference<PdfChunkSink> sink(new PdfChunkSink(chunk_limit, emit_chunk));
    // FilterData mirrors what LOK's own saveAs sends, byte for byte. It is
    // load-bearing: a descriptor without FilterData flips the pdf filter
    // into the installation's configured defaults (tagged PDF among them)
    // and changes the output. Any future filter option must be merged into
    // this sequence, keeping it non-empty.
    const bool ranged = pdf.first_page > 0 || pdf.last_page > 0;
    css::uno::Sequence<css::beans::PropertyValue> filter_data(
        1 + (ranged ? 1 : 0) + (pdf.skip_hidden ? 1 : 0));
    sal_Int32 slot = 0;
    filter_data.getArray()[slot].Name = "ExportBookmarks";
    filter_data.getArray()[slot++].Value <<= true;
    if (ranged) {
      const int first = pdf.first_page > 0 ? pdf.first_page : 1;
      const int last = pdf.last_page > 0 ? pdf.last_page : 9999;
      std::string range = std::to_string(first) + "-" + std::to_string(last);
      filter_data.getArray()[slot].Name = "PageRange";
      filter_data.getArray()[slot++].Value <<=
          rtl::OUString::createFromAscii(range.c_str());
    }
    if (pdf.skip_hidden) {
      // An installation configured with ExportHiddenSlides=true would
      // otherwise include hidden slides; unlisted FilterData entries fall
      // back to that configuration. Unknown entries are ignored by the
      // non-impress filters.
      filter_data.getArray()[slot].Name = "ExportHiddenSlides";
      filter_data.getArray()[slot++].Value <<= false;
    }
    css::uno::Sequence<css::beans::PropertyValue> descriptor(3);
    css::beans::PropertyValue* props = descriptor.getArray();
    props[0].Name = "FilterName";
    props[0].Value <<= rtl::OUString::createFromAscii(filter_name.c_str());
    props[1].Name = "OutputStream";
    props[1].Value <<= Reference<css::io::XOutputStream>(sink.get());
    props[2].Name = "FilterData";
    props[2].Value <<= filter_data;
    // private:stream routes the store to the OutputStream instead of a URL;
    // the same idiom LOK uses for shape rendering.
    storable->storeToURL("private:stream", descriptor);
    if (sink->total() == 0) {
      *error = "PDF export produced no bytes";
      return false;
    }
    if (!sink->closed()) {
      // Without closeOutput the sink's buffered tail was never flushed, so
      // whatever reached the wire is an incomplete PDF.
      *error = "PDF filter delivered bytes but never closed the output "
               "stream; the export is incomplete";
      return false;
    }
    *total_bytes = sink->total();
    return true;
  } catch (const css::uno::Exception& failure) {
    *error = "PDF export failed: " + utf8(failure.Message);
    return false;
  }
}

bool is_repairable_broken_package(const std::string& bytes) {
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) return false;
    Reference<css::lang::XMultiComponentFactory> manager =
        context->getServiceManager();
    if (!manager.is()) return false;
    rtl::Reference<MemoryStream> stream(new MemoryStream());
    stream->writeBytes(css::uno::Sequence<sal_Int8>(
        reinterpret_cast<const sal_Int8*>(bytes.data()),
        static_cast<sal_Int32>(bytes.size())));
    stream->seek(0);
    // The identical probe the office core's type detection runs before
    // offering its repair path (filter TypeDetection::isBrokenZIP): a plain
    // ZipPackage open throws for broken-or-not-ZIP bytes, and a
    // RepairPackage-mode reopen that yields content proves repairability.
    css::uno::Sequence<css::uno::Any> plain(3);
    css::uno::Any* args = plain.getArray();
    args[0] <<= Reference<css::io::XInputStream>(stream.get());
    args[1] <<= css::beans::NamedValue("AllowRemoveOnInsert",
                                       css::uno::Any(false));
    args[2] <<= css::beans::NamedValue(
        "StorageFormat", css::uno::Any(css::embed::StorageFormats::ZIP));
    try {
      manager->createInstanceWithArgumentsAndContext(
          "com.sun.star.packages.comp.ZipPackage", plain, context);
      return false;
    } catch (const css::packages::zip::ZipIOException&) {
    }
    stream->seek(0);
    css::uno::Sequence<css::uno::Any> repair(plain);
    repair.realloc(4);
    repair.getArray()[3] <<=
        css::beans::NamedValue("RepairPackage", css::uno::Any(true));
    Reference<css::beans::XPropertySet> package(
        manager->createInstanceWithArgumentsAndContext(
            "com.sun.star.packages.comp.ZipPackage", repair, context),
        UNO_QUERY);
    bool has_elements = false;
    if (package.is()) {
      package->getPropertyValue("HasElements") >>= has_elements;
    }
    return has_elements;
  } catch (const css::uno::Exception&) {
    // Anything the probe cannot classify is treated as a plain load
    // failure, never as repairable.
    return false;
  }
}

namespace {

void dispatch_uno(const Reference<css::frame::XModel>& model,
                  const Reference<css::uno::XComponentContext>& context,
                  const char* command, Warner& warner) {
  try {
    Reference<css::lang::XMultiComponentFactory> manager =
        context->getServiceManager();
    Reference<css::frame::XDispatchHelper> helper(
        manager->createInstanceWithContext(
            "com.sun.star.frame.DispatchHelper", context),
        UNO_QUERY);
    Reference<css::frame::XController> controller = model->getCurrentController();
    if (!helper.is() || !controller.is()) return;
    Reference<css::frame::XDispatchProvider> provider(
        controller->getFrame(), UNO_QUERY);
    if (!provider.is()) return;
    helper->executeDispatch(provider, oustring(command), oustring("_self"), 0,
                            css::uno::Sequence<css::beans::PropertyValue>());
  } catch (const css::uno::Exception& error) {
    warner.warn(std::string("dispatch ") + command, error);
  }
}

void apply_tracked_changes(const Reference<css::frame::XModel>& model,
                           const Reference<css::uno::XComponentContext>& context,
                           int display, Warner& warner) {
  if (display <= 0 ||
      display == officev1::TRACKED_CHANGE_DISPLAY_AS_IS) {
    return;
  }
  try {
    Reference<css::frame::XController> controller = model->getCurrentController();
    Reference<css::beans::XPropertySet> view(controller, UNO_QUERY);
    if (display == officev1::TRACKED_CHANGE_DISPLAY_FINAL) {
      dispatch_uno(model, context, ".uno:AcceptAllTrackedChanges", warner);
      if (view.is()) {
        view->setPropertyValue("ShowRedlineChanges", css::uno::Any(false));
      }
    } else if (display == officev1::TRACKED_CHANGE_DISPLAY_ORIGINAL) {
      dispatch_uno(model, context, ".uno:RejectAllTrackedChanges", warner);
    } else if (display == officev1::TRACKED_CHANGE_DISPLAY_SHOW_MARKUP) {
      if (view.is()) {
        view->setPropertyValue("ShowRedlineChanges", css::uno::Any(true));
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("tracked-change display", error);
  }
}

bool parse_bool_value(const std::string& value) {
  std::string lower = value;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
}

void fill_one_fieldmark(const Reference<css::text::XFormField>& fieldmark,
                        const std::string& value, Warner& warner) {
  try {
    Reference<css::container::XNameContainer> parameters =
        fieldmark->getParameters();
    if (parameters.is() && parameters->hasByName("Checkbox_Checked")) {
      parameters->replaceByName("Checkbox_Checked",
                                css::uno::Any(parse_bool_value(value)));
      return;
    }
    if (parameters.is() && (parameters->hasByName("Dropdown_Selected") ||
                            parameters->hasByName("Dropdown_ListEntry"))) {
      sal_Int32 selected = -1;
      try {
        selected = static_cast<sal_Int32>(std::stol(value));
      } catch (...) {
        css::uno::Any entries;
        try {
          parameters->getByName("Dropdown_ListEntry") >>= entries;
        } catch (const css::uno::Exception&) {
        }
        css::uno::Sequence<rtl::OUString> list;
        if (entries >>= list) {
          for (sal_Int32 i = 0; i < list.getLength(); i++) {
            if (utf8(list[i]) == value) {
              selected = i;
              break;
            }
          }
        }
      }
      if (selected >= 0) {
        parameters->replaceByName("Dropdown_Selected", css::uno::Any(selected));
      }
      return;
    }
    Reference<css::text::XTextContent> content(fieldmark, UNO_QUERY);
    if (content.is()) {
      Reference<css::text::XTextRange> range = content->getAnchor();
      if (range.is()) range->setString(oustring(value));
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("form fill fieldmark", error);
  }
}

void apply_form_fills(const Reference<css::frame::XModel>& model,
                      const std::vector<std::pair<std::string, std::string>>& values,
                      Warner& warner) {
  if (values.empty()) return;
  std::map<std::string, std::string> by_name(values.begin(), values.end());
  try {
    Reference<css::drawing::XDrawPagesSupplier> pages(model, UNO_QUERY);
    if (pages.is()) {
      Reference<css::drawing::XDrawPages> list = pages->getDrawPages();
      for (sal_Int32 i = 0; list.is() && i < list->getCount(); i++) {
        Reference<css::drawing::XShapes> shapes(list->getByIndex(i), UNO_QUERY);
        if (!shapes.is()) continue;
        for (sal_Int32 s = 0; s < shapes->getCount(); s++) {
          Reference<css::drawing::XControlShape> control(
              shapes->getByIndex(s), UNO_QUERY);
          if (!control.is()) continue;
          Reference<css::beans::XPropertySet> props(control->getControl(),
                                                    UNO_QUERY);
          if (!props.is()) continue;
          rtl::OUString name;
          try {
            props->getPropertyValue("Name") >>= name;
          } catch (const css::uno::Exception&) {
            continue;
          }
          auto found = by_name.find(utf8(name));
          if (found == by_name.end()) continue;
          try {
            props->setPropertyValue("Text", css::uno::Any(oustring(found->second)));
          } catch (const css::uno::Exception&) {
          }
          try {
            props->setPropertyValue("State",
                                    css::uno::Any(static_cast<sal_Int16>(
                                        parse_bool_value(found->second) ? 1 : 0)));
          } catch (const css::uno::Exception&) {
          }
        }
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("form fill controls", error);
  }
  try {
    Reference<css::text::XTextDocument> text_doc(model, UNO_QUERY);
    if (!text_doc.is()) return;
    Reference<css::text::XTextFieldsSupplier> fields(text_doc, UNO_QUERY);
    if (!fields.is()) return;
    Reference<css::container::XEnumerationAccess> access = fields->getTextFields();
    if (!access.is()) return;
    Reference<css::container::XEnumeration> it = access->createEnumeration();
    while (it->hasMoreElements()) {
      Reference<css::text::XFormField> fieldmark(it->nextElement(), UNO_QUERY);
      if (!fieldmark.is()) continue;
      Reference<css::container::XNamed> named(fieldmark, UNO_QUERY);
      if (!named.is()) continue;
      // Matched by the name the extraction reports, which is the stable form
      // of an import-generated name.
      const std::string name = utf8(named->getName());
      auto found = by_name.find(stable_mark_name(name));
      if (found == by_name.end()) found = by_name.find(name);
      if (found == by_name.end()) continue;
      fill_one_fieldmark(fieldmark, found->second, warner);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("form fill fields", error);
  }
}

// File-name fields print the path the worker loaded, a fresh temp directory
// per request, on the rendered pages and in every export; two parses of the
// same bytes would then differ in pixels and in what layout finds there.
// Each one is pinned to the text the extraction already reports for it: the
// upload's name in the field's own display format.
void pin_file_name_fields(const Reference<css::frame::XModel>& model,
                          Warner& warner) {
  try {
    Reference<css::text::XTextFieldsSupplier> fields(model, UNO_QUERY);
    if (!fields.is()) return;
    Reference<css::container::XEnumerationAccess> access = fields->getTextFields();
    if (!access.is()) return;
    Reference<css::container::XEnumeration> it = access->createEnumeration();
    while (it->hasMoreElements()) {
      Reference<css::text::XTextField> field(it->nextElement(), UNO_QUERY);
      if (!field.is() || text_field_code(field) != "FileName") continue;
      Reference<css::beans::XPropertySet> props(field, UNO_QUERY);
      if (!props.is()) continue;
      try {
        rtl::OUString shown = oustring(file_name_field_text(props));
        props->setPropertyValue("IsFixed", css::uno::Any(true));
        props->setPropertyValue("CurrentPresentation", css::uno::Any(shown));
      } catch (const css::uno::Exception& error) {
        warner.warn("pin file name field", error);
      }
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("pin file name fields", error);
  }
}

// ---- Redaction ------------------------------------------------------------
//
// The request's spans resolve to text, and that text leaves the document
// model, everywhere it occurs, before anything is laid out, painted,
// exported, or extracted. Spans and matching count code points, the unit
// of the annotation text space.

// Every redacted code point becomes one U+2588 FULL BLOCK.
constexpr sal_Unicode kRedactionGlyph = 0x2588;

// Nested tables, content controls, and groups past this depth are beyond
// the walks, so a redaction that would have to reach into them is refused.
constexpr int kMaxRedactionNesting = kMaxShapeGroupDepth;

using CodePoints = std::u32string;
using Intervals = std::vector<std::pair<size_t, size_t>>;

CodePoints code_points(const rtl::OUString& text) {
  CodePoints out;
  out.reserve(static_cast<size_t>(text.getLength()));
  for (sal_Int32 i = 0; i < text.getLength();) {
    out.push_back(static_cast<char32_t>(text.iterateCodePoints(&i)));
  }
  return out;
}

rtl::OUString from_code_points(const CodePoints& text) {
  rtl::OUStringBuffer buffer(static_cast<sal_Int32>(text.size()));
  for (char32_t c : text) buffer.appendUtf32(static_cast<sal_uInt32>(c));
  return buffer.makeStringAndClear();
}

bool redaction_space(char32_t c) {
  return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\v'
      || c == U'\f' || c == 0x00A0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200B)
      || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F
      || c == 0x3000 || c == 0xFEFF;
}

// The intervals of text any piece covers, overlapping occurrences
// included, sorted and merged.
Intervals covered_intervals(const CodePoints& text,
                            const std::vector<CodePoints>& pieces) {
  Intervals found;
  for (const CodePoints& piece : pieces) {
    if (piece.empty() || piece.size() > text.size()) continue;
    for (size_t at = text.find(piece); at != CodePoints::npos;
         at = text.find(piece, at + 1)) {
      found.emplace_back(at, at + piece.size());
    }
  }
  std::sort(found.begin(), found.end());
  Intervals merged;
  for (const auto& interval : found) {
    if (!merged.empty() && interval.first <= merged.back().second) {
      merged.back().second = std::max(merged.back().second, interval.second);
    } else {
      merged.push_back(interval);
    }
  }
  return merged;
}

CodePoints masked(CodePoints text, const Intervals& intervals) {
  for (const auto& [first, last] : intervals) {
    for (size_t i = first; i < last && i < text.size(); i++) {
      text[i] = kRedactionGlyph;
    }
  }
  return text;
}

// One walk over the loaded document's text. kReplace rewrites every
// occurrence of a piece to glyphs; kVerify only looks, and its first find
// is the refusal. A region a walk can neither rewrite nor see into
// refuses in either mode.
struct RedactWalk {
  enum class Mode { kReplace, kVerify };
  Mode mode = Mode::kReplace;
  std::vector<CodePoints> pieces;
  Warner* warner = nullptr;
  // The first refusal; every walk unwinds once it is set. It names the
  // region, never the text found there.
  std::string refusal;
  // The embedding depth of the content being walked, for the walks inside
  // an embedded object, where an object nested on a draw page is inspected
  // in turn. -1 for the host document, whose objects are checked through
  // check_embedded_objects instead.
  int ole_depth = -1;

  bool replacing() const { return mode == Mode::kReplace; }
  bool refused() const { return !refusal.empty(); }
  void refuse(const std::string& why) {
    if (refusal.empty()) refusal = "redaction refused: " + why;
  }
  void remains_in(const std::string& region) {
    refuse("redacted text remains in " + region);
  }
};

// Rewrites (or checks) one string; returns the rewritten value, or
// nothing when it carries no piece or the walk only verifies.
std::optional<rtl::OUString> redact_value(const rtl::OUString& value,
                                          const std::string& region,
                                          RedactWalk& walk) {
  CodePoints text = code_points(value);
  Intervals intervals = covered_intervals(text, walk.pieces);
  if (intervals.empty()) return std::nullopt;
  if (!walk.replacing()) {
    walk.remains_in(region);
    return std::nullopt;
  }
  return from_code_points(masked(std::move(text), intervals));
}

// Rewrites (or checks) one string property; absent properties and
// non-string values are skipped.
void redact_property(const Reference<css::beans::XPropertySet>& props,
                     const rtl::OUString& property, const std::string& region,
                     RedactWalk& walk) {
  if (!props.is() || walk.refused()) return;
  try {
    Reference<css::beans::XPropertySetInfo> info = props->getPropertySetInfo();
    if (info.is() && !info->hasPropertyByName(property)) return;
    rtl::OUString value;
    if (!(props->getPropertyValue(property) >>= value)) return;
    if (std::optional<rtl::OUString> rewritten =
            redact_value(value, region, walk)) {
      props->setPropertyValue(property, css::uno::Any(*rewritten));
    }
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: not every object of a family has the property.
  } catch (const css::uno::Exception& error) {
    // A rewrite that failed is caught by the verify walk, which reads the
    // value again; a value the verify walk cannot read is not verified.
    if (!walk.replacing()) walk.refuse(region + " cannot be inspected");
    walk.warner->warn("redaction of " + region + " failed", error);
  }
}

void redact_property(const Reference<css::beans::XPropertySet>& props,
                     const char* name, const std::string& region,
                     RedactWalk& walk) {
  redact_property(props, rtl::OUString::createFromAscii(name), region, walk);
}

// Property names whose string values are the office core's vocabulary
// (service, font, and fill-style names), never authored text, and which
// other objects look up by value.
bool vocabulary_property(const rtl::OUString& name) {
  return name == "DefaultControl" || name == "Role" || name.indexOf("Font") >= 0
      || name.endsWith("GradientName") || name.endsWith("HatchName")
      || name.endsWith("BitmapName") || name.endsWith("DashName")
      || name == "LineStartName" || name == "LineEndName";
}

// Rewrites (or checks) every string and every list of strings a property
// set carries, vocabulary aside: for objects whose strings all end up on
// the page or in the PDF: form controls and their forms, and image map
// areas. In verify mode a property that cannot be read refuses.
void redact_all_strings(const Reference<css::beans::XPropertySet>& props,
                        const std::string& region, RedactWalk& walk) {
  if (!props.is() || walk.refused()) return;
  Reference<css::beans::XPropertySetInfo> info = props->getPropertySetInfo();
  if (!info.is()) {
    walk.refuse(region + " cannot be inspected");
    return;
  }
  for (const css::beans::Property& property : info->getProperties()) {
    if (walk.refused()) return;
    if (vocabulary_property(property.Name)) continue;
    // Only properties that can hold text are read: reading the others
    // (a form's connection among them) can have side effects.
    const css::uno::TypeClass type = property.Type.getTypeClass();
    if (type != css::uno::TypeClass_STRING && type != css::uno::TypeClass_ANY
        && property.Type != cppu::UnoType<css::uno::Sequence<rtl::OUString>>::get()) {
      continue;
    }
    try {
      css::uno::Any value = props->getPropertyValue(property.Name);
      rtl::OUString text;
      css::uno::Sequence<rtl::OUString> list;
      if (value >>= text) {
        if (std::optional<rtl::OUString> rewritten =
                redact_value(text, region, walk)) {
          props->setPropertyValue(property.Name, css::uno::Any(*rewritten));
        }
      } else if (value >>= list) {
        bool changed = false;
        for (sal_Int32 i = 0; i < list.getLength(); i++) {
          if (std::optional<rtl::OUString> rewritten =
                  redact_value(list[i], region, walk)) {
            list.getArray()[i] = *rewritten;
            changed = true;
          }
        }
        if (changed) props->setPropertyValue(property.Name, css::uno::Any(list));
      }
    } catch (const css::uno::Exception& error) {
      // As in redact_property: the verify walk reads every value again,
      // and one it cannot read, listed as it is, is not verified.
      if (!walk.replacing()) walk.refuse(region + " cannot be inspected");
      walk.warner->warn("redaction of " + region + " failed", error);
    }
  }
}

// A stretch of paragraph text a walk may rewrite: a text portion, or a
// text field whose rendered result becomes literal text when rewritten.
struct TextSegment {
  Reference<css::text::XTextRange> range;
  Reference<css::beans::XPropertySet> props;
  bool field = false;
  CodePoints text;
};

// Checks (or rewrites) the strings a content control carries beside its
// text: list entries, alias, and tag. Its text is walked as segments.
void redact_content_control(const Reference<css::beans::XPropertySet>& control,
                            const std::string& region, RedactWalk& walk) {
  redact_property(control, "Alias", region + " (content control alias)", walk);
  redact_property(control, "Tag", region + " (content control tag)", walk);
  if (!control.is() || walk.refused()) return;
  try {
    css::uno::Sequence<css::uno::Sequence<css::beans::PropertyValue>> items;
    if (!(control->getPropertyValue("ListItems") >>= items)) return;
    bool changed = false;
    for (sal_Int32 i = 0; i < items.getLength(); i++) {
      css::uno::Sequence<css::beans::PropertyValue>& item = items.getArray()[i];
      for (sal_Int32 j = 0; j < item.getLength(); j++) {
        css::beans::PropertyValue& entry = item.getArray()[j];
        rtl::OUString value;
        if (!(entry.Value >>= value)) continue;
        if (std::optional<rtl::OUString> rewritten = redact_value(
                value, region + " (content control list entry)", walk)) {
          entry.Value <<= *rewritten;
          changed = true;
        }
      }
    }
    if (changed) control->setPropertyValue("ListItems", css::uno::Any(items));
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: only list content controls carry entries.
  } catch (const css::uno::Exception& error) {
    if (!walk.replacing()) {
      walk.refuse(region + " (a content control's list entries) cannot be "
                  "inspected");
    }
    walk.warner->warn("redaction of " + region + " content control failed",
                      error);
  }
}

// Flattens a paragraph's portions into text segments in reading order,
// descending into content controls and in-content metadata, whose text
// the portion enumeration nests instead of listing. Text and field
// portions contribute exactly what fill_runs puts in the annotation text
// space; the remaining portion kinds carry no text of their own, and one
// that does and holds a piece is a region the walk cannot rewrite.
void collect_segments(const Reference<css::container::XEnumerationAccess>& portions_access,
                      const std::string& region, int depth, RedactWalk& walk,
                      std::vector<TextSegment>* out) {
  if (depth > kMaxRedactionNesting) {
    walk.refuse(region + " nests content controls deeper than "
                + std::to_string(kMaxRedactionNesting) + " levels");
    return;
  }
  Reference<css::container::XEnumeration> portions =
      portions_access->createEnumeration();
  while (portions->hasMoreElements() && !walk.refused()) {
    css::uno::Any element = portions->nextElement();
    Reference<css::text::XTextRange> range(element, UNO_QUERY);
    Reference<css::beans::XPropertySet> props(element, UNO_QUERY);
    if (!range.is() || !props.is()) continue;
    rtl::OUString type;
    try {
      props->getPropertyValue("TextPortionType") >>= type;
    } catch (const css::beans::UnknownPropertyException&) {
      // Expected probe result: a portion without a type is plain text.
      type = "Text";
    }
    if (type == "Text") {
      out->push_back({range, props, false, code_points(range->getString())});
    } else if (type == "TextField") {
      // The same text fill_field_run puts in the annotation space.
      rtl::OUString shown = range->getString();
      Reference<css::text::XTextField> field;
      try {
        props->getPropertyValue("TextField") >>= field;
      } catch (const css::beans::UnknownPropertyException&) {
        // Expected probe result: a field portion may not expose its field.
      }
      if (field.is() && text_field_code(field) == "FileName") {
        shown = oustring(file_name_field_text(
            Reference<css::beans::XPropertySet>(field, UNO_QUERY)));
      } else if (shown.isEmpty() && field.is()) {
        shown = field->getPresentation(false);
      }
      out->push_back({range, props, true, code_points(shown)});
    } else if (type == "ContentControl" || type == "InContentMetadata") {
      Reference<css::uno::XInterface> nested;
      props->getPropertyValue(type) >>= nested;
      if (type == "ContentControl") {
        redact_content_control(
            Reference<css::beans::XPropertySet>(nested, UNO_QUERY), region,
            walk);
      }
      Reference<css::container::XEnumerationAccess> inner(nested, UNO_QUERY);
      if (inner.is()) {
        collect_segments(inner, region, depth + 1, walk, out);
      } else if (!covered_intervals(code_points(range->getString()),
                                    walk.pieces)
                      .empty()) {
        walk.remains_in(region + " (a nested text that cannot be walked)");
      }
    } else if (type == "Ruby") {
      redact_property(props, "RubyText", region + " (ruby text)", walk);
    } else if (type == "DocumentIndexMark") {
      Reference<css::beans::XPropertySet> mark;
      props->getPropertyValue("DocumentIndexMark") >>= mark;
      for (const char* name : {"AlternativeText", "PrimaryKey", "SecondaryKey"}) {
        redact_property(mark, name, region + " (an index entry)", walk);
      }
    } else if (!covered_intervals(code_points(range->getString()), walk.pieces)
                    .empty()) {
      walk.remains_in(region + " (a " + utf8(type) + " portion)");
    }
  }
}

// Steps a cursor right by count characters; the UNO call takes a short.
void step_right(const Reference<css::text::XTextCursor>& cursor, size_t count,
                bool expand) {
  while (count > 0) {
    const sal_Int16 step = static_cast<sal_Int16>(std::min<size_t>(count, 32767));
    cursor->goRight(step, expand);
    count -= static_cast<size_t>(step);
  }
}

rtl::OUString glyphs(size_t count) {
  rtl::OUStringBuffer buffer(static_cast<sal_Int32>(count));
  for (size_t i = 0; i < count; i++) buffer.append(kRedactionGlyph);
  return buffer.makeStringAndClear();
}

// Draws rewritten text black on black, so the glyphs read as a solid bar
// whatever color the text had. Best effort: the glyphs alone already
// carry nothing of the original.
void blacken(const Reference<css::beans::XPropertySet>& props) {
  if (!props.is()) return;
  for (const auto& [name, value] :
       {std::pair<const char*, css::uno::Any>{"CharColor",
                                              css::uno::Any(sal_Int32(0))},
        {"CharBackColor", css::uno::Any(sal_Int32(0))},
        {"CharBackTransparent", css::uno::Any(false)}}) {
    try {
      props->setPropertyValue(rtl::OUString::createFromAscii(name), value);
    } catch (const css::uno::Exception&) {
      // Not every text family carries every character property.
    }
  }
}

// Rewrites the covered code points of one segment to glyphs. A text
// portion of BMP characters is edited in place through a cursor, which
// keeps the formatting of the characters it leaves alone. A field, or a
// portion holding characters outside the BMP (where Writer's cursors step
// by code point and the drawing text's by UTF-16 unit), is replaced whole.
void rewrite_segment(const TextSegment& segment, const Intervals& local,
                     const std::string& region, RedactWalk& walk) {
  try {
    const bool bmp = std::ranges::all_of(
        segment.text, [](char32_t c) { return c < 0x10000; });
    if (!segment.field && bmp) {
      Reference<css::text::XText> text = segment.range->getText();
      for (auto it = local.rbegin(); it != local.rend(); ++it) {
        Reference<css::text::XTextCursor> cursor =
            text->createTextCursorByRange(segment.range->getStart());
        step_right(cursor, it->first, false);
        step_right(cursor, it->second - it->first, true);
        cursor->setString(glyphs(it->second - it->first));
        blacken(Reference<css::beans::XPropertySet>(cursor, UNO_QUERY));
      }
      return;
    }
    segment.range->setString(from_code_points(masked(segment.text, local)));
  } catch (const css::uno::Exception& error) {
    // The verify walk finds whatever this left behind and refuses.
    walk.warner->warn("redaction rewrite in " + region + " failed", error);
  }
}

// Rewrites (or checks) one paragraph: its hyperlink targets, then every
// covered stretch of its text, the rightmost first so no rewrite moves a
// range still to come.
void redact_paragraph(const Reference<css::container::XEnumerationAccess>& paragraph,
                      const std::string& region, RedactWalk& walk) {
  std::vector<TextSegment> segments;
  collect_segments(paragraph, region, 0, walk, &segments);
  if (walk.refused() || segments.empty()) return;
  for (const TextSegment& segment : segments) {
    for (const char* name : {"HyperLinkURL", "HyperLinkName", "HyperLinkTarget"}) {
      redact_property(segment.props, name, region + " (a hyperlink)", walk);
    }
  }
  if (walk.refused()) return;
  CodePoints joined;
  std::vector<size_t> starts;
  for (const TextSegment& segment : segments) {
    starts.push_back(joined.size());
    joined += segment.text;
  }
  Intervals intervals = covered_intervals(joined, walk.pieces);
  if (intervals.empty()) return;
  if (!walk.replacing()) {
    walk.remains_in(region);
    return;
  }
  for (size_t s = segments.size(); s-- > 0;) {
    const size_t begin = starts[s];
    const size_t end = begin + segments[s].text.size();
    Intervals local;
    for (const auto& [first, last] : intervals) {
      const size_t lo = std::max(first, begin);
      const size_t hi = std::min(last, end);
      if (lo < hi) local.emplace_back(lo - begin, hi - begin);
    }
    if (!local.empty()) rewrite_segment(segments[s], local, region, walk);
  }
}

// Walks every paragraph of a text, descending into its tables. Elements
// are collected first, so rewriting never races the enumeration.
void redact_text(const Reference<css::text::XText>& text,
                 const std::string& region, int depth, RedactWalk& walk) {
  if (!text.is() || walk.refused()) return;
  if (depth > kMaxRedactionNesting) {
    walk.refuse(region + " nests tables deeper than "
                + std::to_string(kMaxRedactionNesting) + " levels");
    return;
  }
  Reference<css::container::XEnumerationAccess> access(text, UNO_QUERY);
  if (!access.is()) {
    if (!covered_intervals(code_points(text->getString()), walk.pieces).empty()) {
      walk.remains_in(region + " (a text that cannot be walked)");
    }
    return;
  }
  std::vector<css::uno::Any> elements;
  Reference<css::container::XEnumeration> it = access->createEnumeration();
  while (it->hasMoreElements()) elements.push_back(it->nextElement());
  for (const css::uno::Any& element : elements) {
    if (walk.refused()) return;
    Reference<css::text::XTextTable> table(element, UNO_QUERY);
    if (table.is()) {
      for (const rtl::OUString& name : table->getCellNames()) {
        Reference<css::text::XText> cell(table->getCellByName(name), UNO_QUERY);
        redact_text(cell, region + " (a table cell)", depth + 1, walk);
      }
      continue;
    }
    Reference<css::container::XEnumerationAccess> paragraph(element, UNO_QUERY);
    if (paragraph.is()) redact_paragraph(paragraph, region, walk);
  }
}

// The alt text every shape, frame, image, and object can carry.
void redact_alt_text(const Reference<css::beans::XPropertySet>& props,
                     const std::string& region, RedactWalk& walk) {
  redact_property(props, "Title", region + " (alt text title)", walk);
  redact_property(props, "Description", region + " (alt text description)", walk);
}

// Object names ride the typed events, and other objects refer to them by
// name (text-frame chains), so they are checked, never rewritten.
void check_name(const Reference<css::container::XNamed>& named,
                const std::string& region, RedactWalk& walk) {
  if (!named.is() || walk.refused()) return;
  if (!covered_intervals(code_points(named->getName()), walk.pieces).empty()) {
    walk.remains_in(region + " (its name, which is not rewritten)");
  }
}

// The links an object itself carries, beside any in its text: the
// hyperlink on an image, shape, frame, or object (which the PDF export
// writes as a link annotation), a drawing's click target, and the areas
// of an image map.
void redact_object_links(const Reference<css::beans::XPropertySet>& props,
                         const std::string& region, RedactWalk& walk) {
  for (const char* name : {"HyperLinkURL", "HyperLinkName", "HyperLinkTarget",
                           "Hyperlink", "Bookmark"}) {
    redact_property(props, name, region + " (its hyperlink)", walk);
  }
  if (!props.is() || walk.refused()) return;
  try {
    Reference<css::beans::XPropertySetInfo> info = props->getPropertySetInfo();
    if (info.is() && !info->hasPropertyByName("ImageMap")) return;
    Reference<css::container::XIndexAccess> areas;
    props->getPropertyValue("ImageMap") >>= areas;
    if (!areas.is() || areas->getCount() == 0) return;
    for (sal_Int32 i = 0; i < areas->getCount(); i++) {
      redact_all_strings(
          Reference<css::beans::XPropertySet>(areas->getByIndex(i), UNO_QUERY),
          region + " (an image map area)", walk);
    }
    // The property hands out a copy; the rewritten areas go back whole.
    if (walk.replacing()) {
      props->setPropertyValue("ImageMap", css::uno::Any(areas));
    }
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: not every object carries an image map.
  } catch (const css::uno::Exception& error) {
    if (!walk.replacing()) walk.refuse(region + " (its image map) cannot be inspected");
    walk.warner->warn("redaction of " + region + " image map failed", error);
  }
}

// Every form and control model under a container, nested forms and grid
// columns included: control names, labels, values, help texts, list
// entries, and submit or button URLs all reach the page or the PDF's form
// fields.
void redact_form_tree(const Reference<css::container::XIndexAccess>& container,
                      int depth, RedactWalk& walk) {
  if (depth > kMaxRedactionNesting) {
    walk.refuse("forms nest deeper than " + std::to_string(kMaxRedactionNesting)
                + " levels");
    return;
  }
  for (sal_Int32 i = 0; container.is() && i < container->getCount(); i++) {
    if (walk.refused()) return;
    css::uno::Any element = container->getByIndex(i);
    redact_all_strings(Reference<css::beans::XPropertySet>(element, UNO_QUERY),
                       "a form control", walk);
    redact_form_tree(Reference<css::container::XIndexAccess>(element, UNO_QUERY),
                     depth + 1, walk);
  }
}

void redact_forms(const Reference<css::uno::XInterface>& page, RedactWalk& walk) {
  Reference<css::form::XFormsSupplier2> supplier(page, UNO_QUERY);
  // hasForms first: getForms would create an empty collection.
  if (!supplier.is() || !supplier->hasForms() || walk.refused()) return;
  redact_form_tree(Reference<css::container::XIndexAccess>(supplier->getForms(),
                                                           UNO_QUERY),
                   0, walk);
}

// The cells of a drawing's table shape, each its own text.
void redact_table_shape(const Reference<css::beans::XPropertySet>& props,
                        RedactWalk& walk) {
  css::uno::Any model = props->getPropertyValue("Model");
  Reference<css::table::XCellRange> cells(model, UNO_QUERY);
  Reference<css::table::XColumnRowRange> grid(model, UNO_QUERY);
  if (!cells.is() || !grid.is()) {
    walk.refuse("a table shape whose cells cannot be walked");
    return;
  }
  const sal_Int32 rows = grid->getRows()->getCount();
  const sal_Int32 columns = grid->getColumns()->getCount();
  for (sal_Int32 r = 0; r < rows; r++) {
    for (sal_Int32 c = 0; c < columns; c++) {
      if (walk.refused()) return;
      redact_text(Reference<css::text::XText>(cells->getCellByPosition(c, r),
                                              UNO_QUERY),
                  "a table shape cell", 1, walk);
    }
  }
}

void inspect_ole_shape(const Reference<css::beans::XPropertySet>& props,
                       RedactWalk& walk);

// Walks a draw page: shape text (imported textboxes resolve to their
// backing frame's text), alt text, object links, table-shape cells, and
// form control models, groups recursed. Text frames are walked through
// the frame collection instead. Inside an embedded object (ole_depth set)
// an object nested on the page is inspected in turn.
void redact_shapes(const Reference<css::container::XIndexAccess>& shapes,
                   int depth, RedactWalk& walk) {
  for (sal_Int32 i = 0; shapes.is() && i < shapes->getCount(); i++) {
    if (walk.refused()) return;
    Reference<css::beans::XPropertySet> props(shapes->getByIndex(i), UNO_QUERY);
    Reference<css::lang::XServiceInfo> services(props, UNO_QUERY);
    if (!props.is() || !services.is()) continue;
    if (services->supportsService("com.sun.star.text.TextFrame")) continue;
    redact_alt_text(props, "a drawing shape", walk);
    check_name(Reference<css::container::XNamed>(props, UNO_QUERY),
               "a drawing shape", walk);
    redact_object_links(props, "a drawing shape", walk);
    if (walk.refused()) return;
    if (services->supportsService("com.sun.star.drawing.ControlShape")) {
      // The form walk reaches the same model; this covers a control whose
      // model sits outside the page's forms.
      Reference<css::drawing::XControlShape> control(props, UNO_QUERY);
      redact_all_strings(
          Reference<css::beans::XPropertySet>(
              control.is() ? control->getControl()
                           : Reference<css::awt::XControlModel>(),
              UNO_QUERY),
          "a form control", walk);
      continue;
    }
    if (services->supportsService("com.sun.star.drawing.OLE2Shape")) {
      if (walk.ole_depth >= 0) inspect_ole_shape(props, walk);
      continue;
    }
    if (services->supportsService("com.sun.star.drawing.TableShape")) {
      redact_table_shape(props, walk);
      continue;
    }
    if (services->supportsService("com.sun.star.drawing.GroupShape")) {
      if (depth >= kMaxShapeGroupDepth) {
        walk.refuse("shape groups nested deeper than "
                    + std::to_string(kMaxShapeGroupDepth)
                    + " levels cannot be redacted");
        return;
      }
      redact_shapes(Reference<css::container::XIndexAccess>(props, UNO_QUERY),
                    depth + 1, walk);
      continue;
    }
    redact_text(Reference<css::text::XText>(props, UNO_QUERY), "a drawing shape",
                0, walk);
  }
}

// Every header and footer text of every page style, first-page and
// left-page variants included, used or not.
void redact_headers_footers(const Reference<css::frame::XModel>& model,
                            RedactWalk& walk) {
  Reference<css::style::XStyleFamiliesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::container::XNameAccess> families = supplier->getStyleFamilies();
  if (!families.is() || !families->hasByName("PageStyles")) return;
  Reference<css::container::XIndexAccess> styles;
  families->getByName("PageStyles") >>= styles;
  for (sal_Int32 i = 0; styles.is() && i < styles->getCount(); i++) {
    Reference<css::beans::XPropertySet> props(styles->getByIndex(i), UNO_QUERY);
    if (!props.is()) continue;
    for (const char* name :
         {"HeaderText", "HeaderTextLeft", "HeaderTextRight", "HeaderTextFirst",
          "FooterText", "FooterTextLeft", "FooterTextRight", "FooterTextFirst"}) {
      Reference<css::text::XText> text;
      try {
        props->getPropertyValue(rtl::OUString::createFromAscii(name)) >>= text;
      } catch (const css::beans::UnknownPropertyException&) {
        // Expected probe result: not every core names every variant.
        continue;
      }
      redact_text(text,
                  std::string_view(name).starts_with("Header") ? "a page header"
                                                               : "a page footer",
                  0, walk);
      if (walk.refused()) return;
    }
  }
}

void redact_notes(const Reference<css::text::XTextDocument>& text_doc,
                  RedactWalk& walk) {
  Reference<css::text::XFootnotesSupplier> footnotes(text_doc, UNO_QUERY);
  Reference<css::text::XEndnotesSupplier> endnotes(text_doc, UNO_QUERY);
  for (const auto& [notes, region] :
       {std::pair<Reference<css::container::XIndexAccess>, const char*>{
            footnotes.is() ? footnotes->getFootnotes()
                           : Reference<css::container::XIndexAccess>(),
            "a footnote"},
        {endnotes.is() ? endnotes->getEndnotes()
                       : Reference<css::container::XIndexAccess>(),
         "an endnote"}}) {
    for (sal_Int32 i = 0; notes.is() && i < notes->getCount(); i++) {
      redact_text(Reference<css::text::XText>(notes->getByIndex(i), UNO_QUERY),
                  region, 0, walk);
    }
  }
}

void redact_frames(const Reference<css::text::XTextDocument>& text_doc,
                   RedactWalk& walk) {
  Reference<css::text::XTextFramesSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::container::XIndexAccess> frames(supplier->getTextFrames(),
                                                 UNO_QUERY);
  for (sal_Int32 i = 0; frames.is() && i < frames->getCount(); i++) {
    Reference<css::text::XTextFrame> frame(frames->getByIndex(i), UNO_QUERY);
    if (!frame.is()) continue;
    redact_alt_text(Reference<css::beans::XPropertySet>(frame, UNO_QUERY),
                    "a text frame", walk);
    redact_object_links(Reference<css::beans::XPropertySet>(frame, UNO_QUERY),
                        "a text frame", walk);
    check_name(Reference<css::container::XNamed>(frame, UNO_QUERY),
               "a text frame", walk);
    redact_text(frame->getText(), "a text frame", 0, walk);
  }
}

// Comment content, author, and initials. The anchored text is body text.
void redact_comments(const Reference<css::text::XTextDocument>& text_doc,
                     RedactWalk& walk) {
  Reference<css::text::XTextFieldsSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::container::XEnumerationAccess> access = supplier->getTextFields();
  if (!access.is()) return;
  std::vector<Reference<css::beans::XPropertySet>> comments;
  Reference<css::container::XEnumeration> it = access->createEnumeration();
  while (it->hasMoreElements()) {
    Reference<css::lang::XServiceInfo> field(it->nextElement(), UNO_QUERY);
    if (field.is() &&
        (field->supportsService("com.sun.star.text.textfield.Annotation") ||
         field->supportsService("com.sun.star.text.TextField.Annotation"))) {
      comments.emplace_back(field, UNO_QUERY);
    }
  }
  for (const Reference<css::beans::XPropertySet>& comment : comments) {
    redact_property(comment, "Content", "a comment", walk);
    redact_property(comment, "Author", "a comment's author", walk);
    redact_property(comment, "Initials", "a comment's initials", walk);
  }
}

// The stored document properties, which also become the PDF's document
// information. Custom property names cannot be rewritten; the typed check
// refuses when one carries a piece.
void redact_document_properties(const Reference<css::frame::XModel>& model,
                                RedactWalk& walk) {
  Reference<css::document::XDocumentPropertiesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::document::XDocumentProperties> props =
      supplier->getDocumentProperties();
  if (!props.is()) return;
  auto rewrite = [&](const rtl::OUString& value, const char* what,
                     const std::function<void(const rtl::OUString&)>& set) {
    if (std::optional<rtl::OUString> rewritten = redact_value(
            value, std::string("the document's ") + what, walk)) {
      set(*rewritten);
    }
  };
  rewrite(props->getTitle(), "title", [&](auto& v) { props->setTitle(v); });
  rewrite(props->getSubject(), "subject", [&](auto& v) { props->setSubject(v); });
  rewrite(props->getDescription(), "description",
          [&](auto& v) { props->setDescription(v); });
  rewrite(props->getAuthor(), "author", [&](auto& v) { props->setAuthor(v); });
  rewrite(props->getModifiedBy(), "last editor",
          [&](auto& v) { props->setModifiedBy(v); });
  rewrite(props->getPrintedBy(), "last printer",
          [&](auto& v) { props->setPrintedBy(v); });
  rewrite(props->getTemplateName(), "template name",
          [&](auto& v) { props->setTemplateName(v); });
  css::uno::Sequence<rtl::OUString> keywords = props->getKeywords();
  bool keywords_changed = false;
  for (sal_Int32 i = 0; i < keywords.getLength(); i++) {
    if (std::optional<rtl::OUString> rewritten =
            redact_value(keywords[i], "the document's keywords", walk)) {
      keywords.getArray()[i] = *rewritten;
      keywords_changed = true;
    }
  }
  if (keywords_changed) props->setKeywords(keywords);
  Reference<css::beans::XPropertySet> custom(props->getUserDefinedProperties(),
                                             UNO_QUERY);
  if (custom.is()) {
    for (const css::beans::Property& property :
         custom->getPropertySetInfo()->getProperties()) {
      redact_property(custom, property.Name, "a custom document property",
                      walk);
    }
  }
}

// Style names ride the typed events (paragraph and character styles on
// paragraphs and runs, page styles on pages and headers). A user-defined
// style named after the redacted text is renamed; built-in names are the
// office core's vocabulary and stay, and the typed check skips them.
void redact_style_names(const Reference<css::frame::XModel>& model,
                        RedactWalk& walk) {
  Reference<css::style::XStyleFamiliesSupplier> supplier(model, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::container::XNameAccess> families = supplier->getStyleFamilies();
  if (!families.is()) return;
  for (const char* family_name :
       {"ParagraphStyles", "CharacterStyles", "PageStyles"}) {
    const rtl::OUString family_key = rtl::OUString::createFromAscii(family_name);
    if (!families->hasByName(family_key)) continue;
    Reference<css::container::XNameAccess> family;
    families->getByName(family_key) >>= family;
    if (!family.is()) continue;
    for (const rtl::OUString& name : family->getElementNames()) {
      if (walk.refused()) return;
      Reference<css::style::XStyle> style;
      family->getByName(name) >>= style;
      if (!style.is() || !style->isUserDefined()) continue;
      if (std::optional<rtl::OUString> rewritten =
              redact_value(style->getName(), "a style name", walk)) {
        try {
          style->setName(*rewritten);
        } catch (const css::uno::Exception& error) {
          // The verify walk reads the name again and refuses if it stayed.
          walk.warner->warn("redaction of a style name failed", error);
        }
      }
    }
  }
}

// One full rewrite (or check) pass over a text document's text-bearing
// regions. In replace mode the document indexes are regenerated last, from
// headings that are already rewritten.
void redact_text_document(const Reference<css::text::XTextDocument>& text_doc,
                          const Reference<css::frame::XModel>& model,
                          RedactWalk& walk) {
  redact_document_properties(model, walk);
  redact_style_names(model, walk);
  redact_comments(text_doc, walk);
  redact_text(text_doc->getText(), "the document body", 0, walk);
  redact_headers_footers(model, walk);
  redact_notes(text_doc, walk);
  redact_frames(text_doc, walk);
  Reference<css::drawing::XDrawPageSupplier> page(text_doc, UNO_QUERY);
  if (page.is()) {
    Reference<css::drawing::XDrawPage> draw_page = page->getDrawPage();
    redact_shapes(Reference<css::container::XIndexAccess>(draw_page, UNO_QUERY),
                  0, walk);
    redact_forms(draw_page, walk);
  }
  Reference<css::text::XDocumentIndexesSupplier> indexes_supplier(text_doc,
                                                                  UNO_QUERY);
  Reference<css::container::XIndexAccess> indexes(
      indexes_supplier.is() ? indexes_supplier->getDocumentIndexes()
                            : Reference<css::container::XIndexAccess>(),
      UNO_QUERY);
  for (sal_Int32 i = 0; indexes.is() && i < indexes->getCount(); i++) {
    if (walk.refused()) return;
    Reference<css::text::XDocumentIndex> index(indexes->getByIndex(i), UNO_QUERY);
    if (!index.is()) continue;
    redact_property(Reference<css::beans::XPropertySet>(index, UNO_QUERY),
                    "Title", "a document index title", walk);
    if (walk.replacing()) {
      try {
        index->update();
      } catch (const css::uno::Exception& error) {
        walk.warner->warn("redaction: index regeneration failed", error);
      }
    }
  }
}

// Fields whose values are the office core's vocabulary rather than
// anything authored: font names, the names of built-in styles (user-defined
// style names are rewritten by redact_style_names and checked there),
// language tags, field and shape service names, media types, class ids,
// change kinds, and cell addresses. A redacted word that happens to name a
// built-in style ("Heading", "Strong") or a field ("Page") reveals nothing
// through them, so they do not count as the text surviving.
const std::set<std::string>& vocabulary_fields() {
  static const std::set<std::string> fields = [] {
    std::set<std::string> names;
    const std::string package = "ai.pipestream.office.v1.";
    for (const char* field :
         {"TextRun.font", "TextRun.language", "TextRun.field_code",
          "TextRun.char_style", "Paragraph.style", "PageStyleInfo.name",
          "HeaderFooter.page_style", "Shape.shape_type", "DrawingShape.shape_type",
          "SlideShape.shape_type", "EmbeddedImage.mime_type",
          "EmbeddedObject.clsid", "EmbeddedObject.replacement_mime_type",
          "EmbeddedChart.chart_type_service", "TrackedChange.kind_name",
          "TrackedChange.identifier", "TrackedChangeSuccessor.kind_name",
          "FormField.field_type", "DocumentIndex.type",
          "DocumentMetadata.generator", "DocumentMetadata.language",
          "DocumentMetadata.StatisticsEntry.key", "TableCellData.name"}) {
      names.insert(package + field);
    }
    return names;
  }();
  return fields;
}

// Finds the first string field of message, depth first, that carries a
// redacted string, the joined text of every run list included, and names
// it as a dotted field path. Vocabulary fields are not searched.
bool find_redacted(const google::protobuf::Message& message,
                   const std::vector<std::string>& redacted,
                   const std::string& path, std::string* where) {
  auto carries = [&](const std::string& value) {
    for (const std::string& piece : redacted) {
      if (!piece.empty() && value.find(piece) != std::string::npos) return true;
    }
    return false;
  };
  const google::protobuf::Reflection* reflection = message.GetReflection();
  std::vector<const google::protobuf::FieldDescriptor*> fields;
  reflection->ListFields(message, &fields);
  for (const google::protobuf::FieldDescriptor* field : fields) {
    const std::string name =
        path.empty() ? std::string(field->name())
                     : path + "." + std::string(field->name());
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
      if (vocabulary_fields().contains(std::string(field->full_name()))) continue;
      const int count = field->is_repeated() ? reflection->FieldSize(message, field) : 1;
      for (int i = 0; i < count; i++) {
        const std::string value =
            field->is_repeated() ? reflection->GetRepeatedString(message, field, i)
                                 : reflection->GetString(message, field);
        if (carries(value)) {
          *where = name;
          return true;
        }
      }
    } else if (field->cpp_type() ==
               google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (!field->is_repeated()) {
        if (find_redacted(reflection->GetMessage(message, field), redacted, name,
                          where)) {
          return true;
        }
        continue;
      }
      const int count = reflection->FieldSize(message, field);
      if (field->message_type() == officev1::TextRun::descriptor()) {
        std::string joined;
        for (int i = 0; i < count; i++) {
          joined += static_cast<const officev1::TextRun&>(
                        reflection->GetRepeatedMessage(message, field, i))
                        .text();
        }
        if (carries(joined)) {
          *where = name;
          return true;
        }
      }
      for (int i = 0; i < count; i++) {
        if (find_redacted(reflection->GetRepeatedMessage(message, field, i),
                          redacted, name, where)) {
          return true;
        }
      }
    }
  }
  return false;
}

void check_embedded_objects(const Reference<css::text::XTextDocument>& text_doc,
                            RedactWalk& walk, int depth = 0);
void inspect_model(const Reference<css::frame::XModel>& inner, RedactWalk& look,
                   int depth);

// The text of a chart title (the main title, the subtitle, an axis title).
void check_chart_title(const Reference<css::chart2::XTitled>& titled,
                       const std::string& region, RedactWalk& look) {
  if (!titled.is()) return;
  Reference<css::chart2::XTitle> title = titled->getTitleObject();
  if (!title.is()) return;
  for (const Reference<css::chart2::XFormattedString>& part : title->getText()) {
    if (part.is()) redact_value(part->getString(), region, look);
  }
}

// The text a data sequence shows: a label, a category, or a value as
// text.
void check_chart_sequence(const Reference<css::chart2::data::XDataSequence>& data,
                          const std::string& region, RedactWalk& look) {
  Reference<css::chart2::data::XTextualDataSequence> texts(data, UNO_QUERY);
  if (!texts.is()) return;
  for (const rtl::OUString& text : texts->getTextualData()) {
    redact_value(text, region, look);
  }
}

// The text a data series or data point adds to its data labels: custom
// label fields and the label separator.
void check_chart_points(const Reference<css::beans::XPropertySet>& props,
                        const std::string& region, RedactWalk& look) {
  if (!props.is() || look.refused()) return;
  redact_property(props, "LabelSeparator", region + " (a data label)", look);
  Reference<css::beans::XPropertySetInfo> info = props->getPropertySetInfo();
  if (!info.is() || !info->hasPropertyByName("CustomLabelFields")) return;
  css::uno::Sequence<Reference<css::chart2::XDataPointCustomLabelField>> fields;
  props->getPropertyValue("CustomLabelFields") >>= fields;
  for (const Reference<css::chart2::XDataPointCustomLabelField>& field : fields) {
    if (field.is()) redact_value(field->getString(), region + " (a data label)", look);
  }
}

// Looks at every text an embedded chart can draw: the title and subtitle,
// every axis title and category on every axis of every dimension, series
// names (which the legend shows) and their values as text, custom data
// labels on the series and on each formatted point, trend line names,
// the internal data table's row and column labels, and shapes drawn on
// the chart. Every UNO failure propagates, so the inspection refuses.
void inspect_chart(const Reference<css::chart2::XChartDocument>& chart,
                   RedactWalk& look) {
  check_chart_title(Reference<css::chart2::XTitled>(chart, UNO_QUERY),
                    "the chart title", look);
  Reference<css::chart2::XDiagram> diagram = chart->getFirstDiagram();
  if (diagram.is()) {
    check_chart_title(Reference<css::chart2::XTitled>(diagram, UNO_QUERY),
                      "the chart subtitle", look);
    Reference<css::chart2::XCoordinateSystemContainer> systems(diagram, UNO_QUERY);
    for (const Reference<css::chart2::XCoordinateSystem>& coord :
         systems.is() ? systems->getCoordinateSystems()
                      : css::uno::Sequence<Reference<css::chart2::XCoordinateSystem>>()) {
      if (!coord.is() || look.refused()) continue;
      for (sal_Int32 dimension = 0; dimension < coord->getDimension(); dimension++) {
        const sal_Int32 last = coord->getMaximumAxisIndexByDimension(dimension);
        for (sal_Int32 index = 0; index <= last; index++) {
          Reference<css::chart2::XAxis> axis =
              coord->getAxisByDimension(dimension, index);
          if (!axis.is()) continue;
          check_chart_title(Reference<css::chart2::XTitled>(axis, UNO_QUERY),
                            "a chart axis title", look);
          css::chart2::ScaleData scale = axis->getScaleData();
          if (scale.Categories.is()) {
            check_chart_sequence(scale.Categories->getLabel(),
                                 "a chart category label", look);
            check_chart_sequence(scale.Categories->getValues(),
                                 "a chart category", look);
          }
        }
      }
      Reference<css::chart2::XChartTypeContainer> types(coord, UNO_QUERY);
      if (!types.is()) continue;
      for (const Reference<css::chart2::XChartType>& type : types->getChartTypes()) {
        Reference<css::chart2::XDataSeriesContainer> container(type, UNO_QUERY);
        if (!container.is()) continue;
        for (const Reference<css::chart2::XDataSeries>& series :
             container->getDataSeries()) {
          if (!series.is() || look.refused()) continue;
          Reference<css::beans::XPropertySet> series_props(series, UNO_QUERY);
          check_chart_points(series_props, "a chart series", look);
          css::uno::Sequence<sal_Int32> points;
          if (series_props.is()) {
            series_props->getPropertyValue("AttributedDataPoints") >>= points;
          }
          for (sal_Int32 point : points) {
            check_chart_points(series->getDataPointByIndex(point),
                               "a chart data point", look);
          }
          Reference<css::chart2::data::XDataSource> source(series, UNO_QUERY);
          if (source.is()) {
            for (const Reference<css::chart2::data::XLabeledDataSequence>& labeled :
                 source->getDataSequences()) {
              if (!labeled.is()) continue;
              check_chart_sequence(labeled->getLabel(), "a chart series name",
                                   look);
              check_chart_sequence(labeled->getValues(), "a chart value", look);
            }
          }
          Reference<css::chart2::XRegressionCurveContainer> curves(series,
                                                                   UNO_QUERY);
          if (!curves.is()) continue;
          for (const Reference<css::chart2::XRegressionCurve>& curve :
               curves->getRegressionCurves()) {
            if (!curve.is()) continue;
            redact_property(Reference<css::beans::XPropertySet>(curve, UNO_QUERY),
                            "CurveName", "a chart trend line", look);
          }
        }
      }
    }
  }
  // Row and column labels of the chart's own data table, used or not.
  Reference<css::chart::XComplexDescriptionAccess> data(chart->getDataProvider(),
                                                        UNO_QUERY);
  if (data.is()) {
    for (const auto& labels : {data->getComplexRowDescriptions(),
                               data->getComplexColumnDescriptions()}) {
      for (const css::uno::Sequence<rtl::OUString>& label : labels) {
        for (const rtl::OUString& part : label) {
          redact_value(part, "the chart's data table", look);
        }
      }
    }
  }
  Reference<css::drawing::XDrawPageSupplier> page(chart, UNO_QUERY);
  if (page.is()) {
    redact_shapes(Reference<css::container::XIndexAccess>(page->getDrawPage(),
                                                          UNO_QUERY),
                  0, look);
  }
}

// Looks at every sheet of an embedded spreadsheet, whichever one the
// object shows: sheet names, every non-empty cell's shown text and
// formula, cell comments, and each sheet's draw page (shapes, controls,
// and objects nested there).
void inspect_spreadsheet(const Reference<css::sheet::XSpreadsheetDocument>& doc,
                         RedactWalk& look) {
  Reference<css::container::XIndexAccess> sheets(doc->getSheets(), UNO_QUERY);
  if (!sheets.is()) {
    look.refuse("a spreadsheet whose sheets cannot be walked");
    return;
  }
  for (sal_Int32 s = 0; s < sheets->getCount() && !look.refused(); s++) {
    css::uno::Any element = sheets->getByIndex(s);
    check_name(Reference<css::container::XNamed>(element, UNO_QUERY), "a sheet",
               look);
    Reference<css::sheet::XCellRangesQuery> query(element, UNO_QUERY);
    if (!query.is()) {
      look.refuse("a sheet whose cells cannot be walked");
      return;
    }
    Reference<css::sheet::XSheetCellRanges> filled = query->queryContentCells(
        static_cast<sal_Int16>(
            css::sheet::CellFlags::VALUE | css::sheet::CellFlags::DATETIME
            | css::sheet::CellFlags::STRING | css::sheet::CellFlags::FORMULA));
    Reference<css::container::XEnumerationAccess> cells(
        filled.is() ? filled->getCells()
                    : Reference<css::container::XEnumerationAccess>());
    Reference<css::container::XEnumeration> it =
        cells.is() ? cells->createEnumeration()
                   : Reference<css::container::XEnumeration>();
    while (it.is() && it->hasMoreElements() && !look.refused()) {
      css::uno::Any cell_element = it->nextElement();
      Reference<css::table::XCell> cell(cell_element, UNO_QUERY);
      if (cell.is()) redact_value(cell->getFormula(), "a sheet cell", look);
      redact_text(Reference<css::text::XText>(cell_element, UNO_QUERY),
                  "a sheet cell", 1, look);
    }
    Reference<css::sheet::XSheetAnnotationsSupplier> notes(element, UNO_QUERY);
    Reference<css::container::XIndexAccess> annotations(
        notes.is() ? notes->getAnnotations()
                   : Reference<css::sheet::XSheetAnnotations>(),
        UNO_QUERY);
    for (sal_Int32 i = 0; annotations.is() && i < annotations->getCount(); i++) {
      Reference<css::sheet::XSheetAnnotation> note(annotations->getByIndex(i),
                                                   UNO_QUERY);
      if (!note.is()) continue;
      Reference<css::text::XSimpleText> note_text(note, UNO_QUERY);
      if (!note_text.is()) {
        look.refuse("a cell comment whose text cannot be read");
        return;
      }
      redact_value(note_text->getString(), "a cell comment", look);
      redact_value(note->getAuthor(), "a cell comment's author", look);
    }
    Reference<css::drawing::XDrawPageSupplier> page(element, UNO_QUERY);
    if (page.is()) {
      Reference<css::drawing::XDrawPage> draw_page = page->getDrawPage();
      redact_shapes(Reference<css::container::XIndexAccess>(draw_page, UNO_QUERY),
                    0, look);
      redact_forms(draw_page, look);
    }
  }
}

// Looks into one embedded model for the redacted text, by what it is: a
// chart or spreadsheet through inspect_chart and inspect_spreadsheet, a
// formula through its source text, a text document through the full text
// walk (its own embedded objects included), and a drawing or
// presentation through the shapes and forms of its pages and master
// pages. An object with no office model (a foreign OLE object) or of any
// other kind cannot be inspected and refuses. Never rewrites: look is a
// verify walk.
void inspect_model(const Reference<css::frame::XModel>& inner, RedactWalk& look,
                   int depth) {
  if (depth > kMaxRedactionNesting) {
    look.refuse("embedded objects nest deeper than "
                + std::to_string(kMaxRedactionNesting) + " levels");
    return;
  }
  if (!inner.is()) {
    look.refuse("a foreign object, whose content cannot be inspected");
    return;
  }
  const int outer_depth = look.ole_depth;
  look.ole_depth = depth;
  Reference<css::chart2::XChartDocument> chart(inner, UNO_QUERY);
  Reference<css::sheet::XSpreadsheetDocument> sheet(inner, UNO_QUERY);
  Reference<css::text::XTextDocument> text_doc(inner, UNO_QUERY);
  Reference<css::drawing::XDrawPagesSupplier> drawing(inner, UNO_QUERY);
  Reference<css::lang::XServiceInfo> info(inner, UNO_QUERY);
  if (chart.is()) {
    inspect_chart(chart, look);
  } else if (sheet.is()) {
    inspect_spreadsheet(sheet, look);
  } else if (info.is()
             && info->supportsService("com.sun.star.formula.FormulaProperties")) {
    Reference<css::beans::XPropertySet> props(inner, UNO_QUERY);
    rtl::OUString formula;
    if (!props.is() || !(props->getPropertyValue("Formula") >>= formula)) {
      look.refuse("a formula whose source cannot be read");
    } else {
      redact_value(formula, "a formula", look);
    }
  } else if (text_doc.is()) {
    // The text walk reaches its objects through check_embedded_objects.
    look.ole_depth = -1;
    redact_text_document(text_doc, inner, look);
    check_embedded_objects(text_doc, look, depth + 1);
  } else if (drawing.is()) {
    Reference<css::container::XIndexAccess> pages(drawing->getDrawPages(),
                                                  UNO_QUERY);
    Reference<css::drawing::XMasterPagesSupplier> masters(inner, UNO_QUERY);
    Reference<css::container::XIndexAccess> master_pages(
        masters.is() ? masters->getMasterPages()
                     : Reference<css::drawing::XDrawPages>(),
        UNO_QUERY);
    for (const Reference<css::container::XIndexAccess>& set : {pages, master_pages}) {
      for (sal_Int32 i = 0; set.is() && i < set->getCount(); i++) {
        css::uno::Any page = set->getByIndex(i);
        redact_shapes(Reference<css::container::XIndexAccess>(page, UNO_QUERY),
                      0, look);
        redact_forms(Reference<css::uno::XInterface>(page, UNO_QUERY), look);
      }
    }
  } else {
    look.refuse("an object of a kind the service cannot inspect");
  }
  look.ole_depth = outer_depth;
}

// An object nested on a draw page inside an embedded object.
void inspect_ole_shape(const Reference<css::beans::XPropertySet>& props,
                       RedactWalk& walk) {
  Reference<css::frame::XModel> inner;
  try {
    props->getPropertyValue("Model") >>= inner;
  } catch (const css::beans::UnknownPropertyException&) {
    // Expected probe result: a foreign OLE shape has no inner model;
    // inspect_model refuses it.
  }
  inspect_model(inner, walk, walk.ole_depth + 1);
}

// Inspects one embedded object of the host. The object's picture on the
// host page is drawn from its content, which the service does not
// rewrite, so any occurrence, and any failure to look, refuses.
void inspect_embedded_model(const Reference<css::frame::XModel>& inner,
                            const std::string& label, RedactWalk& host,
                            int depth) {
  RedactWalk look{.mode = RedactWalk::Mode::kVerify,
                  .pieces = host.pieces,
                  .warner = host.warner,
                  .refusal = {},
                  .ole_depth = -1};
  const size_t warnings_before = host.warner->count();
  try {
    inspect_model(inner, look, depth);
  } catch (const css::uno::Exception&) {
    host.refuse(label + " cannot be inspected for the redacted text");
    return;
  }
  if (look.refused()) {
    // The inner walk's reason, without its own "redaction refused: ".
    const std::string prefix = "redaction refused: ";
    std::string inner_reason = look.refusal;
    if (inner_reason.starts_with(prefix)) inner_reason.erase(0, prefix.size());
    host.refuse(label + ", which the service cannot rewrite, fails the "
                "check: " + inner_reason);
    return;
  }
  // A walk that reported a problem did not read everything it looked at.
  if (host.warner->count() != warnings_before) {
    host.refuse(label + " cannot be inspected for the redacted text");
  }
}

// Embedded objects render their own content, which the text walks cannot
// reach and the service does not rewrite. Each is inspected through
// inspect_embedded_model; one that cannot be opened refuses.
void check_embedded_objects(const Reference<css::text::XTextDocument>& text_doc,
                            RedactWalk& walk, int depth) {
  if (depth > kMaxRedactionNesting) {
    walk.refuse("embedded objects nest deeper than "
                + std::to_string(kMaxRedactionNesting) + " levels");
    return;
  }
  Reference<css::text::XTextEmbeddedObjectsSupplier> supplier(text_doc, UNO_QUERY);
  if (!supplier.is()) return;
  Reference<css::container::XIndexAccess> objects(supplier->getEmbeddedObjects(),
                                                  UNO_QUERY);
  for (sal_Int32 i = 0; objects.is() && i < objects->getCount(); i++) {
    if (walk.refused()) return;
    const std::string label = "embedded object " + std::to_string(i);
    Reference<css::document::XEmbeddedObjectSupplier> object(
        objects->getByIndex(i), UNO_QUERY);
    check_name(Reference<css::container::XNamed>(object, UNO_QUERY), label,
               walk);
    Reference<css::frame::XModel> inner;
    try {
      if (object.is()) inner.set(object->getEmbeddedObject(), UNO_QUERY);
    } catch (const css::uno::Exception&) {
      walk.refuse(label + " cannot be opened for the redaction check");
      return;
    }
    inspect_embedded_model(inner, label, walk, depth);
  }
}

// The annotation text space of a text document: each body paragraph's
// start offset and text, built by the same fill_runs portion walk the
// Paragraph events come from.
struct AnnotationParagraph {
  std::int64_t start = 0;
  CodePoints text;
};

std::int64_t annotation_space(const Reference<css::text::XTextDocument>& text_doc,
                              std::vector<AnnotationParagraph>* paragraphs,
                              Warner& warner) {
  Reference<css::container::XEnumerationAccess> body(text_doc->getText(),
                                                     UNO_QUERY);
  if (!body.is()) return 0;
  std::int64_t offset = 0;
  Reference<css::container::XEnumeration> elements = body->createEnumeration();
  while (elements->hasMoreElements()) {
    css::uno::Any element = elements->nextElement();
    if (Reference<css::text::XTextTable>(element, UNO_QUERY).is()) continue;
    Reference<css::container::XEnumerationAccess> paragraph(element, UNO_QUERY);
    if (!paragraph.is() || !Reference<css::text::XTextRange>(element, UNO_QUERY).is()) {
      continue;
    }
    AnnotationParagraph out;
    out.start = offset;
    google::protobuf::RepeatedPtrField<officev1::TextRun> runs;
    fill_runs(paragraph, "redaction paragraph", &runs, &offset, nullptr, warner);
    for (const officev1::TextRun& run : runs) {
      out.text += code_points(oustring(run.text()));
    }
    paragraphs->push_back(std::move(out));
    offset += 1;  // The newline after each body paragraph.
  }
  return offset;
}

}  // namespace

void set_source_name(const std::string& name) { source_name_storage() = name; }

void apply_document_options(const RenderOptions& options,
                            std::vector<std::string>* warnings) {
  Warner warner(warnings);
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) return;
    Reference<css::frame::XModel> model = find_loaded_model(context);
    if (!model.is()) return;
    pin_file_name_fields(model, warner);
    apply_tracked_changes(model, context, options.tracked_changes, warner);
    apply_form_fills(model, options.form_values, warner);
  } catch (const css::uno::Exception& error) {
    warner.warn("apply document options", error);
  }
}

RedactionResult apply_redaction(const RenderOptions& options,
                                std::vector<std::string>* warnings) {
  RedactionResult result;
  std::vector<std::pair<std::int64_t, std::int64_t>> spans;
  for (const auto& span : options.redact_spans) {
    // Zero-length spans are ignored by contract.
    if (span.second > span.first) spans.push_back(span);
  }
  if (spans.empty()) return result;
  auto refuse = [&](const std::string& why) {
    result.ok = false;
    result.refusal = why;
    return result;
  };
  Warner warner(warnings);
  // The step under way, for a refusal when the office core throws: the
  // exception's own message may quote document text, so it is not used.
  const char* stage = "finding the loaded document";
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    Reference<css::frame::XModel> model =
        context.is() ? find_loaded_model(context) : Reference<css::frame::XModel>();
    if (!model.is()) {
      return refuse("redaction refused: the loaded document is not reachable");
    }
    Reference<css::text::XTextDocument> text_doc(model, UNO_QUERY);
    if (!text_doc.is()) {
      return refuse(
          "redaction refused: redact_spans address the annotation text space, "
          "which only text documents carry; this document has none");
    }

    // Resolve the spans against the document as loaded, before anything
    // is rewritten. A span may cross paragraphs; its text splits into one
    // piece per paragraph, trimmed of surrounding whitespace, so the same
    // name is found again where punctuation or a line end follows it.
    stage = "reading the annotation text space";
    std::vector<AnnotationParagraph> paragraphs;
    const size_t warnings_before = warner.count();
    const std::int64_t length = annotation_space(text_doc, &paragraphs, warner);
    // A portion the walk lost would shift every later offset, so a span
    // could land on other text than the caller addressed.
    if (warner.count() != warnings_before) {
      return refuse("redaction refused: the annotation text space could not "
                    "be read in full, so the spans cannot be placed");
    }
    std::vector<CodePoints> pieces;
    for (const auto& [first, last] : spans) {
      if (last > length) {
        return refuse("redaction refused: span [" + std::to_string(first) + ", "
                      + std::to_string(last) + ") ends past the document's "
                      "annotation text space (" + std::to_string(length)
                      + " code points)");
      }
      for (const AnnotationParagraph& paragraph : paragraphs) {
        const std::int64_t end =
            paragraph.start + static_cast<std::int64_t>(paragraph.text.size());
        const std::int64_t lo = std::max(first, paragraph.start);
        const std::int64_t hi = std::min(last, end);
        if (lo >= hi) continue;
        CodePoints piece = paragraph.text.substr(
            static_cast<size_t>(lo - paragraph.start), static_cast<size_t>(hi - lo));
        while (!piece.empty() && redaction_space(piece.front())) piece.erase(0, 1);
        while (!piece.empty() && redaction_space(piece.back())) piece.pop_back();
        const bool glyphs_only = std::ranges::all_of(
            piece, [](char32_t c) { return c == kRedactionGlyph; });
        if (piece.empty() || glyphs_only || std::ranges::contains(pieces, piece)) {
          continue;
        }
        pieces.push_back(std::move(piece));
      }
    }
    if (pieces.empty()) return result;
    for (const CodePoints& piece : pieces) {
      result.redacted.push_back(utf8(from_code_points(piece)));
    }

    stage = "preparing the document for the rewrite";
    // Rewrites must not leave the original behind as tracked deletions,
    // and protected sections (generated indexes among them) must yield.
    Reference<css::beans::XPropertySet> document_props(model, UNO_QUERY);
    if (document_props.is()) {
      document_props->setPropertyValue("RecordChanges", css::uno::Any(false));
    }
    Reference<css::text::XTextSectionsSupplier> sections_supplier(text_doc,
                                                                  UNO_QUERY);
    if (sections_supplier.is()) {
      Reference<css::container::XNameAccess> sections =
          sections_supplier->getTextSections();
      for (const rtl::OUString& name : sections->getElementNames()) {
        Reference<css::beans::XPropertySet> section(sections->getByName(name),
                                                    UNO_QUERY);
        try {
          if (section.is()) {
            section->setPropertyValue("IsProtected", css::uno::Any(false));
          }
        } catch (const css::uno::Exception& error) {
          warner.warn("redaction: a protected section stayed protected", error);
        }
      }
    }

    stage = "the rewrite";
    RedactWalk rewrite{.mode = RedactWalk::Mode::kReplace,
                       .pieces = pieces,
                       .warner = &warner,
                       .refusal = {},
                       .ole_depth = -1};
    check_embedded_objects(text_doc, rewrite);
    if (!rewrite.refused()) redact_text_document(text_doc, model, rewrite);
    if (rewrite.refused()) return refuse(rewrite.refusal);

    stage = "the verify walk";
    RedactWalk verify{.mode = RedactWalk::Mode::kVerify,
                      .pieces = pieces,
                      .warner = &warner,
                      .refusal = {},
                      .ole_depth = -1};
    const size_t verify_warnings = warner.count();
    redact_text_document(text_doc, model, verify);
    if (verify.refused()) return refuse(verify.refusal);
    if (warner.count() != verify_warnings) {
      return refuse("redaction refused: the verify walk could not read the "
                    "whole redacted document");
    }

    // Last, the document as every typed event would carry it, every part
    // a response can carry except page images and line rectangles, which
    // hold no text; image events too, so the guard on outgoing events
    // should never find what this missed after pages have streamed.
    stage = "the typed-content check";
    PartSelection scan;
    scan.all = false;
    scan.mask = ~0u & ~1u;
    for (int part : {officev1::DOCUMENT_PART_PAGES, officev1::DOCUMENT_PART_LINE_RECTS,
                     officev1::DOCUMENT_PART_CELL_LINE_RECTS}) {
      scan.mask &= ~(1u << part);
    }
    std::string where;
    // Its own warnings stay out of the response; only their count matters.
    std::vector<std::string> scan_warnings;
    Warner scan_warner(&scan_warnings);
    const bool finished = emit_typed_content_with(
        scan, nullptr,
        [&](const google::protobuf::MessageLite& event) {
          return !carries_redacted_text(event, result.redacted, &where);
        },
        scan_warner);
    if (!where.empty()) {
      return refuse("redaction refused: redacted text remains in typed content ("
                    + where + ") that the office core does not let the service "
                    "rewrite");
    }
    // A walk that stopped early or reported any problem checked only part
    // of the document.
    if (!finished || scan_warner.count() > 0) {
      return refuse("redaction refused: the typed-content check of the "
                    "redacted document could not complete");
    }
  } catch (const css::uno::Exception&) {
    return refuse(std::string("redaction refused: the office core failed during ")
                  + stage);
  }
  return result;
}

bool carries_redacted_text(const google::protobuf::MessageLite& message,
                           const std::vector<std::string>& redacted,
                           std::string* where) {
  if (redacted.empty()) return false;
  const auto* full = dynamic_cast<const google::protobuf::Message*>(&message);
  if (full == nullptr) {
    // Every event this worker emits is a full message; one that is not
    // cannot be checked and does not go out.
    *where = "an event the check cannot read";
    return true;
  }
  return find_redacted(*full, redacted, "", where);
}

std::string mask_redacted(const std::string& text,
                          const std::vector<std::string>& redacted) {
  if (redacted.empty()) return text;
  std::vector<CodePoints> pieces;
  for (const std::string& piece : redacted) {
    pieces.push_back(code_points(oustring(piece)));
  }
  CodePoints points = code_points(oustring(text));
  Intervals intervals = covered_intervals(points, pieces);
  if (intervals.empty()) return text;
  return utf8(from_code_points(masked(std::move(points), intervals)));
}

std::string export_page_svg_uno(int page_number) {
  if (page_number < 1) return {};
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) return {};
    Reference<css::frame::XModel> model = find_loaded_model(context);
    if (!model.is()) return {};
    Reference<css::frame::XStorable> storable(model, UNO_QUERY);
    if (!storable.is()) return {};
    std::string bytes;
    rtl::Reference<PdfChunkSink> sink(new PdfChunkSink(
        1 << 20, [&](std::string&& chunk) {
          bytes += std::move(chunk);
          return true;
        }));
    css::uno::Sequence<css::beans::PropertyValue> filter_data(1);
    filter_data.getArray()[0].Name = "PageNumber";
    filter_data.getArray()[0].Value <<= static_cast<sal_Int32>(page_number);
    css::uno::Sequence<css::beans::PropertyValue> descriptor(3);
    css::beans::PropertyValue* props = descriptor.getArray();
    props[0].Name = "FilterName";
    props[0].Value <<= oustring("draw_svg_Export");
    props[1].Name = "OutputStream";
    props[1].Value <<= Reference<css::io::XOutputStream>(sink.get());
    props[2].Name = "FilterData";
    props[2].Value <<= filter_data;
    storable->storeToURL(oustring("private:stream"), descriptor);
    if (bytes.contains("<svg")) return bytes;
  } catch (const css::uno::Exception&) {
    // Document classes without an SVG store filter (Writer among them)
    // throw here on every page; the caller's raster fallback handles it
    // and reports the downgrade once per document.
    return {};
  }
  return {};
}

void describe_parts(std::vector<PartLayout>* parts,
                    std::vector<std::string>* warnings) {
  if (parts == nullptr) return;
  parts->clear();
  Warner warner(warnings);
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) return;
    Reference<css::frame::XModel> model = find_loaded_model(context);
    if (!model.is()) return;
    Reference<css::sheet::XSpreadsheetDocument> calc(model, UNO_QUERY);
    if (calc.is()) {
      Reference<css::container::XIndexAccess> sheets(calc->getSheets(),
                                                     UNO_QUERY);
      if (!sheets.is()) return;
      for (sal_Int32 i = 0; i < sheets->getCount(); i++) {
        PartLayout layout;
        Reference<css::sheet::XSpreadsheet> sheet(sheets->getByIndex(i),
                                                  UNO_QUERY);
        Reference<css::beans::XPropertySet> props(sheet, UNO_QUERY);
        if (props.is()) {
          try {
            props->getPropertyValue("IsVisible") >>= layout.visible;
          } catch (const css::uno::Exception&) {
          }
        }
        try {
          Reference<css::sheet::XSheetCellCursor> cursor =
              sheet->createCursor();
          Reference<css::sheet::XUsedAreaCursor> area(cursor, UNO_QUERY);
          if (area.is()) {
            area->gotoStartOfUsedArea(false);
            area->gotoEndOfUsedArea(true);
            Reference<css::sheet::XCellRangeAddressable> addressable(
                cursor, UNO_QUERY);
            css::table::CellRangeAddress used;
            if (addressable.is()) used = addressable->getRangeAddress();
            Reference<css::table::XColumnRowRange> col_rows(sheet, UNO_QUERY);
            Reference<css::table::XTableColumns> columns =
                col_rows.is() ? col_rows->getColumns()
                              : Reference<css::table::XTableColumns>();
            Reference<css::table::XTableRows> rows =
                col_rows.is() ? col_rows->getRows()
                              : Reference<css::table::XTableRows>();
            // Tile coordinates give hidden columns and rows no space, so
            // both the origin offset and the used size sum only the
            // visible ones. The columns before the used range are the
            // origin: a used range that starts away from A1 must shift the
            // paint rectangle, not just shrink it.
            long x = 0;
            long width = 0;
            for (sal_Int32 c = 0; columns.is() && c <= used.EndColumn; c++) {
              Reference<css::beans::XPropertySet> col(columns->getByIndex(c),
                                                      UNO_QUERY);
              if (!col.is()) continue;
              bool visible = true;
              col->getPropertyValue("IsVisible") >>= visible;
              if (!visible) continue;
              sal_Int32 hmm = 0;
              col->getPropertyValue("Width") >>= hmm;
              if (c < used.StartColumn) {
                x += hundredth_mm_to_twips(hmm);
              } else {
                width += hundredth_mm_to_twips(hmm);
              }
            }
            long y = 0;
            long height = 0;
            for (sal_Int32 r = 0; rows.is() && r <= used.EndRow; r++) {
              Reference<css::beans::XPropertySet> row(rows->getByIndex(r),
                                                      UNO_QUERY);
              if (!row.is()) continue;
              bool visible = true;
              row->getPropertyValue("IsVisible") >>= visible;
              if (!visible) continue;
              sal_Int32 hmm = 0;
              row->getPropertyValue("Height") >>= hmm;
              if (r < used.StartRow) {
                y += hundredth_mm_to_twips(hmm);
              } else {
                height += hundredth_mm_to_twips(hmm);
              }
            }
            layout.used_x = x;
            layout.used_y = y;
            layout.used_width = width;
            layout.used_height = height;
          }
        } catch (const css::uno::Exception& error) {
          warner.warn("used range for sheet " + std::to_string(i), error);
        }
        parts->push_back(layout);
      }
      return;
    }
    Reference<css::drawing::XDrawPagesSupplier> slides(model, UNO_QUERY);
    if (!slides.is()) return;
    Reference<css::drawing::XDrawPages> list = slides->getDrawPages();
    if (!list.is()) return;
    for (sal_Int32 i = 0; i < list->getCount(); i++) {
      PartLayout layout;
      Reference<css::beans::XPropertySet> props(list->getByIndex(i), UNO_QUERY);
      if (props.is()) {
        try {
          props->getPropertyValue("Visible") >>= layout.visible;
        } catch (const css::uno::Exception&) {
          try {
            bool hidden = false;
            props->getPropertyValue("Hidden") >>= hidden;
            layout.visible = !hidden;
          } catch (const css::uno::Exception&) {
          }
        }
      }
      parts->push_back(layout);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("describe parts", error);
  }
}

void describe_page_styles(std::vector<std::string>* styles,
                          std::vector<std::string>* warnings) {
  if (styles == nullptr) return;
  styles->clear();
  Warner warner(warnings);
  try {
    Reference<css::uno::XComponentContext> context = process_context();
    if (!context.is()) return;
    Reference<css::frame::XModel> model = find_loaded_model(context);
    if (!model.is()) return;

    Reference<css::sheet::XSpreadsheetDocument> calc(model, UNO_QUERY);
    if (calc.is()) {
      // A sheet's page style is a sheet property, so no cursor walk is
      // needed; the part ordinal is the page ordinal.
      Reference<css::container::XIndexAccess> sheets(calc->getSheets(),
                                                     UNO_QUERY);
      if (!sheets.is()) return;
      for (sal_Int32 i = 0; i < sheets->getCount(); i++) {
        std::string name;
        try {
          Reference<css::beans::XPropertySet> props(sheets->getByIndex(i),
                                                    UNO_QUERY);
          rtl::OUString style;
          if (props.is()) props->getPropertyValue("PageStyle") >>= style;
          name = utf8(style);
        } catch (const css::uno::Exception& error) {
          warner.warn("page style of sheet " + std::to_string(i), error);
        }
        styles->push_back(name);
      }
      return;
    }

    Reference<css::text::XTextViewCursorSupplier> supplier(
        model->getCurrentController(), UNO_QUERY);
    Reference<css::text::XTextDocument> writer(model, UNO_QUERY);
    if (writer.is() && supplier.is()) {
      // The view cursor is the only thing that knows which style the layout
      // put on a page: a page style is a property of the laid-out page, not
      // of the style declaration, and the same declaration can carry any
      // number of pages. Walking the page cursor page by page and reading
      // the cursor's own PageStyleName reports each page's style directly.
      Reference<css::text::XTextViewCursor> cursor = supplier->getViewCursor();
      Reference<css::text::XPageCursor> pages(cursor, UNO_QUERY);
      Reference<css::beans::XPropertySet> props(cursor, UNO_QUERY);
      if (!pages.is() || !props.is()) return;
      // The cursor is shared with the caret and line measurements that run
      // later, so the walk restores where it started.
      Reference<css::text::XTextRange> resume = cursor->getStart();
      int count = 0;
      try {
        if (pages->jumpToLastPage()) count = pages->getPage();
      } catch (const css::uno::Exception& error) {
        warner.warn("page count of the laid-out document", error);
      }
      for (int page = 1; page <= count; page++) {
        std::string name;
        try {
          if (pages->jumpToPage(static_cast<sal_Int16>(page))) {
            rtl::OUString style;
            props->getPropertyValue("PageStyleName") >>= style;
            name = utf8(style);
          }
        } catch (const css::uno::Exception& error) {
          warner.warn("page style of page " + std::to_string(page), error);
        }
        styles->push_back(name);
      }
      if (resume.is()) {
        try {
          cursor->gotoRange(resume, false);
        } catch (const css::uno::Exception& error) {
          warner.warn("view cursor restore after the page style walk", error);
        }
      }
      return;
    }

    Reference<css::drawing::XDrawPagesSupplier> draw(model, UNO_QUERY);
    if (!draw.is()) return;
    Reference<css::drawing::XDrawPages> list = draw->getDrawPages();
    if (!list.is()) return;
    for (sal_Int32 i = 0; i < list->getCount(); i++) {
      std::string name;
      try {
        Reference<css::drawing::XMasterPageTarget> target(list->getByIndex(i),
                                                          UNO_QUERY);
        Reference<css::container::XNamed> master(
            target.is() ? target->getMasterPage()
                        : Reference<css::drawing::XDrawPage>(),
            UNO_QUERY);
        if (master.is()) name = utf8(master->getName());
      } catch (const css::uno::Exception& error) {
        warner.warn("master of page " + std::to_string(i), error);
      }
      styles->push_back(name);
    }
  } catch (const css::uno::Exception& error) {
    warner.warn("describe page styles", error);
  }
}

}  // namespace grlibre
