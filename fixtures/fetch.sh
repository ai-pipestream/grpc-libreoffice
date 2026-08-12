#!/usr/bin/env bash
# Reproducibly fetches the benchmark/demo fixture set. Formats that no longer
# have a stable public URL are generated locally from a downloaded docx
# through a headless LibreOffice.
set -euo pipefail
cd "$(dirname "$0")"

fetch() { curl -fsSL --max-time 60 -o "$1" "$2" && echo "OK   $1" || echo "FAIL $1"; }

fetch sample3.docx https://filesamples.com/samples/document/docx/sample3.docx
fetch sample4.docx https://filesamples.com/samples/document/docx/sample4.docx
fetch sample3.xlsx https://filesamples.com/samples/document/xlsx/sample3.xlsx
fetch sample2.doc  https://filesamples.com/samples/document/doc/sample2.doc
fetch sample2.xls  https://filesamples.com/samples/document/xls/sample2.xls
fetch dummy.pdf    https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf
fetch sample1.pptx https://raw.githubusercontent.com/scanny/python-pptx/master/features/steps/test_files/test.pptx

# odt and rtf: converted locally (their public sample URLs 403/404).
if command -v soffice >/dev/null; then
  soffice --headless --convert-to odt --outdir . sample3.docx >/dev/null 2>&1 && echo "OK   sample3.odt (converted)"
  soffice --headless --convert-to rtf --outdir . sample3.docx >/dev/null 2>&1 && echo "OK   sample3.rtf (converted)"
else
  echo "SKIP odt/rtf (no soffice on PATH)"
fi
