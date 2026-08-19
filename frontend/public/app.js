"use strict";

/* ---------- API base path ---------- */

// Mount prefix the BFF serves this UI under (injected into index.html as
// window.__UI_BASE__ when the BFF runs with UI_BASE). Empty at the root.
const UI_BASE = window.__UI_BASE__ || "";
const apiUrl = (p) => UI_BASE + p;

/* ---------- element handles ---------- */

const $ = (id) => document.getElementById(id);

const els = {
  chips: $("info-chips"),
  navRender: $("nav-render"),
  navSpeed: $("nav-speed"),
  viewRender: $("view-render"),
  viewSpeed: $("view-speed"),
  dropZone: $("drop-zone"),
  dropFormats: $("drop-formats"),
  fileInput: $("file-input"),
  partsPanel: $("parts-panel"),
  partsChips: $("parts-chips"),
  partsSummary: $("parts-summary"),
  partsAll: $("parts-all"),
  partsNone: $("parts-none"),
  partsDefault: $("parts-default"),
  dpiSeg: $("dpi-seg"),
  dpiCustom: $("dpi-custom"),
  formatSeg: $("format-seg"),
  formatQuality: $("format-quality"),
  rangeFrom: $("range-from"),
  rangeTo: $("range-to"),
  optGrayscale: $("opt-grayscale"),
  optMaxWidth: $("opt-max-width"),
  trackedSeg: $("tracked-seg"),
  optSkipHidden: $("opt-skip-hidden"),
  optUsedRange: $("opt-used-range"),
  optNotes: $("opt-notes"),
  optTimeout: $("opt-timeout"),
  roNote: $("ro-note"),
  errorBanner: $("error-banner"),
  errorTitle: $("error-title"),
  errorDetail: $("error-detail"),
  errorDismiss: $("error-dismiss"),
  docCard: $("doc-card"),
  docFilename: $("doc-filename"),
  docFormat: $("doc-format"),
  docClass: $("doc-class"),
  docPages: $("doc-pages"),
  pdfButton: $("pdf-button"),
  pdfTime: $("pdf-time"),
  statFirstPage: $("stat-first-page"),
  statTotal: $("stat-total"),
  statPps: $("stat-pps"),
  statBytes: $("stat-bytes"),
  statPages: $("stat-pages"),
  contentCounts: $("content-counts"),
  pageGrid: $("page-grid"),
  contentPanel: $("content-panel"),
  tabBar: $("tab-bar"),
  tabBodies: $("tab-bodies"),
  statusPanel: $("status-panel"),
  statusBody: $("status-body"),
  warnCount: $("warn-count"),
  lightbox: $("lightbox"),
  lbStage: $("lb-stage"),
  lbImage: $("lb-image"),
  lbOverlaySvg: $("lb-overlay-svg"),
  lbOverlayToggle: $("lb-overlay-toggle"),
  lbCaption: $("lb-caption"),
  lbPrev: $("lb-prev"),
  lbNext: $("lb-next"),
  lbClose: $("lb-close"),
  fixtureList: $("fixture-list"),
  modePages: $("mode-pages"),
  modeFull: $("mode-full"),
  modePdf: $("mode-pdf"),
  speedIterations: $("speed-iterations"),
  speedRun: $("speed-run"),
  speedStatus: $("speed-status"),
  speedResults: $("speed-results"),
  resultsTbody: $("results-tbody"),
  barChart: $("bar-chart"),
};

const SVG_NS = "http://www.w3.org/2000/svg";

/* ---------- formatting helpers ---------- */

function formatBytes(n) {
  if (n == null || isNaN(n)) return "-";
  if (n < 1024) return `${n} B`;
  const units = ["KiB", "MiB", "GiB"];
  let v = n;
  let u = -1;
  do { v /= 1024; u++; } while (v >= 1024 && u < units.length - 1);
  return `${v.toFixed(v >= 100 ? 0 : 1)} ${units[u]}`;
}

