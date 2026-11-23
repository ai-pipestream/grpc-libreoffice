#ifndef GRLIBRE_PDF_PAGE_RANGE_H
#define GRLIBRE_PDF_PAGE_RANGE_H

#include <string>

namespace grlibre {

// The PDF export filter's PageRange for a request's 1-based inclusive
// first and last page, where 0 means "from the start" and "through the
// end". Empty when neither bound is set: the whole document.
inline std::string pdf_page_range(int first_page, int last_page) {
  if (first_page <= 0 && last_page <= 0) return std::string();
  const int first = first_page > 0 ? first_page : 1;
  const int last = last_page > 0 ? last_page : 9999;
  return std::to_string(first) + "-" + std::to_string(last);
}

}  // namespace grlibre

#endif  // GRLIBRE_PDF_PAGE_RANGE_H
