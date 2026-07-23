// grlibre-worker: renders exactly one document, then exits. The parent
// server streams the document over stdin and reads framed response events
// off stdout; isolation comes from this process boundary. A crash or hang
// in the office core dies here, not in the server.

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "lok_engine.h"

namespace {

// Decodes the argv parts token: "all" selects every part, otherwise the
// token is DocumentPart numeric values joined by commas (for example "2,3").
// Values outside (0, 32) are ignored, matching the UNSPECIFIED rule.
grlibre::PartSelection parse_parts(const std::string& token) {
  grlibre::PartSelection parts;
  if (token == "all") return parts;
  parts.all = false;
  size_t pos = 0;
  while (pos <= token.size()) {
    size_t comma = token.find(',', pos);
    if (comma == std::string::npos) comma = token.size();
    int value = std::atoi(token.substr(pos, comma - pos).c_str());
    if (value > 0 && value < 32) parts.mask |= 1u << value;
    pos = comma + 1;
  }
  return parts;
}

std::string read_all_stdin() {
  std::string bytes;
  char buffer[1 << 16];
  ssize_t got;
  while ((got = ::read(STDIN_FILENO, buffer, sizeof buffer)) > 0) {
    bytes.append(buffer, static_cast<size_t>(got));
  }
  return bytes;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 7 && argc != 8) {
    std::cerr << "usage: grlibre-worker <pages|pdf> <extension> <dpi> "
                 "<max_side_px> <work_dir> <install_path> [parts]\n";
    return grlibre::kExitRenderFailure;
  }
  grlibre::RenderOptions options;
  options.mode = argv[1];
  options.extension = argv[2];
  options.dpi = std::atoi(argv[3]);
  options.max_side_px = std::atoi(argv[4]);
  options.work_dir = argv[5];
  options.install_path = argv[6];
  // Absent token means every part, so older callers keep full output.
  if (argc == 8) options.parts = parse_parts(argv[7]);

  std::string document = read_all_stdin();
  if (document.empty()) {
    std::cerr << "grlibre-worker: no document bytes on stdin\n";
    return grlibre::kExitLoadFailure;
  }
  options.input_bytes = static_cast<long>(document.size());
  options.doc_path = options.work_dir + "/doc." + options.extension;
  {
    std::ofstream out(options.doc_path, std::ios::binary);
    out.write(document.data(), static_cast<std::streamsize>(document.size()));
    if (!out) {
      std::cerr << "grlibre-worker: cannot write " << options.doc_path << "\n";
      return grlibre::kExitRenderFailure;
    }
  }
  std::filesystem::create_directories(options.work_dir + "/profile");

  std::string error;
  int code = grlibre::run_render(options, STDOUT_FILENO, &error);
  if (code != grlibre::kExitOk) {
    std::cerr << "grlibre-worker: " << error << "\n";
  }
  return code;
}
