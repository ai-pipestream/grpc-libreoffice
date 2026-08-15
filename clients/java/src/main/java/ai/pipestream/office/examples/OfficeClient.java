package ai.pipestream.office.examples;

import ai.pipestream.office.v1.ConvertToPdfRequest;
import ai.pipestream.office.v1.ConvertToPdfResponse;
import ai.pipestream.office.v1.DocumentChunk;
import ai.pipestream.office.v1.DocumentInfo;
import ai.pipestream.office.v1.DocumentPart;
import ai.pipestream.office.v1.FormFillValue;
import ai.pipestream.office.v1.GetServiceInfoRequest;
import ai.pipestream.office.v1.GetServiceInfoResponse;
import ai.pipestream.office.v1.OfficeRenderServiceGrpc;
import ai.pipestream.office.v1.PageImage;
import ai.pipestream.office.v1.PageImageFormat;
import ai.pipestream.office.v1.PageVectorFormat;
import ai.pipestream.office.v1.RenderStatus;
import ai.pipestream.office.v1.StreamOptions;
import ai.pipestream.office.v1.StreamPagesRequest;
import ai.pipestream.office.v1.StreamPagesResponse;
import ai.pipestream.office.v1.TextSpan;
import ai.pipestream.office.v1.ToDocumentResponse;
import ai.pipestream.office.v1.TrackedChangeDisplay;
import com.google.protobuf.ByteString;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import io.grpc.StatusRuntimeException;
import io.grpc.stub.StreamObserver;

import java.io.IOException;
import java.io.OutputStream;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.ArrayList;
import java.util.List;
import java.util.function.BiFunction;
import java.util.function.Consumer;

/**
 * Example CLI client for the grpc-libreoffice OfficeRenderService.
 *
 * <pre>
 *   gradle run --args="info"
 *   gradle run --args="pages ../../fixtures/sample3.docx out"
 *   gradle run --args="pages ../../fixtures/sample3.docx out --dpi 72"
 *   gradle run --args="pages ../../fixtures/sample3.docx out --format webp --quality 60"
 *   gradle run --args="pages ../../fixtures/sample3.docx out --first-page 2 --last-page 2"
 *   gradle run --args="pdf ../../fixtures/sample3.docx out.pdf"
 * </pre>
 *
 * The server address defaults to localhost:50053; override with the
 * GRLIBRE_ADDR environment variable.
 */
public final class OfficeClient {

    private static final int CHUNK_SIZE = 256 * 1024;
    // Page PNGs and embedded images can exceed gRPC's 4 MiB default.
    private static final int MAX_MESSAGE_BYTES = 128 * 1024 * 1024;

