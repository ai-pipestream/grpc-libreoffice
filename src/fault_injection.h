#ifndef GRLIBRE_FAULT_INJECTION_H
#define GRLIBRE_FAULT_INJECTION_H

#include <cstdlib>
#include <string_view>

namespace grlibre {

// Test-only fault points. The render tests run the shipped worker binary
// against a real LibreOffice, and some of the failures the worker must
// report cannot be produced from a document on demand: an office core that
// throws out of the typed-content walk, or one whose UNO context cannot be
// reached. GRLIBRE_TEST_FAULT names the fault points to trip, comma
// separated; each call site names the one point it stands for. The variable
// is unset in every deployment, and a set one only ever makes a render fail.
inline bool fault_injected(std::string_view point) {
  const char* raw = std::getenv("GRLIBRE_TEST_FAULT");
  if (raw == nullptr) return false;
  std::string_view rest(raw);
  while (!rest.empty()) {
    const size_t comma = rest.find(',');
    if (rest.substr(0, comma) == point) return true;
    if (comma == std::string_view::npos) break;
    rest.remove_prefix(comma + 1);
  }
  return false;
}

}  // namespace grlibre

#endif
