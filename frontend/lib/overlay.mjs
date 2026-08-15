// Lightbox overlay math. LineBox / PageRect coordinates are
// document-absolute twips; dpi/1440 maps them into the painted page's
// pixel space. public/app.js uses the same formula (kept inline because
// the SPA has no module loader).

export const TWIPS_PER_INCH = 1440;

export function twipsToPx(twips, dpi) {
  return (Number(twips) * Number(dpi)) / TWIPS_PER_INCH;
}

// Maps a document-absolute twip box onto its page's local origin.
export function pageLocalBox(box, pageRect) {
  return {
    x: Number(box.xTwips) - Number(pageRect.xTwips),
    y: Number(box.yTwips) - Number(pageRect.yTwips),
    width: Number(box.widthTwips),
    height: Number(box.heightTwips),
  };
}

export function boxInsidePage(box, pageRect) {
  const local = pageLocalBox(box, pageRect);
  return local.x >= 0
    && local.y >= 0
    && local.x + local.width <= Number(pageRect.widthTwips)
    && local.y + local.height <= Number(pageRect.heightTwips);
}

// Expected painted size of a pageRect at the given dpi. The service
// rounds to integer pixels; callers should compare with a 1-px tolerance.
export function expectedPagePixels(pageRect, dpi) {
  return {
    width: twipsToPx(pageRect.widthTwips, dpi),
    height: twipsToPx(pageRect.heightTwips, dpi),
  };
}
