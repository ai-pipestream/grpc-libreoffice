#ifndef GRLIBRE_PDF_PAGE_RANGE_H
#define GRLIBRE_PDF_PAGE_RANGE_H

#include <string>

namespace grlibre {

// The PDF export filter's PageRange for a request's 1-based inclusive
// first and last page, where 0 means "from the start" and "through the
// end". Empty when neither bound is set: the whole document. A range with
// no last page is written open-ended ("5-"), which the filter reads as
// through the document's real last page.
inline std::string pdf_page_range(int first_page, int last_page) {
  if (first_page <= 0 && last_page <= 0) return std::string();
  const int first = first_page > 0 ? first_page : 1;
  if (last_page <= 0) return std::to_string(first) + "-";
  return std::to_string(first) + "-" + std::to_string(last_page);
}

}  // namespace grlibre

#endif  // GRLIBRE_PDF_PAGE_RANGE_H
