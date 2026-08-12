"use strict";

/* ---------- element handles ---------- */

const $ = (id) => document.getElementById(id);

const els = {
  chips: $("info-chips"),
  dropZone: $("drop-zone"),
  dropFormats: $("drop-formats"),
  fileInput: $("file-input"),
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
  statusPanel: $("status-panel"),
  statusBody: $("status-body"),
  warnCount: $("warn-count"),
  lightbox: $("lightbox"),
  lbImage: $("lb-image"),
  lbCaption: $("lb-caption"),
  lbPrev: $("lb-prev"),
  lbNext: $("lb-next"),
  lbClose: $("lb-close"),
};

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
  liveTimer: null,
  startedAt: 0,
  lightboxIndex: -1,
};

/* ---------- service info ---------- */

async function loadServiceInfo() {
  try {
    const resp = await fetch("/api/info");
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

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]));
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

  hideError();
  closeLightbox();
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

  if (state.liveTimer) clearInterval(state.liveTimer);
  state.liveTimer = setInterval(() => {
    if (state.rendering) {
      els.statTotal.textContent =
        formatMs(performance.now() - state.startedAt);
    }
  }, 100);

  try {
    const resp = await fetch(
      "/api/render?filename=" + encodeURIComponent(file.name),
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
      break;
  }
}

function onDocumentInfo(info) {
  els.docCard.classList.remove("hidden");
  els.docFormat.textContent = info.sourceFormat || "?";
  els.docClass.textContent = info.documentType || "?";
  state.expectedPages = info.pageCount;
  els.docPages.textContent =
    `${info.pageCount} page${info.pageCount === 1 ? "" : "s"}`;
  els.statPages.textContent = `0 / ${info.pageCount}`;
}

function onPageImage(page, tMs) {
  if (state.firstPageMs == null) {
    state.firstPageMs = tMs;
    els.statFirstPage.textContent = formatMs(tMs);
  }
  const dataUrl = "data:image/png;base64," + page.png;
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

  els.statPages.textContent = state.expectedPages != null
    ? `${state.pages.length} / ${state.expectedPages}`
    : String(state.pages.length);
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
      "/api/pdf?filename=" + encodeURIComponent(file.name),
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

/* ---------- lightbox ---------- */

function openLightbox(pos) {
  if (pos < 0 || pos >= state.pages.length) return;
  state.lightboxIndex = pos;
  const page = state.pages[pos];
  els.lbImage.src = page.dataUrl;
  els.lbCaption.textContent =
    `Page ${page.index + 1} of ${state.expectedPages ?? state.pages.length}` +
    ` \u00b7 ${page.widthPx}\u00d7${page.heightPx} @ ${page.dpi} dpi`;
  els.lbPrev.disabled = pos === 0;
  els.lbNext.disabled = pos === state.pages.length - 1;
  els.lightbox.classList.remove("hidden");
}

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
});

/* ---------- boot ---------- */

loadServiceInfo();
