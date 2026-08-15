// Query-string parsers and StreamOptions builders for the BFF.
// Kept out of server.mjs so unit tests can exercise them without
// booting HTTP or dialing gRPC.

export const PAGE_FORMATS = {
  png: "PAGE_IMAGE_FORMAT_PNG",
  jpeg: "PAGE_IMAGE_FORMAT_JPEG",
  webp: "PAGE_IMAGE_FORMAT_WEBP",
  svg: "PAGE_IMAGE_FORMAT_SVG",
};

export const TRACKED_CHANGES = {
  "as-is": "TRACKED_CHANGE_DISPLAY_AS_IS",
  final: "TRACKED_CHANGE_DISPLAY_FINAL",
  original: "TRACKED_CHANGE_DISPLAY_ORIGINAL",
  markup: "TRACKED_CHANGE_DISPLAY_SHOW_MARKUP",
  "show-markup": "TRACKED_CHANGE_DISPLAY_SHOW_MARKUP",
};

// Parses ?parts=PAGES,METADATA into full enum names, or null when absent.
// Throws on unknown names. documentPartNames is the proto-derived set of
// DOCUMENT_PART_* tokens.
export function parsePartsParam(url, documentPartNames) {
  const raw = url.searchParams.get("parts");
  if (raw == null || raw.trim() === "") return null;
  const parts = [];
  for (const token of raw.split(",")) {
    const t = token.trim().toUpperCase();
    if (!t) continue;
    const full = t.startsWith("DOCUMENT_PART_") ? t : "DOCUMENT_PART_" + t;
    if (!documentPartNames.has(full)) {
      throw new Error(`unknown document part "${token.trim()}"`);
    }
    parts.push(full);
  }
  return parts.length ? parts : null;
}

// Parses one optional non-negative integer query param, or null when
// absent. Throws on anything that is not a plain base-10 non-negative
// integer (fractions, negatives, exponents, garbage).
export function parseNonNegativeIntParam(url, name) {
  const raw = url.searchParams.get(name);
  if (raw == null || raw.trim() === "") return null;
  const t = raw.trim();
  if (!/^\d+$/.test(t)) {
    throw new Error(`invalid ${name} "${raw}": must be a non-negative integer`);
  }
  return Number(t);
}

export function parseFormatParam(url) {
  const raw = url.searchParams.get("format");
  if (raw == null || raw.trim() === "") return null;
  const format = PAGE_FORMATS[raw.trim().toLowerCase()];
  if (!format) {
    throw new Error(`unknown format "${raw}": expected png, jpeg, webp, or svg`);
  }
  return format;
}

export function parseBoolParam(url, name) {
  const raw = url.searchParams.get(name);
  if (raw == null || raw.trim() === "") return null;
  const t = raw.trim().toLowerCase();
  if (t === "1" || t === "true" || t === "yes") return true;
  if (t === "0" || t === "false" || t === "no") return false;
  throw new Error(`invalid ${name} "${raw}": expected true or false`);
}

export function parseTrackedParam(url) {
  const raw = url.searchParams.get("trackedChanges");
  if (raw == null || raw.trim() === "") return null;
  const value = TRACKED_CHANGES[raw.trim().toLowerCase()];
  if (!value) {
    throw new Error(
      `unknown trackedChanges "${raw}": expected as-is, final, original, or markup`);
  }
  return value;
}

// All StreamPages / ToDocument query knobs. Throws on the first bad token.
export function parseRenderQuery(url, documentPartNames) {
  return {
    parts: parsePartsParam(url, documentPartNames),
    dpi: parseNonNegativeIntParam(url, "dpi"),
    firstPage: parseNonNegativeIntParam(url, "firstPage"),
    lastPage: parseNonNegativeIntParam(url, "lastPage"),
    format: parseFormatParam(url),
    quality: parseNonNegativeIntParam(url, "quality"),
    maxWidth: parseNonNegativeIntParam(url, "maxWidth"),
    grayscale: parseBoolParam(url, "grayscale"),
    timeout: parseNonNegativeIntParam(url, "timeout"),
    trackedChanges: parseTrackedParam(url),
    skipHidden: parseBoolParam(url, "skipHidden"),
    usedRange: parseBoolParam(url, "usedRange"),
    notes: parseBoolParam(url, "notes"),
  };
}

// Builds the StreamOptions object proto-loader expects, or null when
// nothing was set. page_format=svg also sets vectorFormat so the worker
// takes the SVG path even if the enum mapping is the only hint.
export function buildStreamOptions(q) {
  const options = {};
  if (q.parts) options.parts = q.parts;
  if (q.dpi != null) options.renderDpi = q.dpi;
  if (q.firstPage != null) options.firstPage = q.firstPage;
  if (q.lastPage != null) options.lastPage = q.lastPage;
  if (q.format != null) {
    options.pageFormat = q.format;
    if (q.format === "PAGE_IMAGE_FORMAT_SVG") {
      options.vectorFormat = "PAGE_VECTOR_FORMAT_SVG";
    }
  }
  if (q.quality != null) options.pageQuality = q.quality;
  if (q.maxWidth != null) options.maxWidthPx = q.maxWidth;
  if (q.grayscale) options.grayscale = true;
  if (q.timeout != null) options.timeoutSeconds = q.timeout;
  if (q.trackedChanges != null) options.trackedChanges = q.trackedChanges;
  if (q.skipHidden) options.skipHidden = true;
  if (q.usedRange) options.paintUsedRange = true;
  if (q.notes) options.includeNotesPages = true;
  return Object.keys(options).length ? options : null;
}

// ConvertToPdf extras ride on the request itself, not StreamOptions.
export function buildPdfExtras(q) {
  const extra = {};
  if (q.firstPage != null) extra.firstPage = q.firstPage;
  if (q.lastPage != null) extra.lastPage = q.lastPage;
  if (q.timeout != null) extra.timeoutSeconds = q.timeout;
  if (q.trackedChanges != null) extra.trackedChanges = q.trackedChanges;
  if (q.skipHidden) extra.skipHidden = true;
  return Object.keys(extra).length ? extra : null;
}

// Approximate decoded byte size of a base64 string.
export function b64Size(s) {
  if (!s) return 0;
  let padding = 0;
  if (s.endsWith("==")) padding = 2;
  else if (s.endsWith("=")) padding = 1;
  return Math.floor((s.length * 3) / 4) - padding;
}

// Replaces heavyweight byte payloads in typed-content events with their
// sizes so the NDJSON stream stays light. Page images are kept: the UI
// needs them. Returns the number of payload bytes represented by this event.
export function slimEvent(kind, data) {
  let bytes = 0;
  if (kind === "pageImage") {
    bytes = b64Size(data.png);
  } else if (kind === "embeddedImage") {
    bytes = b64Size(data.data);
    data.dataBytes = bytes;
    delete data.data;
  } else if (kind === "embeddedObject") {
    bytes = b64Size(data.replacementImage);
    data.replacementImageBytes = bytes;
    delete data.replacementImage;
  }
  return bytes;
}
