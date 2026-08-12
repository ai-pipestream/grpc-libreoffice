package ai.pipestream.office.examples;

import ai.pipestream.office.v1.ConvertToPdfRequest;
import ai.pipestream.office.v1.ConvertToPdfResponse;
import ai.pipestream.office.v1.DocumentChunk;
import ai.pipestream.office.v1.DocumentInfo;
import ai.pipestream.office.v1.GetServiceInfoRequest;
import ai.pipestream.office.v1.GetServiceInfoResponse;
import ai.pipestream.office.v1.OfficeRenderServiceGrpc;
import ai.pipestream.office.v1.PageImage;
import ai.pipestream.office.v1.RenderStatus;
import ai.pipestream.office.v1.StreamOptions;
import ai.pipestream.office.v1.StreamPagesRequest;
import ai.pipestream.office.v1.StreamPagesResponse;
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
                    int dpi = 0;
                    List<String> positional = new ArrayList<>();
                    for (int i = 1; i < args.length; i++) {
                        if ("--dpi".equals(args[i])) {
                            if (i + 1 >= args.length) {
                                usage();
                            }
                            dpi = Integer.parseInt(args[++i]);
                            if (dpi <= 0) {
                                usage();
                            }
                        } else {
                            positional.add(args[i]);
                        }
                    }
                    if (positional.isEmpty()) {
                        usage();
                    }
                    pages(channel, Path.of(positional.get(0)),
                            Path.of(positional.size() > 1 ? positional.get(1) : "pages-out"),
                            dpi);
                }
                case "pdf" -> {
                    if (args.length < 2) {
                        usage();
                    }
                    pdf(channel, Path.of(args[1]),
                            Path.of(args.length > 2 ? args[2] : "out.pdf"));
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
                "usage: OfficeClient <info | pages <file> [outdir] [--dpi <n>] | pdf <file> [out.pdf]>");
        System.exit(2);
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
        System.out.printf("diskless documents  : %b%n", resp.getDisklessDocuments());
        System.out.printf("supported formats   : %s%n",
                String.join(", ", resp.getSupportedFormatsList()));
    }

    private static void pages(ManagedChannel channel, Path file, Path outdir, int dpi)
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
                                    Path out = outdir.resolve(
                                            String.format("page-%04d.png", img.getIndex() + 1));
                                    try {
                                        Files.write(out, img.getPng().toByteArray());
                                    } catch (IOException e) {
                                        throw new UncheckedIOException(e);
                                    }
                                    pagesSaved[0]++;
                                    System.out.printf("  %s  %dx%dpx @ %d dpi  %,d bytes%n",
                                            out.getFileName(), img.getWidthPx(),
                                            img.getHeightPx(), img.getDpi(), img.getPng().size());
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

        streamDocument(file, upload, (chunk, first) -> {
            StreamPagesRequest.Builder req = StreamPagesRequest.newBuilder().setChunk(chunk);
            // render_dpi resolves to the first nonzero value in the upload
            // stream, so it must ride the first chunk.
            if (first && dpi > 0) {
                req.setOptions(StreamOptions.newBuilder().setRenderDpi(dpi));
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
