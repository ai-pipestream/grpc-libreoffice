// UI_BASE base-path tests: boots server.mjs as a child process with
// UI_BASE=/ui/libreoffice on an ephemeral port and checks the SPA, its
// static assets, and the API routes are served under the base. No gRPC
// backend needed: every assertion exercises BFF-local routing.
//
//   node --test test/ui-base.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SERVER = path.join(__dirname, "..", "server.mjs");
const UI_BASE = "/ui/libreoffice";

let child;
let base; // http://localhost:<port>

before(async () => {
  child = spawn(process.execPath, [SERVER], {
    env: { ...process.env, PORT: "0", UI_BASE },
    stdio: ["ignore", "pipe", "inherit"],
  });
  base = await new Promise((resolve, reject) => {
    let out = "";
    const timer = setTimeout(
      () => reject(new Error("BFF did not report its port in 10s")), 10000);
    child.stdout.on("data", (buf) => {
      out += buf.toString();
      const m = out.match(/listening on (http:\/\/localhost:\d+)/);
      if (m) {
        clearTimeout(timer);
        resolve(m[1]);
      }
    });
    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`BFF exited early with code ${code}`));
    });
  });
});

after(() => {
  if (child) child.kill("SIGTERM");
});

test("GET $UI_BASE/ serves the SPA with the base injected", async () => {
  const resp = await fetch(`${base}${UI_BASE}/`);
  assert.strictEqual(resp.status, 200);
  assert.match(resp.headers.get("content-type"), /text\/html/);
  const body = await resp.text();
  assert.match(body, /grlibre/);
  assert.ok(body.includes(`<base href="${UI_BASE}/">`),
    "served HTML carries a <base> tag for the mount prefix");
  assert.ok(
    body.includes(`window.__UI_BASE__ = ${JSON.stringify(UI_BASE)}`),
    "served HTML hands the base to app.js");
});

test("GET $UI_BASE (no trailing slash) also serves the SPA", async () => {
  const resp = await fetch(`${base}${UI_BASE}`);
  assert.strictEqual(resp.status, 200);
  const body = await resp.text();
  assert.ok(body.includes(`<base href="${UI_BASE}/">`),
    "<base> makes the relative asset refs resolve without the slash");
});

test("static assets resolve under the base", async () => {
  const js = await fetch(`${base}${UI_BASE}/app.js`);
  assert.strictEqual(js.status, 200);
  assert.match(js.headers.get("content-type"), /javascript/);
  const body = await js.text();
  assert.match(body, /apiUrl\("\/api\/info"\)/,
    "app.js prefixes its fetches with the injected base");

  const css = await fetch(`${base}${UI_BASE}/style.css`);
  assert.strictEqual(css.status, 200);
  assert.match(css.headers.get("content-type"), /text\/css/);
});

test("GET $UI_BASE/api/fixtures is routed under the base", async () => {
  const resp = await fetch(`${base}${UI_BASE}/api/fixtures`);
  assert.strictEqual(resp.status, 200);
  const { files } = await resp.json();
  assert.ok(Array.isArray(files));
  assert.ok(files.some((f) => f.name === "sample3.docx"));
});

test("GET $UI_BASE/api/fixtures/<name> serves bytes under the base", async () => {
  const resp = await fetch(`${base}${UI_BASE}/api/fixtures/sample3.docx`);
  assert.strictEqual(resp.status, 200);
  const bytes = Buffer.from(await resp.arrayBuffer());
  const fixture = path.join(__dirname, "..", "..", "fixtures", "sample3.docx");
  assert.strictEqual(bytes.length, fs.statSync(fixture).size);
});

test("POST $UI_BASE/api/render validates query params under the base", async () => {
  // parts=NOT_A_PART is rejected by the BFF's own parser (HTTP 400) before
  // any gRPC call: proves the route is mounted under the base.
  const resp = await fetch(
    `${base}${UI_BASE}/api/render?filename=x.docx&parts=NOT_A_PART`,
    { method: "POST", body: Buffer.from("x") });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /unknown document part/);
});

test("requests outside the base get a 404 from the router", async () => {
  assert.strictEqual((await fetch(`${base}/`)).status, 404);
  assert.strictEqual((await fetch(`${base}/app.js`)).status, 404);
  assert.strictEqual((await fetch(`${base}/api/fixtures`)).status, 404);
});
