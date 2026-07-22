package ai.pipestream.grlibre;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import ai.pipestream.grlibre.convert.ConversionEngine;
import ai.pipestream.grlibre.convert.ConversionFailedException;
import ai.pipestream.grlibre.convert.UnknownFormatException;
import ai.pipestream.grlibre.server.OfficeConvertServiceImpl;
import ai.pipestream.office.v1.ConvertEvent;
import ai.pipestream.office.v1.ConvertRequestChunk;
import ai.pipestream.office.v1.ConvertStatus;
import ai.pipestream.office.v1.GetServiceInfoRequest;
import ai.pipestream.office.v1.GetServiceInfoResponse;
import ai.pipestream.office.v1.OfficeConvertServiceGrpc;
import com.google.protobuf.ByteString;
import io.grpc.ManagedChannel;
import io.grpc.Server;
import io.grpc.Status;
import io.grpc.StatusRuntimeException;
import io.grpc.inprocess.InProcessChannelBuilder;
import io.grpc.inprocess.InProcessServerBuilder;
import io.grpc.stub.StreamObserver;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/** Protocol behavior over a fake engine; no LibreOffice involved. */
class ConvertServiceTest {

  private static final long CAP_BYTES = 4L * 1024 * 1024;
  private static final byte[] PDF_PREFIX = "%PDF-fake\n".getBytes(StandardCharsets.UTF_8);

  private static Server server;
  private static ManagedChannel channel;
  private static ExecutorService executor;

  /** Resolves docx and txt; "boom.docx" fails, "slow.docx" times out. */
  static final class FakeEngine implements ConversionEngine {
    @Override
    public String resolveFormat(String filename, String contentType) throws UnknownFormatException {
      if (filename != null && (filename.endsWith(".docx") || filename.endsWith(".txt"))) {
        return filename.substring(filename.lastIndexOf('.') + 1);
      }
      if ("text/plain".equals(contentType)) return "txt";
      throw new UnknownFormatException("unknown format for " + filename);
    }

    @Override
    public byte[] convertToPdf(byte[] document, String sourceFormat)
        throws ConversionFailedException {
      String text = new String(document, StandardCharsets.UTF_8);
      if (text.startsWith("boom")) {
        throw new ConversionFailedException("engine exploded", null, false);
      }
      if (text.startsWith("slow")) {
        throw new ConversionFailedException("task timeout", null, true);
      }
      ByteArrayOutputStream pdf = new ByteArrayOutputStream();
      pdf.writeBytes(PDF_PREFIX);
      pdf.writeBytes(document);
      return pdf.toByteArray();
    }

    @Override
    public List<String> supportedFormats() {
      return List.of("docx", "txt");
    }
  }

  record Result(String sourceFormat, byte[] pdf, ConvertStatus status, Throwable error) {}

  @BeforeAll
  static void startServer() throws Exception {
    executor = Executors.newVirtualThreadPerTaskExecutor();
    String name = InProcessServerBuilder.generateName();
    server = InProcessServerBuilder.forName(name)
        .directExecutor()
        .addService(new OfficeConvertServiceImpl(
            new FakeEngine(), CAP_BYTES, 4, 2, 50, "test", executor))
        .build()
        .start();
    channel = InProcessChannelBuilder.forName(name).directExecutor().build();
  }

  @AfterAll
  static void stopServer() throws Exception {
    channel.shutdownNow();
    server.shutdownNow();
    executor.shutdown();
    assertTrue(channel.awaitTermination(5, TimeUnit.SECONDS));
    assertTrue(server.awaitTermination(5, TimeUnit.SECONDS));
  }

  private static Result convert(byte[] bytes, String filename, int chunkSize,
                                boolean markComplete) throws Exception {
    OfficeConvertServiceGrpc.OfficeConvertServiceStub stub =
        OfficeConvertServiceGrpc.newStub(channel);
    CountDownLatch done = new CountDownLatch(1);
    List<ConvertEvent> events = new ArrayList<>();
    AtomicReference<Throwable> failure = new AtomicReference<>();
    StreamObserver<ConvertRequestChunk> requests = stub.convertToPdf(new StreamObserver<>() {
      @Override
      public void onNext(ConvertEvent event) {
        synchronized (events) {
          events.add(event);
        }
      }

      @Override
      public void onError(Throwable error) {
        failure.set(error);
        done.countDown();
      }

      @Override
      public void onCompleted() {
        done.countDown();
      }
    });
    for (int offset = 0; offset < bytes.length; offset += chunkSize) {
      int length = Math.min(chunkSize, bytes.length - offset);
      boolean last = offset + length >= bytes.length;
      requests.onNext(ConvertRequestChunk.newBuilder()
          .setDocumentId("doc-1")
          .setFilename(filename)
          .setData(ByteString.copyFrom(bytes, offset, length))
          .setComplete(last && markComplete)
          .build());
    }
    if (bytes.length == 0) {
      requests.onNext(ConvertRequestChunk.newBuilder().setComplete(markComplete).build());
    }
    requests.onCompleted();
    assertTrue(done.await(30, TimeUnit.SECONDS), "conversion did not finish");
    if (failure.get() != null) {
      return new Result(null, null, null, failure.get());
    }
    String sourceFormat = null;
    ByteArrayOutputStream pdf = new ByteArrayOutputStream();
    ConvertStatus status = null;
    for (ConvertEvent event : events) {
      switch (event.getEventCase()) {
        case DOCUMENT_INFO -> sourceFormat = event.getDocumentInfo().getSourceFormat();
        case PDF_CHUNK -> pdf.writeBytes(event.getPdfChunk().getData().toByteArray());
        case STATUS -> status = event.getStatus();
        default -> throw new AssertionError("unexpected event " + event.getEventCase());
      }
    }
    return new Result(sourceFormat, pdf.toByteArray(), status, null);
  }

