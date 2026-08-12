"""Speed test for the grlibre OfficeRenderService.

Streams every fixture through the server in three modes and reports latency
and throughput:

  pages-only  StreamPages with parts=[PAGES]  (pure render speed)
  pages-full  StreamPages with no selection   (render + full typed extraction)
  pdf         ConvertToPdf

Per request it measures time to DocumentInfo (ttfb), time to first page
(ttfp), total wall time, page count, and output bytes, plus the worker's own
render_millis. A concurrency sweep then replays the whole fixture set at
1..N parallel requests and reports docs/sec and pages/sec.

Run through ./run.sh, which owns the venv and stub generation.
"""

import argparse
import concurrent.futures
import glob
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "gen"))

import grpc  # noqa: E402
from ai.pipestream.office.v1 import office_service_pb2 as pb  # noqa: E402
from ai.pipestream.office.v1 import office_service_pb2_grpc as pb_grpc  # noqa: E402

CHUNK_BYTES = 256 * 1024


def upload_requests(path, request_cls, options=None):
    filename = os.path.basename(path)
    with open(path, "rb") as f:
        data = f.read()
    total = len(data)
    offset = 0
    first = True
    while offset < total or first:
        end = min(offset + CHUNK_BYTES, total)
        chunk = pb.DocumentChunk(
            filename=filename if first else "",
            data=data[offset:end],
            complete=(end >= total),
        )
        kwargs = {"chunk": chunk}
        if first and options is not None and request_cls is pb.StreamPagesRequest:
            kwargs["options"] = options
        yield request_cls(**kwargs)
        first = False
        offset = end


def run_pages(stub, path, pages_only, dpi=0, page_range=(0, 0)):
    options = None
    if pages_only or dpi or page_range != (0, 0):
        parts = [pb.DOCUMENT_PART_PAGES] if pages_only else []
        options = pb.StreamOptions(parts=parts, render_dpi=dpi,
                                   first_page=page_range[0],
                                   last_page=page_range[1])
    start = time.perf_counter()
    m = {"ttfb": None, "ttfp": None, "pages": 0, "out_bytes": 0,
         "events": 0, "render_millis": None}
    for resp in stub.StreamPages(
            upload_requests(path, pb.StreamPagesRequest, options)):
        now = time.perf_counter() - start
        m["events"] += 1
        kind = resp.WhichOneof("event")
        if kind == "document_info" and m["ttfb"] is None:
            m["ttfb"] = now
        elif kind == "page_image":
            if m["ttfp"] is None:
                m["ttfp"] = now
            m["pages"] += 1
            m["out_bytes"] += len(resp.page_image.png)
        elif kind == "status":
            m["render_millis"] = resp.status.render_millis
    m["total"] = time.perf_counter() - start
    return m


def run_pdf(stub, path):
    start = time.perf_counter()
    m = {"ttfb": None, "ttfp": None, "pages": 0, "out_bytes": 0,
         "events": 0, "render_millis": None}
    for resp in stub.ConvertToPdf(
            upload_requests(path, pb.ConvertToPdfRequest)):
        now = time.perf_counter() - start
        m["events"] += 1
        kind = resp.WhichOneof("event")
        if kind == "document_info":
            m["ttfb"] = now
            m["pages"] = resp.document_info.page_count
        elif kind == "pdf_chunk":
            if m["ttfp"] is None:
                m["ttfp"] = now
            m["out_bytes"] += len(resp.pdf_chunk.data)
        elif kind == "status":
            m["render_millis"] = resp.status.render_millis
    m["total"] = time.perf_counter() - start
    return m


def run_one(stub, path, mode, dpi=0, page_range=(0, 0)):
    if mode == "pdf":
        return run_pdf(stub, path)
    return run_pages(stub, path, pages_only=(mode == "pages-only"), dpi=dpi,
                     page_range=page_range)


def fmt_ms(seconds):
    if seconds is None:
        return "-"
    return f"{seconds * 1000:.0f}"


def p50(values):
    return statistics.median(values)


