// Manual sanity check of the lightbox overlay math: renders a docx through
// the BFF and verifies that pageRect twips scaled by dpi/1440 match the
// rendered pixel dimensions, and that page-local line boxes fall inside the
// page. Not part of the automated suite (it prints, it doesn't assert).
//   node test/check-overlay-math.mjs [http://localhost:8080]
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const base = process.argv[2] || "http://localhost:8080";
const file = path.join(__dirname, "..", "..", "fixtures", "sample3.docx");

const body = fs.readFileSync(file);
const resp = await fetch(
  `${base}/api/render?filename=sample3.docx&parts=PAGES,PARAGRAPHS,LINE_RECTS`,
  { method: "POST", body });
const events = (await resp.text()).trim().split("\n").map(JSON.parse);

const info = events.find((e) => e.event === "documentInfo").data;
const pages = events.filter((e) => e.event === "pageImage").map((e) => e.data);
console.log("pageRects:", JSON.stringify(info.pageRects));

for (const p of pages) {
  const r = info.pageRects[p.index];
  const ew = (r.widthTwips * p.dpi) / 1440;
  const eh = (r.heightTwips * p.dpi) / 1440;
  console.log(
    `page ${p.index}: rect ${r.widthTwips}x${r.heightTwips}tw @${p.dpi}dpi` +
    ` -> expect ${ew.toFixed(1)}x${eh.toFixed(1)}px, actual ${p.widthPx}x${p.heightPx}px`);
}

const paras = events.filter(
  (e) => e.event === "paragraph" && (e.data.lineRects || []).length);
console.log("paragraphs with lineRects:", paras.length);
let outOfBounds = 0;
for (const e of paras) {
  for (const b of e.data.lineRects) {
    const r = info.pageRects[b.pageIndex];
    if (!r) continue;
    const lx = b.xTwips - r.xTwips;
    const ly = b.yTwips - r.yTwips;
    if (lx < 0 || ly < 0 || lx + b.widthTwips > r.widthTwips ||
        ly + b.heightTwips > r.heightTwips) {
      outOfBounds++;
    }
  }
}
console.log("boxes outside their page rect:", outOfBounds);
for (const e of paras.slice(0, 4)) {
  const b = e.data.lineRects[0];
  const r = info.pageRects[b.pageIndex];
  const p = pages.find((pg) => pg.index === b.pageIndex);
  const px = (v) => ((v * p.dpi) / 1440).toFixed(0);
  console.log(
    `box p${b.pageIndex} local=(${px(b.xTwips - r.xTwips)},${px(b.yTwips - r.yTwips)})px` +
    ` size=${px(b.widthTwips)}x${px(b.heightTwips)}px page=${p.widthPx}x${p.heightPx}px` +
    ` "${(e.data.runs || []).map((r2) => r2.text).join("").slice(0, 40)}"`);
}