function formatMs(ms) {
  if (ms == null || isNaN(ms)) return "-";
  if (ms < 1000) return `${Math.round(ms)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}

function formatDate(epochMs) {
  if (!epochMs) return "";
  return new Date(Number(epochMs)).toLocaleString();
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]));
}

/* ---------- view switching ---------- */

function switchView(view) {
  const speed = view === "speed";
  els.viewRender.classList.toggle("hidden", speed);
  els.viewSpeed.classList.toggle("hidden", !speed);
  els.navRender.classList.toggle("active", !speed);
  els.navSpeed.classList.toggle("active", speed);
  if (speed) loadFixtures();
}

els.navRender.addEventListener("click", () => switchView("render"));
els.navSpeed.addEventListener("click", () => switchView("speed"));

/* ---------- document part selection (Feature 1) ---------- */

// Order and labels of the DocumentPart enum (short names; the BFF prefixes
// DOCUMENT_PART_). defaultOn mirrors the proto's empty-list semantics:
// everything except CELL_LINE_RECTS.
const PART_DEFS = [
  { key: "PAGES", label: "Pages" },
  { key: "METADATA", label: "Metadata" },
  { key: "PARAGRAPHS", label: "Paragraphs" },
  { key: "TABLES", label: "Tables" },
  { key: "IMAGES", label: "Images" },
  { key: "FOOTNOTES", label: "Footnotes" },
  { key: "HEADERS_FOOTERS", label: "Headers/footers" },
  { key: "PAGE_STYLES", label: "Page styles" },
  { key: "INDEXES", label: "Indexes" },
  { key: "SHEETS", label: "Sheets" },
  { key: "SLIDES", label: "Slides" },
  { key: "SHAPES", label: "Shapes" },
  { key: "TEXT_FRAMES", label: "Text frames" },
  { key: "EMBEDDED_OBJECTS", label: "Embedded objects" },
  { key: "LINE_RECTS", label: "Line rects" },
  { key: "CELL_LINE_RECTS", label: "Cell line rects", defaultOn: false },
  { key: "COMMENTS", label: "Comments" },
  { key: "TRACKED_CHANGES", label: "Tracked changes" },
  { key: "BOOKMARKS", label: "Bookmarks" },
  { key: "FORM_FIELDS", label: "Form fields" },
];

const partsSelected = new Set(
  PART_DEFS.filter((p) => p.defaultOn !== false).map((p) => p.key));

function isDefaultPartsSelection() {
  const def = PART_DEFS.filter((p) => p.defaultOn !== false);
  return partsSelected.size === def.length &&
    def.every((p) => partsSelected.has(p.key));
}

// Query-string fragment for the current selection. The default selection
// sends nothing: the server's empty-list behavior is exactly that default.
function partsQuery() {
  if (isDefaultPartsSelection() || partsSelected.size === 0) return "";
  return "&parts=" + [...partsSelected].join(",");
}

function updatePartsSummary() {
  if (isDefaultPartsSelection()) {
    els.partsSummary.textContent = "everything (default)";
  } else if (partsSelected.size === 0) {
    els.partsSummary.textContent = "none selected: server default applies";
  } else {
    els.partsSummary.textContent =
      `${partsSelected.size} of ${PART_DEFS.length} selected`;
  }
}

function buildPartsChips() {
  for (const def of PART_DEFS) {
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = "part-chip" + (partsSelected.has(def.key) ? " on" : "");
    chip.textContent = def.label;
    chip.dataset.key = def.key;
    chip.addEventListener("click", () => {
      if (partsSelected.has(def.key)) partsSelected.delete(def.key);
      else partsSelected.add(def.key);
      chip.classList.toggle("on", partsSelected.has(def.key));
      updatePartsSummary();
    });
    els.partsChips.appendChild(chip);
  }
  updatePartsSummary();
}

function setAllParts(pred) {
  partsSelected.clear();
  for (const def of PART_DEFS) {
    if (pred(def)) partsSelected.add(def.key);
  }
  for (const chip of els.partsChips.children) {
    chip.classList.toggle("on", partsSelected.has(chip.dataset.key));
  }
  updatePartsSummary();
}

els.partsAll.addEventListener("click", () => setAllParts(() => true));
els.partsNone.addEventListener("click", () => setAllParts(() => false));
els.partsDefault.addEventListener("click",
  () => setAllParts((def) => def.defaultOn !== false));

buildPartsChips();

/* ---------- render options: DPI and page range ---------- */

// Selected preset DPI ("" = server default). A valid custom entry beats the
// preset. Applies to StreamPages only; /api/pdf never sends dpi or range.
let presetDpi = "";

function customDpiValue() {
  const raw = els.dpiCustom.value.trim();
  if (!raw) return null;
  const n = Number(raw);
  return Number.isInteger(n) && n > 0 ? n : null;
}

function effectiveDpi() {
  const custom = customDpiValue();
  if (custom != null) return custom;
  return presetDpi ? Number(presetDpi) : null;
}

// Reads the from/to inputs; swaps a backwards pair so the server (which
// rejects first > last with INVALID_ARGUMENT) never sees one.
function effectiveRange() {
  const parse = (el) => {
    const n = Number(el.value.trim());
    return el.value.trim() && Number.isInteger(n) && n >= 1 ? n : null;
  };
  let from = parse(els.rangeFrom);
  let to = parse(els.rangeTo);
  let swapped = false;
  if (from != null && to != null && from > to) {
    [from, to] = [to, from];
    swapped = true;
  }
  return { from, to, swapped };
}

// Selected page image format ("" = PNG). The quality box only applies to
// the lossy formats. Tracked-change display is empty = as-is (server default).
let pageFormat = "";
let trackedChanges = "";

function effectiveQuality() {
  const raw = els.formatQuality.value.trim();
  if (!raw || !pageFormat) return null;
  const n = Number(raw);
  return Number.isInteger(n) && n >= 1 && n <= 100 ? n : null;
}

function updateOptionsNote() {
  const bits = [];
  const dpi = effectiveDpi();
  if (dpi != null) bits.push(`dpi ${dpi}` + (dpi < 24 || dpi > 600 ? " (server clamps to 24-600)" : ""));
  const { from, to, swapped } = effectiveRange();
  if (from != null || to != null) {
    bits.push(`pages ${from ?? 1}-${to ?? "end"}` + (swapped ? " (swapped: from > to)" : ""));
  }
  if (pageFormat === "svg") {
    bits.push("svg");
  } else if (pageFormat) {
    bits.push(pageFormat + " q" + (effectiveQuality() ?? 85));
  }
  if (els.optGrayscale.checked) bits.push("grayscale");
  const maxWidth = effectiveMaxWidth();
  if (maxWidth != null) bits.push("max-width " + maxWidth);
  if (trackedChanges) bits.push("tracked " + trackedChanges);
  if (els.optSkipHidden.checked) bits.push("skip-hidden");
  if (els.optUsedRange.checked) bits.push("used-range");
  if (els.optNotes.checked) bits.push("notes");
  const timeout = effectiveTimeout();
  if (timeout != null) bits.push("timeout " + timeout + "s");
  els.roNote.textContent = bits.length ? bits.join(" \u00b7 ") : "";
  els.roNote.classList.toggle("ro-note-warn",
    els.roNote.textContent.includes("swapped") ||
    els.roNote.textContent.includes("clamps"));
}

function markDpiSeg() {
  const custom = customDpiValue() != null;
  for (const btn of els.dpiSeg.children) {
    btn.classList.toggle("active", !custom && btn.dataset.dpi === presetDpi);
  }
}

els.dpiSeg.addEventListener("click", (e) => {
  const btn = e.target.closest(".ro-seg-btn");
  if (!btn) return;
  presetDpi = btn.dataset.dpi;
  els.dpiCustom.value = "";
  markDpiSeg();
  updateOptionsNote();
});

els.dpiCustom.addEventListener("input", () => {
  markDpiSeg();
  updateOptionsNote();
});

els.rangeFrom.addEventListener("input", updateOptionsNote);
els.rangeTo.addEventListener("input", updateOptionsNote);

els.formatSeg.addEventListener("click", (e) => {
  const btn = e.target.closest(".ro-seg-btn");
  if (!btn) return;
  pageFormat = btn.dataset.format;
  els.formatQuality.disabled = !pageFormat || pageFormat === "svg";
  for (const b of els.formatSeg.children) {
    b.classList.toggle("active", b.dataset.format === pageFormat);
  }
  updateOptionsNote();
});

els.formatQuality.addEventListener("input", updateOptionsNote);
els.optGrayscale.addEventListener("change", updateOptionsNote);
els.optMaxWidth.addEventListener("input", updateOptionsNote);

els.trackedSeg.addEventListener("click", (e) => {
  const btn = e.target.closest(".ro-seg-btn");
  if (!btn) return;
  trackedChanges = btn.dataset.tracked;
  for (const b of els.trackedSeg.children) {
    b.classList.toggle("active", b.dataset.tracked === trackedChanges);
  }
  updateOptionsNote();
});

els.optSkipHidden.addEventListener("change", updateOptionsNote);
els.optUsedRange.addEventListener("change", updateOptionsNote);
els.optNotes.addEventListener("change", updateOptionsNote);
els.optTimeout.addEventListener("input", updateOptionsNote);

function effectiveMaxWidth() {
  const raw = els.optMaxWidth.value.trim();
  if (!raw) return null;
  const n = Number(raw);
  return Number.isInteger(n) && n > 0 ? n : null;
}

function effectiveTimeout() {
  const raw = els.optTimeout.value.trim();
  if (!raw) return null;
  const n = Number(raw);
  return Number.isInteger(n) && n > 0 ? n : null;
}

// Query-string fragment for the current DPI and page range. Also returns
// the values so the render session can label stats honestly.
function renderOptionsQuery() {
  let q = "";
  const dpi = effectiveDpi();
  if (dpi != null) q += `&dpi=${dpi}`;
  const { from, to } = effectiveRange();
  if (from != null) q += `&firstPage=${from}`;
  if (to != null) q += `&lastPage=${to}`;
  if (pageFormat) {
    q += `&format=${pageFormat}`;
    const quality = effectiveQuality();
    if (quality != null) q += `&quality=${quality}`;
  }
  if (els.optGrayscale.checked) q += `&grayscale=1`;
  const maxWidth = effectiveMaxWidth();
  if (maxWidth != null) q += `&maxWidth=${maxWidth}`;
  if (trackedChanges) q += `&trackedChanges=${trackedChanges}`;
  if (els.optSkipHidden.checked) q += `&skipHidden=1`;
  if (els.optUsedRange.checked) q += `&usedRange=1`;
  if (els.optNotes.checked) q += `&notes=1`;
  const timeout = effectiveTimeout();
  if (timeout != null) q += `&timeout=${timeout}`;
  return { q, dpi, from, to };
}

/* ---------- content-count labels ---------- */

const KIND_LABELS = {
  metadata: "metadata",
  paragraph: "paragraphs",
  table: "tables",
  embeddedImage: "images",
  footnote: "footnotes",
  headerFooter: "headers/footers",
  pageStyle: "page styles",
  documentIndex: "indexes",
  drawingShape: "drawing shapes",
  slide: "slides",
  slideShape: "slide shapes",
  textFrame: "text frames",
  shape: "shapes",
  embeddedObject: "embedded objects",
  sheet: "sheets",
  sheetRow: "sheet rows",
  sheetNamedRange: "named ranges",
  sheetDatabaseRange: "database ranges",
  sheetCellComment: "cell comments",
  sheetChart: "charts",
  sheetPivotTable: "pivot tables",
  comment: "comments",
  trackedChange: "tracked changes",
  bookmark: "bookmarks",
  formField: "form fields",
};

/* ---------- render session state ---------- */

const state = {
  currentFile: null,
  rendering: false,
  pages: [], // { index, dataUrl, widthPx, heightPx, dpi }
  counts: new Map(),
  bytesReceived: 0,
  firstPageMs: null,
  lastEventMs: 0,
  expectedPages: null,
  rangeFrom: null, // page range sent with the active render, 1-based
  rangeTo: null,
  liveTimer: null,
  startedAt: 0,
  lightboxIndex: -1,
  pageRects: [], // DocumentInfo.pageRects, twips, document-absolute
  lineBoxes: new Map(), // pageIndex -> [{x, y, w, h}] twips, doc-absolute
  overlayOn: false,
};

/* ---------- service info ---------- */

async function loadServiceInfo() {
  try {
    const resp = await fetch(apiUrl("/api/info"));
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const info = await resp.json();
    const formats = info.supportedFormats || [];
    els.chips.innerHTML = "";
    addChip(`LibreOffice <b>${escapeHtml(info.libreofficeVersion || "?")}</b>`);
    addChip(`max <b>${formatBytes(Number(info.maxDocumentBytes))}</b> / doc`);
    addChip(`<b>${formats.length}</b> formats`);
    addChip(`<b>${info.renderDpi}</b> dpi`);
    if (info.disklessDocuments) addChip(`diskless <b>tmpfs</b>`);
    els.dropFormats.textContent = formats.length
      ? formats.map((f) => "." + f).join("  ")
      : "no format list reported";
  } catch (err) {
    els.chips.innerHTML = "";
    const chip = document.createElement("span");
    chip.className = "chip chip-bad";
    chip.textContent = "service unreachable";
    els.chips.appendChild(chip);
    els.dropFormats.textContent =
      "Could not reach the render service. Is the BFF and gRPC server running?";
  }
}

function addChip(html) {
  const chip = document.createElement("span");
  chip.className = "chip";
  chip.innerHTML = html;
  els.chips.appendChild(chip);
}

/* ---------- error banner ---------- */

function showError(title, detail) {
  els.errorTitle.textContent = title;
  els.errorDetail.textContent = detail ? " — " + detail : "";
  els.errorBanner.classList.remove("hidden");
}

function hideError() {
  els.errorBanner.classList.add("hidden");
}

els.errorDismiss.addEventListener("click", hideError);

/* ---------- upload wiring ---------- */

els.dropZone.addEventListener("click", () => els.fileInput.click());
els.dropZone.addEventListener("keydown", (e) => {
  if (e.key === "Enter" || e.key === " ") {
    e.preventDefault();
    els.fileInput.click();
  }
});
els.fileInput.addEventListener("change", () => {
  if (els.fileInput.files.length) {
    startRender(els.fileInput.files[0]);
    els.fileInput.value = "";
  }
});

["dragenter", "dragover"].forEach((ev) =>
  els.dropZone.addEventListener(ev, (e) => {
    e.preventDefault();
    els.dropZone.classList.add("dragging");
  }));
["dragleave", "drop"].forEach((ev) =>
  els.dropZone.addEventListener(ev, (e) => {
    e.preventDefault();
    els.dropZone.classList.remove("dragging");
  }));
els.dropZone.addEventListener("drop", (e) => {
  const file = e.dataTransfer?.files?.[0];
  if (file) startRender(file);
});

/* ---------- render session ---------- */

function resetSession(file) {
  state.currentFile = file;
  state.pages = [];
  state.counts = new Map();
  state.bytesReceived = 0;
  state.firstPageMs = null;
  state.lastEventMs = 0;
  state.expectedPages = null;
  state.startedAt = performance.now();
  state.lightboxIndex = -1;
  state.pageRects = [];
  state.lineBoxes = new Map();

  hideError();
  closeLightbox();
  resetContentPanel();
  els.pageGrid.innerHTML = "";
  els.contentCounts.innerHTML = "";
  els.statusBody.innerHTML = "";
  els.warnCount.textContent = "";
  els.statusPanel.classList.add("hidden");
  els.statusPanel.open = false;
  els.docCard.classList.add("hidden");
  els.pdfTime.textContent = "";

  els.statFirstPage.textContent = "-";
  els.statTotal.textContent = "0 ms";
  els.statPps.textContent = "-";
  els.statBytes.textContent = "0 B";
  els.statPages.textContent = "0";

  els.docFilename.textContent = file.name;
  els.docFormat.textContent = "";
  els.docClass.textContent = "";
  els.docPages.textContent = "";
  els.pdfButton.disabled = true;
}

async function startRender(file) {
  if (state.rendering) return;
  state.rendering = true;
  els.dropZone.classList.add("busy");
  resetSession(file);
  const opts = renderOptionsQuery();
  state.rangeFrom = opts.from;
  state.rangeTo = opts.to;

  if (state.liveTimer) clearInterval(state.liveTimer);
  state.liveTimer = setInterval(() => {
    if (state.rendering) {
      els.statTotal.textContent =
        formatMs(performance.now() - state.startedAt);
    }
  }, 100);

  try {
    const resp = await fetch(
      apiUrl("/api/render?filename=" + encodeURIComponent(file.name) +
        partsQuery() + opts.q),
      {
        method: "POST",
        headers: {
          "X-Filename": encodeURIComponent(file.name),
          "Content-Type": file.type || "application/octet-stream",
        },
        body: file,
      });
    if (!resp.ok || !resp.body) {
      throw new Error(`HTTP ${resp.status}`);
    }

    const reader = resp.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      state.bytesReceived += value.byteLength;
      buffer += decoder.decode(value, { stream: true });
      let nl;
      while ((nl = buffer.indexOf("\n")) >= 0) {
        const line = buffer.slice(0, nl).trim();
        buffer = buffer.slice(nl + 1);
        if (line) handleEvent(JSON.parse(line));
      }
      els.statBytes.textContent = formatBytes(state.bytesReceived);
    }
  } catch (err) {
    showError("NETWORK ERROR", String(err.message || err));
  } finally {
    finishRender();
  }
}

function finishRender() {
  state.rendering = false;
  els.dropZone.classList.remove("busy");
  if (state.liveTimer) {
    clearInterval(state.liveTimer);
    state.liveTimer = null;
  }
  const total = state.lastEventMs ||
    (performance.now() - state.startedAt);
  els.statTotal.textContent = formatMs(total);
  els.statBytes.textContent = formatBytes(state.bytesReceived);
  updatePps();
  if (state.currentFile) els.pdfButton.disabled = false;
}

function updatePps() {
  if (state.pages.length && state.lastEventMs > 0) {
    els.statPps.textContent =
      (state.pages.length / (state.lastEventMs / 1000)).toFixed(2);
  }
}

/* ---------- event dispatch ---------- */

function handleEvent(evt) {
  if (evt.tMs != null) state.lastEventMs = Math.max(state.lastEventMs, evt.tMs);
  switch (evt.event) {
    case "start":
      break;
    case "documentInfo":
      onDocumentInfo(evt.data);
      break;
    case "pageImage":
      onPageImage(evt.data, evt.tMs);
      break;
    case "status":
      onStatus(evt.data, evt.tMs);
      break;
    case "error":
      onGrpcError(evt.data);
      break;
    case "end":
      break;
    default:
      bumpCount(evt.event);
      collectLineBoxes(evt.event, evt.data);
      contentEvent(evt.event, evt.data);
      break;
  }
}

function onDocumentInfo(info) {
  els.docCard.classList.remove("hidden");
  els.docFormat.textContent = info.sourceFormat || "?";
  els.docClass.textContent = info.documentType || "?";
  state.expectedPages = info.pageCount;
  state.pageRects = info.pageRects || [];
  els.docPages.textContent =
    `${info.pageCount} page${info.pageCount === 1 ? "" : "s"}`;
  els.statPages.textContent = pagesStatText();
}

// The "pages" stat: emitted count against the document's FULL page count
// (DocumentInfo is never restricted by a page range). With a range active
// the honest reading is "2 of 4 (range 2-3)", not "2 / 4 missing pages".
function pagesStatText() {
  const got = state.pages.length;
  const total = state.expectedPages;
  if (total == null) return String(got);
  if (state.rangeFrom == null && state.rangeTo == null) {
    return `${got} / ${total}`;
  }
  const lo = state.rangeFrom ?? 1;
  const hi = Math.min(state.rangeTo ?? total, total);
  return `${got} of ${total} (range ${lo}-${hi})`;
}

function onPageImage(page, tMs) {
  if (state.firstPageMs == null) {
    state.firstPageMs = tMs;
    els.statFirstPage.textContent = formatMs(tMs);
  }
  const mime = page.format === "PAGE_IMAGE_FORMAT_JPEG" ? "image/jpeg"
      : page.format === "PAGE_IMAGE_FORMAT_WEBP" ? "image/webp"
      : page.format === "PAGE_IMAGE_FORMAT_SVG" ? "image/svg+xml"
      : "image/png";
  const dataUrl = `data:${mime};base64,` + page.png;
  state.pages.push({
    index: page.index,
    dataUrl,
    widthPx: page.widthPx,
    heightPx: page.heightPx,
    dpi: page.dpi,
  });

  const cell = document.createElement("figure");
  cell.className = "page-cell";
  cell.style.margin = "0";
  const img = document.createElement("img");
  img.src = dataUrl;
  img.alt = `Page ${page.index + 1}`;
  img.loading = "lazy";
  const meta = document.createElement("div");
  meta.className = "page-meta";
  meta.innerHTML =
    `<span>p.${page.index + 1}</span>` +
    `<span>${page.widthPx}&times;${page.heightPx} @ ${page.dpi}dpi</span>`;
  cell.appendChild(img);
  cell.appendChild(meta);
  const arrayPos = state.pages.length - 1;
  cell.addEventListener("click", () => openLightbox(arrayPos));
  els.pageGrid.appendChild(cell);

  els.statPages.textContent = pagesStatText();
  updatePps();
}

function bumpCount(kind) {
  const next = (state.counts.get(kind) || 0) + 1;
  state.counts.set(kind, next);
  let pill = document.getElementById("count-" + kind);
  if (!pill) {
    pill = document.createElement("span");
    pill.className = "count-pill";
    pill.id = "count-" + kind;
    pill.innerHTML =
      `<b>0</b> ${escapeHtml(KIND_LABELS[kind] || kind)}`;
    els.contentCounts.appendChild(pill);
  }
  pill.querySelector("b").textContent = String(next);
}

function onStatus(status, tMs) {
  els.statusPanel.classList.remove("hidden");
  const warnings = status.warnings || [];
  els.warnCount.textContent =
    warnings.length ? `(${warnings.length} warning${warnings.length === 1 ? "" : "s"})` : "";
  const lines = [];
  lines.push(`<div class="status-line status-ok">state: ${escapeHtml(status.state)}</div>`);
  lines.push(`<div class="status-line">input: ${formatBytes(Number(status.inputBytes))}` +
    ` &middot; output: ${formatBytes(Number(status.outputBytes))}` +
    ` &middot; worker render time: ${formatMs(Number(status.renderMillis))}` +
    ` &middot; stream time: ${formatMs(tMs)}</div>`);
  for (const w of warnings) {
    lines.push(`<div class="status-line status-warn">warning: ${escapeHtml(w)}</div>`);
  }
  els.statusBody.innerHTML = lines.join("");
  if (warnings.length) els.statusPanel.open = true;
}

function onGrpcError(err) {
  showError(
    err.codeName || "UNKNOWN",
    err.message || "the render failed with no message");
  els.statusPanel.classList.remove("hidden");
  els.statusBody.innerHTML +=
    `<div class="status-line status-warn">gRPC ${escapeHtml(err.codeName || "?")}: ` +
    `${escapeHtml(err.message || "")}</div>`;
}

/* ---------- layout overlay collection (Feature 3) ---------- */

function addLineBoxes(rects) {
  for (const b of rects || []) {
    if (b.pageIndex == null || b.pageIndex < 0) continue;
    let list = state.lineBoxes.get(b.pageIndex);
    if (!list) {
      list = [];
      state.lineBoxes.set(b.pageIndex, list);
    }
    list.push({
      x: Number(b.xTwips), y: Number(b.yTwips),
      w: Number(b.widthTwips), h: Number(b.heightTwips),
    });
  }
}

// Events that carry LineBox rects: Paragraph, TableData (and its cells when
// CELL_LINE_RECTS is selected), EmbeddedImage.
function collectLineBoxes(kind, data) {
  if (data.lineRects) addLineBoxes(data.lineRects);
  if (kind === "table") {
    for (const cell of data.cells || []) {
      if (cell.lineRects) addLineBoxes(cell.lineRects);
    }
  }
}

/* ---------- typed-content viewer (Feature 2) ---------- */

const TAB_DEFS = [
  { key: "text", label: "Text" },
  { key: "tables", label: "Tables" },
  { key: "sheets", label: "Sheets" },
  { key: "slides", label: "Slides" },
  { key: "metadata", label: "Metadata" },
];

const MAX_SHEET_ROWS = 200;
const MAX_SHEET_COLS = 40;

// Per-session viewer state; rebuilt by resetContentPanel().
let content = null;

function resetContentPanel() {
  els.tabBar.innerHTML = "";
  els.tabBodies.innerHTML = "";
  els.contentPanel.classList.add("hidden");
  content = {
    tabs: new Map(), // key -> { btn, body, count }
    activeTab: null,
    textFlow: null, // container for paragraphs
    footnotesSection: null,
    commentsSection: null,
    sheets: new Map(), // sheetIndex -> { tbody, rows, cols, startCol, truncNote }
    slides: new Map(), // slideIndex -> { shapesDiv, notesDiv }
  };
  for (const def of TAB_DEFS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "tab-btn hidden";
    btn.setAttribute("role", "tab");
    btn.innerHTML =
      `${escapeHtml(def.label)}<span class="tab-count"></span>`;
    const body = document.createElement("div");
    body.className = "tab-body";
    btn.addEventListener("click", () => activateTab(def.key));
    els.tabBar.appendChild(btn);
    els.tabBodies.appendChild(body);
    content.tabs.set(def.key, { btn, body, count: 0 });
  }
}

function activateTab(key) {
  for (const [k, tab] of content.tabs) {
    const active = k === key;
    tab.btn.classList.toggle("active", active);
    tab.body.classList.toggle("active", active);
  }
  content.activeTab = key;
}

// Returns the tab body, unhiding the tab (and the panel) on first content.
function tab(key, delta = 1) {
  const t = content.tabs.get(key);
  t.count += delta;
  if (t.btn.classList.contains("hidden")) {
    t.btn.classList.remove("hidden");
    els.contentPanel.classList.remove("hidden");
    if (!content.activeTab) activateTab(key);
  }
  t.btn.querySelector(".tab-count").textContent =
    t.count > 0 ? String(t.count) : "";
  return t.body;
}

// One styled run -> inline DOM. Hyperlinks become real links.
function renderRun(run) {
  let node;
  if (run.hyperlinkUrl) {
    node = document.createElement("a");
    node.href = run.hyperlinkUrl;
    node.target = "_blank";
    node.rel = "noopener noreferrer";
    if (run.hyperlinkName) node.title = run.hyperlinkName;
  } else {
    node = document.createElement("span");
  }
  node.textContent = run.text || "";
  if (run.weight >= 150) node.style.fontWeight = "700";
  if (run.italic) node.style.fontStyle = "italic";
  const deco = [];
  if (run.underline) deco.push("underline");
  if (run.strikethrough) deco.push("line-through");
  if (deco.length) node.style.textDecoration = deco.join(" ");
  return node;
}

function paragraphText(runs) {
  return (runs || []).map((r) => r.text || "").join("");
}

function runsInto(el, runs) {
  for (const run of runs || []) {
    if (run.text) el.appendChild(renderRun(run));
  }
  return el;
}

/* text tab */

function textFlow() {
  const body = tab("text", 0);
  if (!content.textFlow) {
    content.textFlow = document.createElement("div");
    body.insertBefore(content.textFlow, body.firstChild);
  }
  return content.textFlow;
}

function onParagraphEvent(p) {
  if (!paragraphText(p.runs).trim()) return; // skip empty spacer paragraphs
  tab("text");
  const el = document.createElement("p");
  const level = p.outlineLevel || 0;
  if (level >= 1) {
    el.className = "ct-h " +
      (level === 1 ? "ct-h1" : level === 2 ? "ct-h2" : "ct-h3");
  } else {
    el.className = "ct-para" + (p.listLevel >= 0 ? " ct-list" : "");
    if (p.listLevel > 0) el.style.marginLeft = `${p.listLevel * 22}px`;
  }
  runsInto(el, p.runs);
  textFlow().appendChild(el);
}

function textSection(prop, title) {
  if (!content[prop]) {
    const body = tab("text", 0);
    const head = document.createElement("div");
    head.className = "ct-section-head";
    head.textContent = title;
    const sec = document.createElement("div");
    body.appendChild(head);
    body.appendChild(sec);
    content[prop] = sec;
  }
  return content[prop];
}

function onFootnoteEvent(f) {
  tab("text");
  const el = document.createElement("p");
  el.className = "ct-note";
  const label = document.createElement("b");
  label.textContent = (f.endnote ? "endnote " : "footnote ") + (f.label || "");
  el.appendChild(label);
  el.appendChild(document.createTextNode("  "));
  runsInto(el, f.runs);
  textSection("footnotesSection", "Footnotes & endnotes").appendChild(el);
}

function onCommentEvent(c) {
  tab("text");
  const el = document.createElement("div");
  el.className = "ct-comment";
  const meta = document.createElement("div");
  meta.className = "ct-comment-meta";
  const when = formatDate(c.epochMs);
  meta.textContent = [c.author || "unknown author", when]
    .filter(Boolean).join(" \u00b7 ") +
    (c.resolved ? " \u00b7 resolved" : "");
  el.appendChild(meta);
  const text = document.createElement("div");
  text.textContent = c.text || paragraphText(c.runs);
  el.appendChild(text);
  if (c.anchoredText) {
    const anchor = document.createElement("div");
    anchor.className = "ct-comment-meta";
    anchor.textContent = `on: "${c.anchoredText}"`;
    el.appendChild(anchor);
  }
  textSection("commentsSection", "Comments").appendChild(el);
}

/* tables tab */

function onTableEvent(t) {
  const body = tab("tables");
  const caption = document.createElement("div");
  caption.className = "ct-table-caption";
  caption.textContent =
    `table ${t.index + 1} \u00b7 page ${t.pageIndex + 1} \u00b7 ` +
    `${t.rows}\u00d7${t.columns}`;
  body.appendChild(caption);

  const table = document.createElement("table");
  table.className = "ct-table";
  // Base grid, addressed by row/column; cells outside the base grid (split
  // or merged, row -1) are appended below by their office cell name.
  const grid = [];
  for (let r = 0; r < t.rows; r++) grid.push(new Array(t.columns).fill(null));
  const extras = [];
  for (const cell of t.cells || []) {
    if (cell.row >= 0 && cell.row < t.rows &&
        cell.column >= 0 && cell.column < t.columns) {
      grid[cell.row][cell.column] = cell;
    } else {
      extras.push(cell);
    }
  }
  for (let r = 0; r < t.rows; r++) {
    const tr = document.createElement("tr");
    for (let c = 0; c < t.columns; c++) {
      const td = document.createElement("td");
      const cell = grid[r][c];
      if (cell) {
        td.textContent = cell.text || "";
        td.title = cell.name || "";
      }
      tr.appendChild(td);
    }
    table.appendChild(tr);
  }
  body.appendChild(table);
  if (extras.length) {
    const note = document.createElement("div");
    note.className = "ct-table-caption";
    note.textContent = "off-grid cells: " + extras
      .map((c) => `${c.name}="${c.text || ""}"`).join("  ");
    body.appendChild(note);
  }
}

/* sheets tab */

function sheetEntry(sheetIndex, header) {
  let entry = content.sheets.get(sheetIndex);
  if (entry) return entry;
  const body = tab("sheets", 0);

  const caption = document.createElement("div");
  caption.className = "ct-table-caption";
  caption.textContent = header
    ? `sheet ${sheetIndex + 1}: ${header.name}` +
      (header.visible === false ? " (hidden)" : "")
    : `sheet ${sheetIndex + 1}`;
  body.appendChild(caption);

  const startCol = header ? header.usedStartColumn : 0;
  const endCol = header
    ? Math.min(header.usedEndColumn, startCol + MAX_SHEET_COLS - 1)
    : startCol + MAX_SHEET_COLS - 1;

  const table = document.createElement("table");
  table.className = "ct-table";
  const thead = document.createElement("tr");
  thead.appendChild(document.createElement("th")); // row-number corner
  for (let c = startCol; c <= endCol; c++) {
    const th = document.createElement("th");
    th.textContent = columnName(c);
    thead.appendChild(th);
  }
  table.appendChild(thead);
  body.appendChild(table);

  const truncNote = document.createElement("div");
  truncNote.className = "ct-sheet-truncated hidden";
  body.appendChild(truncNote);

  entry = { table, rows: 0, startCol, endCol, truncNote };
  content.sheets.set(sheetIndex, entry);
  return entry;
}

function columnName(c) {
  let name = "";
  let n = c;
  do {
    name = String.fromCharCode(65 + (n % 26)) + name;
    n = Math.floor(n / 26) - 1;
  } while (n >= 0);
  return name;
}

function onSheetEvent(s) {
  tab("sheets");
  sheetEntry(s.index, s);
}

function onSheetRowEvent(r) {
  tab("sheets");
  const entry = sheetEntry(r.sheetIndex, null);
  if (entry.rows >= MAX_SHEET_ROWS) {
    entry.truncNote.classList.remove("hidden");
    entry.truncNote.textContent =
      `showing first ${MAX_SHEET_ROWS} used rows; more rows streamed`;
    return;
  }
  entry.rows++;
  const tr = document.createElement("tr");
  const th = document.createElement("th");
  th.textContent = String(r.row + 1);
  tr.appendChild(th);
  const byCol = new Map((r.cells || []).map((c) => [c.column, c]));
  for (let c = entry.startCol; c <= entry.endCol; c++) {
    const td = document.createElement("td");
    const cell = byCol.get(c);
    if (cell) {
      td.textContent = cell.display || "";
      if (cell.formula) td.title = cell.formula;
    }
    tr.appendChild(td);
  }
  entry.table.appendChild(tr);
}

/* slides tab */

function slideEntry(slideIndex, header) {
  let entry = content.slides.get(slideIndex);
  if (entry) return entry;
  const body = tab("slides", 0);

  const box = document.createElement("div");
  box.className = "ct-slide";
  const head = document.createElement("div");
  head.className = "ct-slide-head";
  const title = document.createElement("b");
  title.textContent = header?.name || `Slide ${slideIndex + 1}`;
  head.appendChild(title);
  if (header?.masterPageName) {
    const master = document.createElement("span");
    master.className = "ct-slide-master";
    master.textContent = `master: ${header.masterPageName}`;
    head.appendChild(master);
  }
  box.appendChild(head);
  const shapesDiv = document.createElement("div");
  box.appendChild(shapesDiv);
  body.appendChild(box);

  entry = { shapesDiv };
  content.slides.set(slideIndex, entry);
  return entry;
}

function onSlideEvent(s) {
  tab("slides");
  slideEntry(s.index, s);
}

function onSlideShapeEvent(s) {
  tab("slides");
  const entry = slideEntry(s.slideIndex, null);
  const div = document.createElement("div");
  div.className = "ct-shape";

  const meta = document.createElement("div");
  meta.className = "ct-shape-meta";
  const role = document.createElement("span");
  if (s.notes) {
    role.className = "ct-role ct-role-notes";
    role.textContent = "speaker notes";
  } else {
    role.className = "ct-role";
    const r = (s.placeholderRole || "").replace(/^PLACEHOLDER_ROLE_/, "");
    role.textContent = r && r !== "NONE" ? r.toLowerCase() : "shape";
  }
  meta.appendChild(role);
  const type = document.createElement("span");
  type.className = "ct-shape-type";
  type.textContent = (s.shapeType || "").replace(/^com\.sun\.star\./, "");
  meta.appendChild(type);
  div.appendChild(meta);

  let hasText = false;
  for (const para of s.paragraphs || []) {
    const text = paragraphText(para.runs);
    if (!text.trim()) continue;
    hasText = true;
    const p = document.createElement("p");
    p.className = "ct-slide-para";
    p.style.marginLeft = `${(para.outlineDepth || 0) * 18}px`;
    runsInto(p, para.runs);
    div.appendChild(p);
  }
  if (!hasText && s.isEmptyPlaceholder) {
    const p = document.createElement("p");
    p.className = "ct-slide-para";
    p.style.color = "var(--text-dim)";
    p.textContent = "(empty placeholder)";
    div.appendChild(p);
  }
  entry.shapesDiv.appendChild(div);
}

/* metadata tab */

function onMetadataEvent(m) {
  const body = tab("metadata");
  const dl = document.createElement("dl");
  dl.className = "ct-meta-grid";
  const add = (label, value) => {
    if (value == null || value === "" ||
        (Array.isArray(value) && !value.length)) return;
    const dt = document.createElement("dt");
    dt.textContent = label;
    const dd = document.createElement("dd");
    dd.textContent = Array.isArray(value) ? value.join(", ") : String(value);
    dl.appendChild(dt);
    dl.appendChild(dd);
  };
  add("title", m.title);
  add("author", m.author);
  add("subject", m.subject);
  add("keywords", m.keywords);
  add("created", formatDate(m.createdEpochMs));
  add("modified", formatDate(m.modifiedEpochMs));
  add("modified by", m.modifiedBy);
  add("generator", m.generator);
  add("language", m.language);
  add("template", m.templateName);
  if (m.editingCycles) add("editing cycles", m.editingCycles);
  if (m.editingDurationSeconds) {
    add("editing time", formatMs(Number(m.editingDurationSeconds) * 1000));
  }
  add("printed", formatDate(m.printedEpochMs));
  add("printed by", m.printedBy);
  for (const [k, v] of Object.entries(m.statistics || {})) {
    add(k, Number(v));
  }
  for (const up of m.userProperties || []) {
    const v = up.value === "epochMs" ? formatDate(up.epochMs) : up[up.value];
    add(up.name, v);
  }
  body.appendChild(dl);
}

/* dispatcher for typed content */

function contentEvent(kind, data) {
  switch (kind) {
    case "paragraph": onParagraphEvent(data); break;
    case "footnote": onFootnoteEvent(data); break;
    case "comment": onCommentEvent(data); break;
    case "table": onTableEvent(data); break;
    case "sheet": onSheetEvent(data); break;
    case "sheetRow": onSheetRowEvent(data); break;
    case "slide": onSlideEvent(data); break;
    case "slideShape": onSlideShapeEvent(data); break;
    case "metadata": onMetadataEvent(data); break;
    default: break;
  }
}

resetContentPanel();

/* ---------- PDF download ---------- */

els.pdfButton.addEventListener("click", async () => {
  const file = state.currentFile;
  if (!file) return;
  els.pdfButton.disabled = true;
  const t0 = performance.now();
  const tick = setInterval(() => {
    els.pdfTime.textContent = formatMs(performance.now() - t0);
  }, 100);
  try {
    const resp = await fetch(
      apiUrl("/api/pdf?filename=" + encodeURIComponent(file.name)),
      {
        method: "POST",
        headers: {
          "X-Filename": encodeURIComponent(file.name),
          "Content-Type": file.type || "application/octet-stream",
        },
        body: file,
      });
    if (!resp.ok) {
      let detail = `HTTP ${resp.status}`;
      try {
        const payload = await resp.json();
        if (payload.error) {
          detail = `${payload.error.codeName || "?"} — ${payload.error.message || ""}`;
        }
      } catch { /* non-JSON error body */ }
      throw new Error(detail);
    }
    const blob = await resp.blob();
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = file.name.replace(/\.[^.]*$/, "") + ".pdf";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 30000);
    els.pdfTime.textContent =
      `${formatMs(performance.now() - t0)} \u00b7 ${formatBytes(blob.size)}`;
  } catch (err) {
    els.pdfTime.textContent = "";
    showError("PDF CONVERSION FAILED", String(err.message || err));
  } finally {
    clearInterval(tick);
    els.pdfButton.disabled = false;
  }
});

/* ---------- lightbox + overlay (Feature 3) ---------- */

function openLightbox(pos) {
  if (pos < 0 || pos >= state.pages.length) return;
  state.lightboxIndex = pos;
  const page = state.pages[pos];
  els.lbImage.src = page.dataUrl;
  const boxes = state.lineBoxes.get(page.index) || [];
  els.lbCaption.textContent =
    `Page ${page.index + 1} of ${state.expectedPages ?? state.pages.length}` +
    ` \u00b7 ${page.widthPx}\u00d7${page.heightPx} @ ${page.dpi} dpi` +
    (boxes.length ? ` \u00b7 ${boxes.length} line boxes` : "");
  els.lbPrev.disabled = pos === 0;
  els.lbNext.disabled = pos === state.pages.length - 1;
  els.lbOverlayToggle.disabled = state.lineBoxes.size === 0;
  els.lightbox.classList.remove("hidden");
  drawOverlay();
}

// Draws the current page's line boxes over the image. LineBox coordinates
// are document-absolute twips; the page's PageRect origin maps them to
// page-local twips, and dpi/1440 maps those to the PNG's pixel space. The
// SVG viewBox is that pixel space, so CSS scaling of the displayed image
// applies to the boxes for free.
function drawOverlay() {
  const svg = els.lbOverlaySvg;
  svg.innerHTML = "";
  const pos = state.lightboxIndex;
  if (!state.overlayOn || pos < 0) {
    svg.classList.add("hidden");
    return;
  }
  const page = state.pages[pos];
  const pageRect = state.pageRects[page.index];
  const boxes = state.lineBoxes.get(page.index) || [];
  if (!pageRect || !boxes.length) {
    svg.classList.add("hidden");
    return;
  }
  svg.setAttribute("viewBox", `0 0 ${page.widthPx} ${page.heightPx}`);
  const scale = page.dpi / 1440; // twips -> px at the rendered dpi
  for (const b of boxes) {
    const rect = document.createElementNS(SVG_NS, "rect");
    rect.setAttribute("x", ((b.x - Number(pageRect.xTwips)) * scale).toFixed(1));
    rect.setAttribute("y", ((b.y - Number(pageRect.yTwips)) * scale).toFixed(1));
    rect.setAttribute("width", (b.w * scale).toFixed(1));
    rect.setAttribute("height", (b.h * scale).toFixed(1));
    svg.appendChild(rect);
  }
  svg.classList.remove("hidden");
}

els.lbOverlayToggle.addEventListener("click", () => {
  state.overlayOn = !state.overlayOn;
  els.lbOverlayToggle.setAttribute("aria-pressed", String(state.overlayOn));
  drawOverlay();
});

function closeLightbox() {
  els.lightbox.classList.add("hidden");
  state.lightboxIndex = -1;
}

els.lbClose.addEventListener("click", closeLightbox);
els.lbPrev.addEventListener("click", () => openLightbox(state.lightboxIndex - 1));
els.lbNext.addEventListener("click", () => openLightbox(state.lightboxIndex + 1));
els.lightbox.addEventListener("click", (e) => {
  if (e.target === els.lightbox) closeLightbox();
});
document.addEventListener("keydown", (e) => {
  if (els.lightbox.classList.contains("hidden")) return;
  if (e.key === "Escape") closeLightbox();
  else if (e.key === "ArrowLeft") openLightbox(state.lightboxIndex - 1);
  else if (e.key === "ArrowRight") openLightbox(state.lightboxIndex + 1);
  else if (e.key === "o" || e.key === "O") els.lbOverlayToggle.click();
});

/* ---------- speed test (Feature 4) ---------- */

const speed = {
  fixtures: null, // [{name, bytes}] once loaded
  bytesCache: new Map(), // name -> ArrayBuffer
  running: false,
};

async function loadFixtures() {
  if (speed.fixtures) return;
  try {
    const resp = await fetch(apiUrl("/api/fixtures"));
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const { files } = await resp.json();
    speed.fixtures = files;
    els.fixtureList.innerHTML = "";
    for (const f of files) {
      const label = document.createElement("label");
      label.className = "check-row";
      const cb = document.createElement("input");
      cb.type = "checkbox";
      cb.checked = true;
      cb.dataset.name = f.name;
      const name = document.createElement("span");
      name.textContent = f.name;
      const size = document.createElement("span");
      size.className = "fx-size";
      size.textContent = formatBytes(f.bytes);
      label.appendChild(cb);
      label.appendChild(name);
      label.appendChild(size);
      els.fixtureList.appendChild(label);
    }
  } catch (err) {
    els.fixtureList.innerHTML = "";
    const chip = document.createElement("span");
    chip.className = "chip chip-bad";
    chip.textContent = "could not list fixtures: " + String(err.message || err);
    els.fixtureList.appendChild(chip);
  }
}

async function fixtureBytes(name) {
  let bytes = speed.bytesCache.get(name);
  if (!bytes) {
    const resp = await fetch(apiUrl("/api/fixtures/") + encodeURIComponent(name));
    if (!resp.ok) throw new Error(`fixture fetch HTTP ${resp.status}`);
    bytes = await resp.arrayBuffer();
    speed.bytesCache.set(name, bytes);
  }
  return bytes;
}

const SPEED_MODES = [
  { key: "pages", label: "pages-only", el: () => els.modePages,
    endpoint: "render", query: "&parts=PAGES" },
  { key: "full", label: "full", el: () => els.modeFull,
    endpoint: "render", query: "" },
  { key: "pdf", label: "pdf", el: () => els.modePdf,
    endpoint: "pdf", query: "" },
];

// One measured request. Returns {ttfpMs, totalMs, pages, bytes}; for pdf
// mode ttfpMs/pages stay null.
async function speedRun(name, body, mode) {
  const t0 = performance.now();
  const resp = await fetch(
    `${apiUrl("/api/")}${mode.endpoint}?filename=${encodeURIComponent(name)}${mode.query}`,
    { method: "POST", body });
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

  const reader = resp.body.getReader();
  const result = { ttfpMs: null, totalMs: 0, pages: null, bytes: 0 };

  if (mode.endpoint === "pdf") {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      result.bytes += value.byteLength;
    }
    result.totalMs = performance.now() - t0;
    return result;
  }

  const decoder = new TextDecoder();
  let buffer = "";
  result.pages = 0;
  let sawError = null;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    result.bytes += value.byteLength;
    buffer += decoder.decode(value, { stream: true });
    let nl;
    while ((nl = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, nl).trim();
      buffer = buffer.slice(nl + 1);
      if (!line) continue;
      const evt = JSON.parse(line);
      if (evt.event === "pageImage") {
        result.pages++;
        if (result.ttfpMs == null) result.ttfpMs = performance.now() - t0;
      } else if (evt.event === "error") {
        sawError = evt.data;
      }
    }
  }
  result.totalMs = performance.now() - t0;
  if (sawError) {
    throw new Error(`${sawError.codeName}: ${sawError.message}`);
  }
  return result;
}

function avg(list) {
  return list.length
    ? list.reduce((a, b) => a + b, 0) / list.length
    : null;
}

els.speedRun.addEventListener("click", async () => {
  if (speed.running) return;
  const picked = [...els.fixtureList.querySelectorAll("input:checked")]
    .map((cb) => cb.dataset.name);
  const modes = SPEED_MODES.filter((m) => m.el().checked);
  const iterations = Math.max(1,
    Math.min(20, Number(els.speedIterations.value) || 2));
  if (!picked.length || !modes.length) {
    els.speedStatus.textContent = "pick at least one fixture and one mode";
    return;
  }

  speed.running = true;
  els.speedRun.disabled = true;
  els.speedResults.classList.add("hidden");
  els.resultsTbody.innerHTML = "";
  els.barChart.innerHTML = "";

  // rows: one per fixture x mode, aggregated over iterations
  const rows = [];
  const totalRuns = picked.length * modes.length * iterations;
  let doneRuns = 0;
  try {
    for (const name of picked) {
      const body = await fixtureBytes(name);
      for (const mode of modes) {
        const runs = [];
        let error = null;
        for (let i = 0; i < iterations; i++) {
          doneRuns++;
          els.speedStatus.textContent =
            `${name} \u00b7 ${mode.label} \u00b7 run ${i + 1}/${iterations}` +
            ` (${doneRuns}/${totalRuns})`;
          try {
            runs.push(await speedRun(name, body, mode));
          } catch (err) {
            error = String(err.message || err);
            break;
          }
        }
        rows.push({ name, mode, runs, error });
      }
    }
  } finally {
    speed.running = false;
    els.speedRun.disabled = false;
    els.speedStatus.textContent = "done";
  }
  renderSpeedResults(rows);
});

function renderSpeedResults(rows) {
  els.resultsTbody.innerHTML = "";
  for (const row of rows) {
    const tr = document.createElement("tr");
    const cells = [];
    cells.push(row.name, row.mode.label);
    if (row.error) {
      cells.push(String(row.runs.length), "-", "-", "-", "-");
    } else {
      const totalMs = avg(row.runs.map((r) => r.totalMs));
      const ttfp = avg(row.runs.map((r) => r.ttfpMs).filter((v) => v != null));
      const pps = avg(row.runs
        .filter((r) => r.pages != null && r.totalMs > 0)
        .map((r) => r.pages / (r.totalMs / 1000)));
      const bytes = avg(row.runs.map((r) => r.bytes));
      cells.push(
        String(row.runs.length),
        ttfp != null ? formatMs(ttfp) : "-",
        formatMs(totalMs),
        pps != null ? pps.toFixed(2) : "-",
        formatBytes(bytes));
    }
    cells.forEach((text, i) => {
      const td = document.createElement("td");
      td.textContent = text;
      tr.appendChild(td);
    });
    if (row.error) {
      const td = tr.lastChild;
      td.textContent = row.error;
      td.className = "err";
      td.style.textAlign = "left";
    }
    els.resultsTbody.appendChild(tr);
  }

  // horizontal CSS bar chart of avg total ms per fixture/mode
  els.barChart.innerHTML = "";
  const bars = rows
    .filter((r) => !r.error && r.runs.length)
    .map((r) => ({
      label: r.name,
      mode: r.mode,
      ms: avg(r.runs.map((x) => x.totalMs)),
    }));
  const maxMs = Math.max(1, ...bars.map((b) => b.ms));
  for (const b of bars) {
    const row = document.createElement("div");
    row.className = "bar-row";
    const label = document.createElement("div");
    label.className = "bar-label";
    label.innerHTML = `<b>${escapeHtml(b.label)}</b> \u00b7 ${escapeHtml(b.mode.label)}`;
    const track = document.createElement("div");
    track.className = "bar-track";
    const fill = document.createElement("div");
    fill.className = "bar-fill" +
      (b.mode.key === "pdf" ? " bar-pdf" : b.mode.key === "pages" ? " bar-pages" : "");
    track.appendChild(fill);
    const value = document.createElement("div");
    value.className = "bar-value";
    value.textContent = formatMs(b.ms);
    row.appendChild(label);
    row.appendChild(track);
    row.appendChild(value);
    els.barChart.appendChild(row);
    requestAnimationFrame(() => {
      fill.style.width = `${(b.ms / maxMs) * 100}%`;
    });
  }
  els.speedResults.classList.remove("hidden");
}

/* ---------- boot ---------- */

loadServiceInfo();