def table(rows, headers):
    widths = [max(len(str(r[i])) for r in rows + [headers])
              for i in range(len(headers))]
    def line(cells):
        return "  ".join(str(c).rjust(w) for c, w in zip(cells, widths))
    out = [line(headers), line(["-" * w for w in widths])]
    out += [line(r) for r in rows]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", default="localhost:50053")
    ap.add_argument("--files", nargs="*",
                    default=sorted(glob.glob(os.path.join(
                        os.path.dirname(__file__), "..", "fixtures", "*"))))
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--modes", default="pages-only,pages-full,pdf")
    ap.add_argument("--concurrency", default="1,2,4",
                    help="comma list for the throughput sweep; empty to skip")
    ap.add_argument("--dpi", type=int, default=0,
                    help="per-request render DPI override for the pages "
                         "modes (StreamOptions.render_dpi); 0 = server "
                         "default")
    ap.add_argument("--pages", default="",
                    help="1-based inclusive page range for the pages modes "
                         "(StreamOptions first_page/last_page), e.g. 2:5, "
                         "7 (just page 7), or 100: (open end)")
    ap.add_argument("--json", dest="json_out", default=None)
    args = ap.parse_args()

    page_range = (0, 0)
    if args.pages:
        first, sep, last = args.pages.partition(":")
        page_range = (int(first or 0), int(last or 0) if sep else int(first))

    # run.sh cds into bench/; resolve caller-relative paths against the
    # invoking directory.
    invoke_dir = os.environ.get("BENCH_INVOKE_DIR")

    def resolve(p):
        if invoke_dir and not os.path.isabs(p):
            return os.path.join(invoke_dir, p)
        return p

    files = [f for f in (resolve(p) for p in args.files) if os.path.isfile(f)]
    if not files:
        sys.exit("no fixture files found")
    modes = [m.strip() for m in args.modes.split(",") if m.strip()]

    channel = grpc.insecure_channel(
        args.target,
        options=[("grpc.max_receive_message_length", 64 * 1024 * 1024)])
    stub = pb_grpc.OfficeRenderServiceStub(channel)

    info = stub.GetServiceInfo(pb.GetServiceInfoRequest())
    print(f"target {args.target}  server {info.service_version}  "
          f"{info.libreoffice_version}  "
          f"max_concurrent {info.max_concurrent_documents}")
    print(f"{len(files)} files, {args.iterations} iterations "
          f"(+{args.warmup} warmup), modes: {', '.join(modes)}\n")

    results = {}
    rows = []
    for path in files:
        name = os.path.basename(path)
        size_kib = os.path.getsize(path) / 1024
        for mode in modes:
            try:
                for _ in range(args.warmup):
                    run_one(stub, path, mode, args.dpi, page_range)
                runs = [run_one(stub, path, mode, args.dpi, page_range)
                        for _ in range(args.iterations)]
            except grpc.RpcError as e:
                rows.append([name, mode, f"{size_kib:.0f}", "ERR",
                             e.code().name, "-", "-", "-", "-"])
                continue
            results[(name, mode)] = runs
            pages = runs[0]["pages"]
            total = p50([r["total"] for r in runs])
            rows.append([
                name, mode, f"{size_kib:.0f}", pages,
                fmt_ms(p50([r["ttfb"] for r in runs])),
                fmt_ms(p50([r["ttfp"] for r in runs
                            if r["ttfp"] is not None]) if any(
                    r["ttfp"] is not None for r in runs) else None),
                fmt_ms(total),
                f"{pages / total:.1f}" if pages and total else "-",
                f"{runs[0]['out_bytes'] / 1024:.0f}",
            ])
    print(table(rows, ["file", "mode", "KiB in", "pages", "ttfb ms",
                       "ttfp ms", "total ms", "pages/s", "KiB out"]))

    sweep_rows = []
    levels = [int(c) for c in args.concurrency.split(",") if c.strip()]
    if levels:
        print("\nThroughput sweep (whole fixture set per level, "
              "pages-only mode):")
        for level in levels:
            jobs = files * max(1, args.iterations)
            start = time.perf_counter()
            pages = 0
            failed = 0
            with concurrent.futures.ThreadPoolExecutor(level) as pool:
                futs = [pool.submit(run_one, stub, f, "pages-only", args.dpi)
                        for f in jobs]
                for fut in concurrent.futures.as_completed(futs):
                    try:
                        pages += fut.result()["pages"]
                    except grpc.RpcError:
                        failed += 1
            wall = time.perf_counter() - start
            sweep_rows.append([
                level, len(jobs), failed, f"{wall:.1f}",
                f"{(len(jobs) - failed) / wall:.2f}",
                f"{pages / wall:.1f}",
            ])
        print(table(sweep_rows, ["conc", "docs", "failed", "wall s",
                                 "docs/s", "pages/s"]))

    if args.json_out:
        args.json_out = resolve(args.json_out)
        payload = {
            "target": args.target,
            "server": info.service_version,
            "libreoffice": info.libreoffice_version,
            "per_file": {f"{k[0]}|{k[1]}": v for k, v in results.items()},
            "sweep": sweep_rows,
        }
        with open(args.json_out, "w") as f:
            json.dump(payload, f, indent=2)
        print(f"\nwrote {args.json_out}")


if __name__ == "__main__":
    main()
