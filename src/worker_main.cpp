// grlibre-worker: renders exactly one document, then exits. The parent
// server streams the document over stdin and reads framed response events
// off stdout; isolation comes from this process boundary. A crash or hang
// in the office core dies here, not in the server.

#include <linux/magic.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <cerrno>
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

// The uploaded document must never reach disk. Everything the worker or the
// office core writes (the document, the profile, the core's temp spills)
// lands under the work dir, so requiring the work dir to be RAM-backed
// tmpfs is the single enforcement point. There is deliberately no disk
// fallback: a work dir off tmpfs is a deployment error and the worker
// refuses to run.
bool on_tmpfs(const std::string& path) {
  struct statfs fs;
  return ::statfs(path.c_str(), &fs) == 0 && fs.f_type == TMPFS_MAGIC;
}

// Seeded into the fresh profile before the office core initializes. Without
// it the core writes a .~lock.doc.<ext># sibling next to every loaded
// document and a failed lock write aborts a batch-mode load outright. The
// officecfg setting is the only working off switch in this core: the
// SAL_ENABLE_FILE_LOCKING env var ENABLES advisory locking when set to any
// value, "0" included (sal/osl/unx/file.cxx).
constexpr char kProfileSeed[] =
    R"(<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<item oor:path="/org.openoffice.Office.Common/Misc"><prop oor:name="UseLocking" oor:op="fuse"><value>false</value></prop></item>
</oor:items>
)";

}  // namespace

int main(int argc, char** argv) {
  if (argc < 7 || argc > 9) {
    std::cerr << "usage: grlibre-worker <pages|pdf> <extension> <dpi> "
                 "<max_side_px> <work_dir> <install_path> "
                 "[parts [repair|no-repair]]\n";
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
  if (argc >= 8) options.parts = parse_parts(argv[7]);
  // The broken-package repair opt-in. Absent means refuse, matching the
  // wire default; an unknown token is a caller bug and fails loudly.
  if (argc == 9) {
    std::string repair = argv[8];
    if (repair == "repair") {
      options.allow_disk_repair = true;
    } else if (repair != "no-repair") {
      std::cerr << "grlibre-worker: unknown repair token \"" << repair
                << "\" (expected repair or no-repair)\n";
      return grlibre::kExitRenderFailure;
    }
  }

  std::string document = read_all_stdin();
  if (document.empty()) {
    std::cerr << "grlibre-worker: no document bytes on stdin\n";
    return grlibre::kExitLoadFailure;
  }
  options.input_bytes = static_cast<long>(document.size());
  if (!on_tmpfs(options.work_dir)) {
    std::cerr << "grlibre-worker: work dir " << options.work_dir
              << " is not on tmpfs; refusing to write the uploaded document "
                 "to disk\n";
    return grlibre::kExitRenderFailure;
  }
  // The extension in the filename is load-bearing: delimiter formats (csv,
  // tsv, txt) fall back to a plain Writer text import without it, and the
  // csv/tsv filter preset does not rescue them. The file lives only until
  // the load completes; the engine unlinks it the moment the core has
  // opened its own descriptors on it.
  options.doc_path = options.work_dir + "/doc." + options.extension;
  {
    std::ofstream out(options.doc_path, std::ios::binary);
    out.write(document.data(), static_cast<std::streamsize>(document.size()));
    // Closing before the check makes a failed final flush (a full tmpfs)
    // loud instead of silently truncating the document.
    out.close();
    if (!out) {
      std::cerr << "grlibre-worker: cannot write " << options.doc_path << ": "
                << std::strerror(errno) << " (is the tmpfs sized for the document?)\n";
      return grlibre::kExitRenderFailure;
    }
  }
  std::error_code fs_error;
  std::filesystem::create_directories(options.work_dir + "/profile/user", fs_error);
  if (fs_error) {
    std::cerr << "grlibre-worker: cannot create the profile: "
              << fs_error.message() << "\n";
    return grlibre::kExitRenderFailure;
  }
  {
    std::string seed_path = options.work_dir + "/profile/user/registrymodifications.xcu";
    std::ofstream seed(seed_path, std::ios::binary);
    seed << kProfileSeed;
    seed.close();
    if (!seed) {
      std::cerr << "grlibre-worker: cannot write " << seed_path << "\n";
      return grlibre::kExitRenderFailure;
    }
  }
  // The office core spills document content into TMPDIR: an ODF load keeps
  // one full package copy of the input there for the document's lifetime,
  // and embedded media spill their raw bytes plus derived bitmaps. Pinning
  // TMPDIR inside the tmpfs work dir keeps those spills in RAM; the work
  // dir's removal also collects the empty lu*.tmp directories the core
  // leaks on exit.
  std::string core_tmp = options.work_dir + "/tmp";
  std::filesystem::create_directories(core_tmp, fs_error);
  if (fs_error || ::setenv("TMPDIR", core_tmp.c_str(), 1) != 0) {
    std::cerr << "grlibre-worker: cannot point TMPDIR at " << core_tmp << "\n";
    return grlibre::kExitRenderFailure;
  }

  std::string error;
  int code = grlibre::run_render(options, STDOUT_FILENO, &error);
  if (code != grlibre::kExitOk) {
    std::cerr << "grlibre-worker: " << error << "\n";
  }
  return code;
}
