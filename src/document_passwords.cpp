#include "document_passwords.h"

#include <cstdint>

namespace grlibre {

std::string encode_passwords(const std::vector<std::string>& passwords) {
  std::string out;
  for (const std::string& password : passwords) {
    const auto size = static_cast<std::uint32_t>(password.size());
    for (int shift = 0; shift < 32; shift += 8) {
      out.push_back(static_cast<char>((size >> shift) & 0xFF));
    }
    out.append(password);
  }
  return out;
}

std::optional<std::vector<std::string>> decode_passwords(std::string_view bytes) {
  std::vector<std::string> passwords;
  while (!bytes.empty()) {
    if (bytes.size() < 4 || passwords.size() == kMaxDocumentPasswords) return std::nullopt;
    std::uint32_t size = 0;
    for (int index = 0; index < 4; ++index) {
      size |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << (8 * index);
    }
    bytes.remove_prefix(4);
    if (size > kMaxDocumentPasswordBytes || size > bytes.size()) return std::nullopt;
    passwords.emplace_back(bytes.substr(0, size));
    bytes.remove_prefix(size);
  }
  return passwords;
}

std::string password_attempt_clause(size_t tried) {
  if (tried == 0) return "no password was supplied";
  if (tried == 1) return "the supplied password did not open it";
  return "none of the " + std::to_string(tried) + " supplied passwords opened it";
}

}  // namespace grlibre
