#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace grlibre {

// Candidate passwords for an encrypted document. The caller sends them in
// the call's initial metadata, never in a request message, so nothing that
// logs or recaptures requests carries them: one candidate per entry of
// `document-password` (printable ASCII) or `document-password-bin` (any
// bytes; gRPC base64-encodes it on the wire), the plain key's candidates
// first, each key's in the order sent. gRParse forwards its own callers'
// candidates this way. The service hands them to the worker through a file
// in the worker's tmpfs work dir, which the worker unlinks as soon as it
// has read it; nothing logs them, and errors say how many were tried,
// never which.
inline constexpr std::string_view kDocumentPasswordKey = "document-password";
inline constexpr std::string_view kDocumentPasswordBinKey = "document-password-bin";
inline constexpr size_t kMaxDocumentPasswords = 16;
inline constexpr size_t kMaxDocumentPasswordBytes = 1024;

// The work-dir file the worker reads them from.
inline constexpr std::string_view kPasswordsFileName = "passwords";

// The file's bytes: each candidate as a little-endian u32 length and its
// bytes, so any byte sequence survives.
std::string encode_passwords(const std::vector<std::string>& passwords);

// The candidates back, or nullopt when the bytes are not a whole encoding
// or exceed the bounds above.
std::optional<std::vector<std::string>> decode_passwords(std::string_view bytes);

// How an error names the attempt: how many candidates, never which.
std::string password_attempt_clause(size_t tried);

}  // namespace grlibre
