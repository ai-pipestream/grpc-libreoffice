"""Unit tests for the pure logic in bench.py; no server needed.

bench.py imports generated stubs from bench/gen. Run through ./test.sh,
which generates the stubs the same way run.sh does; when gen/ is absent
(and test.sh was bypassed) the whole module skips with a pointer.
"""

import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
if not os.path.isfile(
        os.path.join(HERE, "gen", "ai", "pipestream", "office", "v1",
                     "office_service_pb2.py")):
    pytest.skip(
        "bench/gen stubs missing; run bench/test.sh (or bench/run.sh) once",
        allow_module_level=True)

sys.path.insert(0, HERE)

import bench  # noqa: E402
from ai.pipestream.office.v1 import office_service_pb2 as pb  # noqa: E402


@pytest.fixture
def make_file(tmp_path):
    def _make(name, size):
        p = tmp_path / name
        p.write_bytes(os.urandom(size) if size else b"")
        return str(p)
    return _make


class TestUploadRequests:
    def test_large_file_chunking(self, make_file):
        size = bench.CHUNK_BYTES + 10
        path = make_file("big.docx", size)
        reqs = list(bench.upload_requests(path, pb.StreamPagesRequest))

        assert len(reqs) == 2
        assert len(reqs[0].chunk.data) == bench.CHUNK_BYTES
        assert len(reqs[1].chunk.data) == 10
        # only the last chunk is complete
        assert [r.chunk.complete for r in reqs] == [False, True]
        # filename only on the first chunk
        assert reqs[0].chunk.filename == "big.docx"
        assert reqs[1].chunk.filename == ""
        # data round-trips
        with open(path, "rb") as f:
            assert b"".join(r.chunk.data for r in reqs) == f.read()

    def test_exact_chunk_boundary_single_chunk(self, make_file):
        path = make_file("even.docx", bench.CHUNK_BYTES)
        reqs = list(bench.upload_requests(path, pb.StreamPagesRequest))
        assert len(reqs) == 1
        assert reqs[0].chunk.complete is True

    def test_empty_file_yields_one_complete_chunk(self, make_file):
        path = make_file("empty.docx", 0)
        reqs = list(bench.upload_requests(path, pb.StreamPagesRequest))
        assert len(reqs) == 1
        assert reqs[0].chunk.complete is True
        assert reqs[0].chunk.data == b""
        assert reqs[0].chunk.filename == "empty.docx"

    def test_options_only_on_first_stream_pages_request(self, make_file):
        path = make_file("big.docx", bench.CHUNK_BYTES * 2 + 1)
        options = pb.StreamOptions(parts=[pb.DOCUMENT_PART_PAGES])
        reqs = list(
            bench.upload_requests(path, pb.StreamPagesRequest, options))
        assert len(reqs) == 3
        assert reqs[0].HasField("options")
        assert list(reqs[0].options.parts) == [pb.DOCUMENT_PART_PAGES]
        assert not any(r.HasField("options") for r in reqs[1:])

    def test_options_ignored_for_convert_to_pdf(self, make_file):
        # ConvertToPdfRequest has no options field; upload_requests must not
        # try to set it even when options are passed.
        path = make_file("doc.docx", 100)
        options = pb.StreamOptions(parts=[pb.DOCUMENT_PART_PAGES])
        reqs = list(
            bench.upload_requests(path, pb.ConvertToPdfRequest, options))
        assert len(reqs) == 1
        assert reqs[0].chunk.complete is True

    def test_no_options_means_field_unset(self, make_file):
        path = make_file("doc.docx", 100)
        reqs = list(bench.upload_requests(path, pb.StreamPagesRequest))
        assert not reqs[0].HasField("options")


class TestFmtMs:
    def test_none_is_dash(self):
        assert bench.fmt_ms(None) == "-"

    def test_seconds_to_whole_milliseconds(self):
        assert bench.fmt_ms(0.1234) == "123"
        assert bench.fmt_ms(1.0) == "1000"
        assert bench.fmt_ms(0) == "0"


class TestP50:
    def test_odd_count(self):
        assert bench.p50([3, 1, 2]) == 2

    def test_even_count_averages_middle_pair(self):
        assert bench.p50([1, 2, 3, 4]) == 2.5

    def test_single_value(self):
        assert bench.p50([7]) == 7


class TestTable:
    def test_layout(self):
        out = bench.table([["a", 10], ["bbbb", 2]], ["col", "n"])
        lines = out.split("\n")
        assert len(lines) == 4  # header, rule, two rows
        # column width follows the widest cell, values right-justified
        assert lines[0] == " col   n"
        assert lines[1] == "----  --"
        assert lines[2] == "   a  10"
        assert lines[3] == "bbbb   2"

    def test_header_wider_than_rows(self):
        out = bench.table([["x", 1]], ["longheader", "n"])
        lines = out.split("\n")
        assert lines[0] == "longheader  n"
        assert lines[2] == "         x  1"
