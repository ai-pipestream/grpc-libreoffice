// Unit tests for lightbox overlay math. The SPA uses the same
// dpi/1440 scale inline in public/app.js.
import { test } from "node:test";
import assert from "node:assert/strict";
import {
  TWIPS_PER_INCH,
  twipsToPx,
  pageLocalBox,
  boxInsidePage,
  expectedPagePixels,
} from "../lib/overlay.mjs";

test("twipsToPx is dpi/1440", () => {
  assert.equal(TWIPS_PER_INCH, 1440);
  assert.equal(twipsToPx(1440, 72), 72);
  assert.equal(twipsToPx(1440, 144), 144);
  assert.equal(twipsToPx(720, 144), 72);
  assert.equal(twipsToPx(0, 144), 0);
});

test("pageLocalBox subtracts the page origin", () => {
  const pageRect = { xTwips: 100, yTwips: 200, widthTwips: 1000, heightTwips: 800 };
  const box = { xTwips: 150, yTwips: 260, widthTwips: 40, heightTwips: 20 };
  assert.deepEqual(pageLocalBox(box, pageRect), {
    x: 50, y: 60, width: 40, height: 20,
  });
});

test("boxInsidePage accepts on-page boxes and rejects overflow", () => {
  const pageRect = { xTwips: 0, yTwips: 0, widthTwips: 100, heightTwips: 100 };
  assert.ok(boxInsidePage(
    { xTwips: 0, yTwips: 0, widthTwips: 100, heightTwips: 100 }, pageRect));
  assert.ok(boxInsidePage(
    { xTwips: 10, yTwips: 10, widthTwips: 20, heightTwips: 20 }, pageRect));
  assert.ok(!boxInsidePage(
    { xTwips: -1, yTwips: 0, widthTwips: 10, heightTwips: 10 }, pageRect));
  assert.ok(!boxInsidePage(
    { xTwips: 90, yTwips: 0, widthTwips: 20, heightTwips: 10 }, pageRect));
  assert.ok(!boxInsidePage(
    { xTwips: 0, yTwips: 90, widthTwips: 10, heightTwips: 20 }, pageRect));
});

test("expectedPagePixels matches letter at 144 dpi", () => {
  // US Letter in twips: 12240 x 15840.
  const letter = { widthTwips: 12240, heightTwips: 15840 };
  const px = expectedPagePixels(letter, 144);
  assert.equal(px.width, 1224);
  assert.equal(px.height, 1584);
});