  private static Status.Code codeOf(Throwable error) {
    return ((StatusRuntimeException) error).getStatus().getCode();
  }

  @Test
  void roundTripProducesInfoPdfAndStatus() throws Exception {
    byte[] document = "hello office".getBytes(StandardCharsets.UTF_8);
    Result result = convert(document, "letter.docx", 1024, true);
    assertNull(result.error());
    assertEquals("docx", result.sourceFormat());
    ByteArrayOutputStream expected = new ByteArrayOutputStream();
    expected.writeBytes(PDF_PREFIX);
    expected.writeBytes(document);
    assertArrayEquals(expected.toByteArray(), result.pdf());
    assertNotNull(result.status());
    assertEquals(ConvertStatus.State.STATE_OK, result.status().getState());
    assertEquals(document.length, result.status().getInputBytes());
    assertEquals(result.pdf().length, result.status().getOutputBytes());
  }

  @Test
  void chunkedUploadMatchesSingleChunk() throws Exception {
    byte[] document = new byte[300_000];
    for (int i = 0; i < document.length; i++) document[i] = (byte) (i % 251);
    Result single = convert(document, "big.docx", document.length, true);
    Result chunked = convert(document, "big.docx", 7_001, true);
    assertNull(single.error());
    assertNull(chunked.error());
    assertArrayEquals(single.pdf(), chunked.pdf());
  }

  @Test
  void largePdfIsChunkedInOrder() throws Exception {
    byte[] document = new byte[600_000];
    Result result = convert(document, "big.docx", 100_000, true);
    assertNull(result.error());
    assertEquals(PDF_PREFIX.length + document.length, result.pdf().length);
  }

  @Test
  void unknownFormatIsInvalidArgument() throws Exception {
    Result result = convert("x".getBytes(StandardCharsets.UTF_8), "mystery.zzz", 16, true);
    assertNotNull(result.error());
    assertEquals(Status.Code.INVALID_ARGUMENT, codeOf(result.error()));
  }

  @Test
  void engineFailureIsInternal() throws Exception {
    Result result = convert("boom now".getBytes(StandardCharsets.UTF_8), "b.docx", 16, true);
    assertNotNull(result.error());
    assertEquals(Status.Code.INTERNAL, codeOf(result.error()));
  }

  @Test
  void engineTimeoutIsDeadlineExceeded() throws Exception {
    Result result = convert("slow doc".getBytes(StandardCharsets.UTF_8), "s.docx", 16, true);
    assertNotNull(result.error());
    assertEquals(Status.Code.DEADLINE_EXCEEDED, codeOf(result.error()));
  }

  @Test
  void oversizeDocumentIsResourceExhausted() throws Exception {
    byte[] document = new byte[(int) CAP_BYTES + 1];
    Result result = convert(document, "huge.docx", 1 << 20, true);
    assertNotNull(result.error());
    assertEquals(Status.Code.RESOURCE_EXHAUSTED, codeOf(result.error()));
  }

  @Test
  void missingCompleteFlagIsInvalid() throws Exception {
    Result result = convert("abc".getBytes(StandardCharsets.UTF_8), "a.docx", 16, false);
    assertNotNull(result.error());
    assertEquals(Status.Code.INVALID_ARGUMENT, codeOf(result.error()));
  }

  @Test
  void emptyStreamIsInvalid() throws Exception {
    Result result = convert(new byte[0], "a.docx", 16, false);
    assertNotNull(result.error());
    assertEquals(Status.Code.INVALID_ARGUMENT, codeOf(result.error()));
  }

  @Test
  void serviceInfoReportsCapabilities() {
    GetServiceInfoResponse info = OfficeConvertServiceGrpc.newBlockingStub(channel)
        .getServiceInfo(GetServiceInfoRequest.getDefaultInstance());
    assertEquals(OfficeConvertServiceImpl.SERVICE_VERSION, info.getServiceVersion());
    assertEquals("test", info.getLibreofficeVersion());
    assertEquals(CAP_BYTES, info.getMaxDocumentBytes());
    assertEquals(2, info.getPoolSize());
    assertEquals(50, info.getMaxTasksPerProcess());
    assertEquals(List.of("docx", "txt"), info.getSupportedFormatsList());
  }

  @Test
  void concurrentConversionsComplete() throws Exception {
    int lanes = 8;
    CountDownLatch finished = new CountDownLatch(lanes);
    AtomicReference<Throwable> anyFailure = new AtomicReference<>();
    for (int lane = 0; lane < lanes; lane++) {
      final int id = lane;
      Thread.ofVirtual().start(() -> {
        try {
          byte[] document = ("document " + id).getBytes(StandardCharsets.UTF_8);
          Result result = convert(document, "d" + id + ".docx", 8, true);
          if (result.error() != null) anyFailure.set(result.error());
        } catch (Throwable unexpected) {
          anyFailure.set(unexpected);
        } finally {
          finished.countDown();
        }
      });
    }
    assertTrue(finished.await(60, TimeUnit.SECONDS));
    assertNull(anyFailure.get());
  }
}