    private OfficeClient() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            usage();
        }
        String addr = System.getenv().getOrDefault("GRLIBRE_ADDR", "localhost:50053");
        ManagedChannel channel = ManagedChannelBuilder.forTarget(addr)
                .usePlaintext()
                .maxInboundMessageSize(MAX_MESSAGE_BYTES)
                .build();
        int exit = 0;
        try {
            switch (args[0]) {
                case "info" -> info(channel);
                case "pages" -> {
                    PagesFlags flags = parsePagesFlags(args);
                    pages(channel, Path.of(flags.file), Path.of(flags.outdir), flags);
                }
                case "pdf" -> {
                    if (args.length < 2) {
                        usage();
                    }
                    pdf(channel, Path.of(args[1]),
                            Path.of(args.length > 2 ? args[2] : "out.pdf"));
                }
                case "todoc" -> {
                    PagesFlags flags = parsePagesFlags(args);
                    toDocument(channel, Path.of(flags.file), flags);
                }
                default -> usage();
            }
        } catch (StatusRuntimeException e) {
            System.err.printf("gRPC error: %s: %s%n",
                    e.getStatus().getCode(), e.getStatus().getDescription());
            exit = 1;
        } finally {
            channel.shutdownNow();
            channel.awaitTermination(5, TimeUnit.SECONDS);
        }
        System.exit(exit);
    }

    private static void usage() {
        System.err.println(
                "usage: OfficeClient <info | pages <file> [outdir] [options] | "
                        + "pdf <file> [out.pdf] | todoc <file> [options]>");
        System.err.println(
                "pages options: --dpi <n> --first-page <n> --last-page <n> "
                        + "--format png|jpeg|webp|svg --quality <n> --parts PAGES,PARAGRAPHS,... "
                        + "--max-width <n> --grayscale --timeout <n> "
                        + "--tracked-changes as-is|final|original|markup "
                        + "--skip-hidden --used-range --notes --form NAME=VALUE "
                        + "--redact START:END --repair");
        System.exit(2);
    }

    /** Parsed flags for the pages subcommand. */
    private static final class PagesFlags {
        String file;
        String outdir = "pages-out";
        int dpi;
        int firstPage;
        int lastPage;
        PageImageFormat format = PageImageFormat.PAGE_IMAGE_FORMAT_UNSPECIFIED;
        int quality;
        final List<DocumentPart> parts = new ArrayList<>();
        int maxWidth;
        boolean grayscale;
        int timeout;
        TrackedChangeDisplay trackedChanges =
                TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_UNSPECIFIED;
        boolean skipHidden;
        boolean usedRange;
        boolean notes;
        final List<FormFillValue> forms = new ArrayList<>();
        final List<TextSpan> redacts = new ArrayList<>();
        boolean repair;
    }

    private static int requirePositive(String flag, String raw) {
        int n;
        try {
            n = Integer.parseInt(raw);
        } catch (NumberFormatException e) {
            usage();
            return 0;
        }
        if (n <= 0) {
            usage();
        }
        return n;
    }

    private static PageImageFormat parseFormat(String raw) {
        return switch (raw.toLowerCase(Locale.ROOT)) {
            case "png" -> PageImageFormat.PAGE_IMAGE_FORMAT_PNG;
            case "jpeg", "jpg" -> PageImageFormat.PAGE_IMAGE_FORMAT_JPEG;
            case "webp" -> PageImageFormat.PAGE_IMAGE_FORMAT_WEBP;
            case "svg" -> PageImageFormat.PAGE_IMAGE_FORMAT_SVG;
            default -> {
                System.err.println("--format must be png, jpeg, webp, or svg");
                System.exit(2);
                yield PageImageFormat.PAGE_IMAGE_FORMAT_UNSPECIFIED;
            }
        };
    }

    private static List<DocumentPart> parseParts(String raw) {
        List<DocumentPart> parts = new ArrayList<>();
        for (String token : raw.split(",")) {
            String t = token.trim().toUpperCase(Locale.ROOT);
            if (t.isEmpty()) {
                continue;
            }
            String name = t.startsWith("DOCUMENT_PART_") ? t : "DOCUMENT_PART_" + t;
            try {
                DocumentPart part = DocumentPart.valueOf(name);
                if (part == DocumentPart.UNRECOGNIZED
                        || part == DocumentPart.DOCUMENT_PART_UNSPECIFIED) {
                    throw new IllegalArgumentException(token);
                }
                parts.add(part);
            } catch (IllegalArgumentException e) {
                System.err.printf(
                        "unknown part %s: expected a DocumentPart name "
                                + "(PAGES or DOCUMENT_PART_PAGES)%n",
                        token);
                System.exit(2);
            }
        }
        return parts;
    }

    private static TrackedChangeDisplay parseTracked(String raw) {
        return switch (raw.toLowerCase(Locale.ROOT)) {
            case "as-is" -> TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_AS_IS;
            case "final" -> TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_FINAL;
            case "original" -> TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_ORIGINAL;
            case "markup", "show-markup" ->
                    TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_SHOW_MARKUP;
            default -> {
                System.err.println(
                        "--tracked-changes must be as-is, final, original, or markup");
                System.exit(2);
                yield TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_UNSPECIFIED;
            }
        };
    }

    private static FormFillValue parseForm(String raw) {
        int eq = raw.indexOf('=');
        if (eq <= 0) {
            System.err.println("--form needs NAME=VALUE");
            System.exit(2);
        }
        return FormFillValue.newBuilder()
                .setName(raw.substring(0, eq))
                .setValue(raw.substring(eq + 1))
                .build();
    }

    private static TextSpan parseRedact(String raw) {
        int sep = raw.indexOf(':');
        if (sep < 0) {
            System.err.println("--redact needs START:END");
            System.exit(2);
        }
        try {
            return TextSpan.newBuilder()
                    .setCharStart(Long.parseLong(raw.substring(0, sep)))
                    .setCharEnd(Long.parseLong(raw.substring(sep + 1)))
                    .build();
        } catch (NumberFormatException e) {
            System.err.println("--redact needs integer START:END");
            System.exit(2);
            return TextSpan.getDefaultInstance();
        }
    }

    private static PagesFlags parsePagesFlags(String[] args) {
        PagesFlags flags = new PagesFlags();
        List<String> positional = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            if (!a.startsWith("--")) {
                positional.add(a);
                continue;
            }
            switch (a) {
                case "--grayscale" -> flags.grayscale = true;
                case "--skip-hidden" -> flags.skipHidden = true;
                case "--used-range" -> flags.usedRange = true;
                case "--notes" -> flags.notes = true;
                case "--repair" -> flags.repair = true;
                default -> {
                    if (i + 1 >= args.length) {
                        usage();
                    }
                    String value = args[++i];
                    switch (a) {
                        case "--dpi" -> flags.dpi = requirePositive(a, value);
                        case "--first-page" -> flags.firstPage = requirePositive(a, value);
                        case "--last-page" -> flags.lastPage = requirePositive(a, value);
                        case "--format" -> flags.format = parseFormat(value);
                        case "--quality" -> flags.quality = requirePositive(a, value);
                        case "--parts" -> flags.parts.addAll(parseParts(value));
                        case "--max-width" -> flags.maxWidth = requirePositive(a, value);
                        case "--timeout" -> flags.timeout = requirePositive(a, value);
                        case "--tracked-changes" -> flags.trackedChanges = parseTracked(value);
                        case "--form" -> flags.forms.add(parseForm(value));
                        case "--redact" -> flags.redacts.add(parseRedact(value));
                        default -> usage();
                    }
                }
            }
        }
        if (positional.isEmpty()) {
            usage();
        }
        flags.file = positional.get(0);
        if (positional.size() > 1) {
            flags.outdir = positional.get(1);
        }
        return flags;
    }

    private static StreamOptions pagesOptions(PagesFlags flags) {
        if (flags.dpi <= 0 && flags.firstPage <= 0 && flags.lastPage <= 0
                && flags.format == PageImageFormat.PAGE_IMAGE_FORMAT_UNSPECIFIED
                && flags.quality <= 0 && flags.parts.isEmpty()
                && flags.maxWidth <= 0 && !flags.grayscale && flags.timeout <= 0
                && flags.trackedChanges
                    == TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_UNSPECIFIED
                && !flags.skipHidden && !flags.usedRange && !flags.notes
                && flags.forms.isEmpty() && flags.redacts.isEmpty()) {
            return null;
        }
        StreamOptions.Builder b = StreamOptions.newBuilder();
        if (flags.dpi > 0) {
            b.setRenderDpi(flags.dpi);
        }
        if (flags.firstPage > 0) {
            b.setFirstPage(flags.firstPage);
        }
        if (flags.lastPage > 0) {
            b.setLastPage(flags.lastPage);
        }
        if (flags.format != PageImageFormat.PAGE_IMAGE_FORMAT_UNSPECIFIED) {
            b.setPageFormat(flags.format);
            if (flags.format == PageImageFormat.PAGE_IMAGE_FORMAT_SVG) {
                b.setVectorFormat(PageVectorFormat.PAGE_VECTOR_FORMAT_SVG);
            }
        }
        if (flags.quality > 0) {
            b.setPageQuality(flags.quality);
        }
        if (!flags.parts.isEmpty()) {
            b.addAllParts(flags.parts);
        }
        if (flags.maxWidth > 0) {
            b.setMaxWidthPx(flags.maxWidth);
        }
        if (flags.grayscale) {
            b.setGrayscale(true);
        }
        if (flags.timeout > 0) {
            b.setTimeoutSeconds(flags.timeout);
        }
        if (flags.trackedChanges
                != TrackedChangeDisplay.TRACKED_CHANGE_DISPLAY_UNSPECIFIED) {
            b.setTrackedChanges(flags.trackedChanges);
        }
        if (flags.skipHidden) {
            b.setSkipHidden(true);
        }
        if (flags.usedRange) {
            b.setPaintUsedRange(true);
        }
        if (flags.notes) {
            b.setIncludeNotesPages(true);
        }
        if (!flags.forms.isEmpty()) {
            b.addAllFormValues(flags.forms);
        }
        if (!flags.redacts.isEmpty()) {
            b.addAllRedactSpans(flags.redacts);
        }
        return b.build();
    }

    private static String pageExt(PageImageFormat format) {
        return switch (format) {
            case PAGE_IMAGE_FORMAT_JPEG -> "jpg";
            case PAGE_IMAGE_FORMAT_WEBP -> "webp";
            case PAGE_IMAGE_FORMAT_SVG -> "svg";
            default -> "png";
        };
    }

    private static String pageLabel(PageImageFormat format) {
        return switch (format) {
            case PAGE_IMAGE_FORMAT_JPEG -> "jpeg";
            case PAGE_IMAGE_FORMAT_WEBP -> "webp";
            case PAGE_IMAGE_FORMAT_SVG -> "svg";
            default -> "png";
        };
    }

    private static void info(ManagedChannel channel) {
        GetServiceInfoResponse resp = OfficeRenderServiceGrpc.newBlockingStub(channel)
                .getServiceInfo(GetServiceInfoRequest.getDefaultInstance());
        System.out.printf("service version     : %s%n", resp.getServiceVersion());
        System.out.printf("libreoffice version : %s%n", resp.getLibreofficeVersion());
        System.out.printf("api version         : %s%n", resp.getApiVersion());
        System.out.printf("max document bytes  : %,d%n", resp.getMaxDocumentBytes());
        System.out.printf("max concurrent docs : %d%n", resp.getMaxConcurrentDocuments());
        System.out.printf("render dpi          : %d%n", resp.getRenderDpi());
        System.out.printf("typed content       : %b%n", resp.getTypedContent());
        System.out.printf("document mapping    : %b%n", resp.getDocumentMapping());
        System.out.printf("package repair      : %b%n", resp.getPackageRepair());
        System.out.printf("diskless documents  : %b%n", resp.getDisklessDocuments());
        System.out.printf("supported formats   : %s%n",
                String.join(", ", resp.getSupportedFormatsList()));
    }

    private static void pages(ManagedChannel channel, Path file, Path outdir, PagesFlags flags)
            throws IOException, InterruptedException {
        Files.createDirectories(outdir);
        long t0 = System.nanoTime();
        AtomicReference<Long> firstPageNanos = new AtomicReference<>();
        Map<String, Integer> counts = new TreeMap<>();
        int[] pagesSaved = {0};

        CallState state = new CallState();
        StreamObserver<StreamPagesRequest> upload =
                OfficeRenderServiceGrpc.newStub(channel).streamPages(
                        state.observer((StreamPagesResponse resp) -> {
                            switch (resp.getEventCase()) {
                                case DOCUMENT_INFO -> printDocumentInfo(resp.getDocumentInfo());
                                case PAGE_IMAGE -> {
                                    firstPageNanos.compareAndSet(null, System.nanoTime() - t0);
                                    PageImage img = resp.getPageImage();
                                    Path out = outdir.resolve(String.format("page-%04d.%s",
                                            img.getIndex() + 1, pageExt(img.getFormat())));
                                    try {
                                        Files.write(out, img.getPng().toByteArray());
                                    } catch (IOException e) {
                                        throw new UncheckedIOException(e);
                                    }
                                    pagesSaved[0]++;
                                    System.out.printf("  %s  %dx%dpx @ %d dpi  %s  %,d bytes%n",
                                            out.getFileName(), img.getWidthPx(),
                                            img.getHeightPx(), img.getDpi(),
                                            pageLabel(img.getFormat()), img.getPng().size());
                                }
                                case STATUS -> {
                                    long totalMs = (System.nanoTime() - t0) / 1_000_000;
                                    System.out.println();
                                    if (!counts.isEmpty()) {
                                        int width = counts.keySet().stream()
                                                .mapToInt(String::length).max().orElse(0);
                                        System.out.println("typed content events:");
                                        counts.forEach((kind, n) -> System.out.printf(
                                                "  %-" + width + "s  %d%n", kind, n));
                                        System.out.println();
                                    }
                                    printRenderStatus(resp.getStatus(), totalMs);
                                    Long fp = firstPageNanos.get();
                                    if (fp != null) {
                                        System.out.printf("first page  : %d ms%n", fp / 1_000_000);
                                    }
                                    System.out.printf("pages saved : %d -> %s/%n",
                                            pagesSaved[0], outdir);
                                }
                                default -> counts.merge(
                                        resp.getEventCase().name().toLowerCase(Locale.ROOT), 1,
                                        Integer::sum);
                            }
                        }));

        StreamOptions options = pagesOptions(flags);
        streamDocument(file, upload, (chunk, first) -> {
            StreamPagesRequest.Builder req = StreamPagesRequest.newBuilder().setChunk(chunk);
            // StreamOptions resolve to the first nonzero/non-empty value in
            // the upload stream, so they must ride the first chunk.
            if (first && options != null) {
                req.setOptions(options);
            }
            if (first && flags.repair) {
                req.setAllowPackageRepair(true);
            }
            return req.build();
        });
        state.awaitDone();
    }

    private static void toDocument(ManagedChannel channel, Path file, PagesFlags flags)
            throws IOException, InterruptedException {
        long t0 = System.nanoTime();
        StreamOptions options = pagesOptions(flags);
        CallState state = new CallState();
        StreamObserver<StreamPagesRequest> upload =
                OfficeRenderServiceGrpc.newStub(channel).toDocument(
                        state.observer((ToDocumentResponse resp) -> {
                            printDocumentInfo(resp.getDocumentInfo());
                            var doc = resp.getDocument();
                            System.out.printf("texts       : %d%n", doc.getTextsCount());
                            System.out.printf("pictures    : %d%n", doc.getPicturesCount());
                            System.out.printf("tables      : %d%n", doc.getTablesCount());
                            System.out.printf("pages       : %d%n", doc.getPagesCount());
                            if (resp.hasStatus()) {
                                System.out.println();
                                printRenderStatus(resp.getStatus(),
                                        (System.nanoTime() - t0) / 1_000_000);
                            }
                        }));
        streamDocument(file, upload, (chunk, first) -> {
            StreamPagesRequest.Builder req = StreamPagesRequest.newBuilder().setChunk(chunk);
            if (first && options != null) {
                req.setOptions(options);
            }
            if (first && flags.repair) {
                req.setAllowPackageRepair(true);
            }
            return req.build();
        });
        state.awaitDone();
    }

    private static void pdf(ManagedChannel channel, Path file, Path out)
            throws IOException, InterruptedException {
        long t0 = System.nanoTime();
        long[] total = {0};
        try (OutputStream os = Files.newOutputStream(out)) {
            CallState state = new CallState();
            StreamObserver<ConvertToPdfRequest> upload =
                    OfficeRenderServiceGrpc.newStub(channel).convertToPdf(
                            state.observer((ConvertToPdfResponse resp) -> {
                                switch (resp.getEventCase()) {
                                    case DOCUMENT_INFO -> printDocumentInfo(resp.getDocumentInfo());
                                    case PDF_CHUNK -> {
                                        ByteString data = resp.getPdfChunk().getData();
                                        try {
                                            data.writeTo(os);
                                        } catch (IOException e) {
                                            throw new UncheckedIOException(e);
                                        }
                                        total[0] += data.size();
                                    }
                                    case STATUS -> printRenderStatus(resp.getStatus(),
                                            (System.nanoTime() - t0) / 1_000_000);
                                    default -> {
                                    }
                                }
                            }));

            streamDocument(file, upload,
                    (chunk, first) -> ConvertToPdfRequest.newBuilder().setChunk(chunk).build());
            state.awaitDone();
        }
        System.out.printf("wrote       : %,d bytes -> %s%n", total[0], out);
    }

    /** Completion latch plus error slot for one streaming call. */
    private static final class CallState {
        private final CountDownLatch done = new CountDownLatch(1);
        private final AtomicReference<Throwable> error = new AtomicReference<>();

        <T> StreamObserver<T> observer(Consumer<T> onData) {
            return new StreamObserver<>() {
                @Override
                public void onNext(T value) {
                    onData.accept(value);
                }

                @Override
                public void onError(Throwable t) {
                    error.set(t);
                    done.countDown();
                }

                @Override
                public void onCompleted() {
                    done.countDown();
                }
            };
        }

        void awaitDone() throws InterruptedException {
            done.await();
            Throwable t = error.get();
            if (t instanceof StatusRuntimeException sre) {
                throw sre;
            }
            if (t != null) {
                throw new RuntimeException(t);
            }
        }
    }

    /**
     * Uploads {@code file} as 256 KiB DocumentChunks; the last chunk sets complete.
     * {@code wrap} receives each chunk plus whether it is the first of the stream,
     * so per-request options can ride the front of the upload.
     */
    private static <ReqT> void streamDocument(Path file, StreamObserver<ReqT> upload,
            BiFunction<DocumentChunk, Boolean, ReqT> wrap) throws IOException {
        byte[] data = Files.readAllBytes(file);
        int offset = 0;
        boolean first = true;
        do {
            int end = Math.min(offset + CHUNK_SIZE, data.length);
            DocumentChunk.Builder chunk = DocumentChunk.newBuilder()
                    .setData(ByteString.copyFrom(data, offset, end - offset))
                    .setComplete(end == data.length);
            if (first) {
                chunk.setDocumentId(UUID.randomUUID().toString())
                        .setFilename(file.getFileName().toString());
            }
            upload.onNext(wrap.apply(chunk.build(), first));
            first = false;
            offset = end;
        } while (offset < data.length);
        upload.onCompleted();
    }

    private static void printDocumentInfo(DocumentInfo info) {
        System.out.printf("document_id : %s%n", info.getDocumentId());
        System.out.printf("format      : %s%n", info.getSourceFormat());
        System.out.printf("type        : %s%n", info.getDocumentType());
        System.out.printf("pages       : %d%n", info.getPageCount());
    }

    private static void printRenderStatus(RenderStatus status, long wallMs) {
        System.out.printf("status      : %s%n", status.getState());
        System.out.printf("input bytes : %,d%n", status.getInputBytes());
        System.out.printf("output bytes: %,d%n", status.getOutputBytes());
        System.out.printf("render time : %d ms (server) / %d ms (wall)%n",
                status.getRenderMillis(), wallMs);
        for (String w : status.getWarningsList()) {
            System.out.printf("warning     : %s%n", w);
        }
    }
}
