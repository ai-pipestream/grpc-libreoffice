#!/usr/bin/env python3
"""Example CLI client for the grpc-libreoffice OfficeRenderService.

Subcommands:
  info                     print server versions, limits, accepted formats
  pages <file> [outdir]    render every page to PNG, dump typed-content stats
  pdf <file> [out.pdf]     convert the document to a PDF

The server address defaults to localhost:50053; override with --addr or the
GRLIBRE_ADDR environment variable.
"""

import argparse
import mimetypes
import os
import sys
import time
import uuid

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen"))

import grpc  # noqa: E402
from ai.pipestream.office.v1 import office_service_pb2 as pb  # noqa: E402
from ai.pipestream.office.v1 import office_service_pb2_grpc as pb_grpc  # noqa: E402

CHUNK_SIZE = 256 * 1024
# Page PNGs and embedded images can easily exceed gRPC's 4 MiB default.
MAX_MESSAGE_BYTES = 128 * 1024 * 1024


def make_stub(addr):
    channel = grpc.insecure_channel(
        addr,
        options=[("grpc.max_receive_message_length", MAX_MESSAGE_BYTES)],
    )
    return pb_grpc.OfficeRenderServiceStub(channel)


def chunk_requests(path, wrap):
    """Yield upload requests for `path`, 256 KiB per chunk, last one complete."""
    filename = os.path.basename(path)
    content_type = mimetypes.guess_type(filename)[0] or ""
    doc_id = str(uuid.uuid4())
    with open(path, "rb") as f:
        data = f.read()
    offset, first = 0, True
    while True:
        end = min(offset + CHUNK_SIZE, len(data))
        chunk = pb.DocumentChunk(data=data[offset:end], complete=(end == len(data)))
        if first:
            chunk.document_id = doc_id
            chunk.filename = filename
            chunk.content_type = content_type
            first = False
        yield wrap(chunk)
        offset = end
        if offset >= len(data):
            return


def print_document_info(info):
    print(f"document_id : {info.document_id}")
    print(f"format      : {info.source_format}")
    print(f"type        : {info.document_type}")
    print(f"pages       : {info.page_count}")


def print_render_status(status, total_s):
    print(f"status      : {pb.RenderStatus.State.Name(status.state)}")
    print(f"input bytes : {status.input_bytes:,}")
    print(f"output bytes: {status.output_bytes:,}")
    print(f"render time : {status.render_millis} ms (server) / {total_s * 1000:.0f} ms (wall)")
    for w in status.warnings:
        print(f"warning     : {w}")


def cmd_info(stub, args):
    resp = stub.GetServiceInfo(pb.GetServiceInfoRequest())
    print(f"service version     : {resp.service_version}")
    print(f"libreoffice version : {resp.libreoffice_version}")
    print(f"api version         : {resp.api_version}")
    print(f"max document bytes  : {resp.max_document_bytes:,}")
    print(f"max concurrent docs : {resp.max_concurrent_documents}")
    print(f"render dpi          : {resp.render_dpi}")
    print(f"typed content       : {resp.typed_content}")
    print(f"diskless documents  : {resp.diskless_documents}")
    print(f"supported formats   : {', '.join(resp.supported_formats)}")
    if resp.internal_temp_artifacts:
        print(f"internal temp files : {', '.join(resp.internal_temp_artifacts)}")


def cmd_pages(stub, args):
    outdir = args.outdir
    os.makedirs(outdir, exist_ok=True)
    t0 = time.monotonic()
    first_page_s = None
    pages = 0
    counts = {}
    responses = stub.StreamPages(
        chunk_requests(args.file, lambda c: pb.StreamPagesRequest(chunk=c))
    )
    for resp in responses:
        event = resp.WhichOneof("event")
        if event == "document_info":
            print_document_info(resp.document_info)
        elif event == "page_image":
            if first_page_s is None:
                first_page_s = time.monotonic() - t0
            img = resp.page_image
            name = f"page-{img.index + 1:04d}.png"
            with open(os.path.join(outdir, name), "wb") as f:
                f.write(img.png)
            pages += 1
            print(f"  {name}  {img.width_px}x{img.height_px}px @ {img.dpi} dpi  "
                  f"{len(img.png):,} bytes")
        elif event == "status":
            total_s = time.monotonic() - t0
            print()
            if counts:
                width = max(len(k) for k in counts)
                print("typed content events:")
                for kind in sorted(counts):
                    print(f"  {kind:<{width}}  {counts[kind]}")
                print()
            print_render_status(resp.status, total_s)
            if first_page_s is not None:
                print(f"first page  : {first_page_s * 1000:.0f} ms")
            print(f"pages saved : {pages} -> {outdir}/")
        else:
            counts[event] = counts.get(event, 0) + 1


def cmd_pdf(stub, args):
    t0 = time.monotonic()
    total = 0
    responses = stub.ConvertToPdf(
        chunk_requests(args.file, lambda c: pb.ConvertToPdfRequest(chunk=c))
    )
    with open(args.out, "wb") as f:
        for resp in responses:
            event = resp.WhichOneof("event")
            if event == "document_info":
                print_document_info(resp.document_info)
            elif event == "pdf_chunk":
                f.write(resp.pdf_chunk.data)
                total += len(resp.pdf_chunk.data)
            elif event == "status":
                total_s = time.monotonic() - t0
                print_render_status(resp.status, total_s)
    print(f"wrote       : {total:,} bytes -> {args.out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--addr", default=os.environ.get("GRLIBRE_ADDR", "localhost:50053"),
                        help="server address (default localhost:50053)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="print server capabilities")

    p = sub.add_parser("pages", help="render every page as PNG")
    p.add_argument("file")
    p.add_argument("outdir", nargs="?", default="pages-out")

    p = sub.add_parser("pdf", help="convert to PDF")
    p.add_argument("file")
    p.add_argument("out", nargs="?", default="out.pdf")

    args = parser.parse_args()
    stub = make_stub(args.addr)
    handlers = {"info": cmd_info, "pages": cmd_pages, "pdf": cmd_pdf}
    try:
        handlers[args.cmd](stub, args)
    except grpc.RpcError as e:
        print(f"gRPC error: {e.code().name}: {e.details()}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
