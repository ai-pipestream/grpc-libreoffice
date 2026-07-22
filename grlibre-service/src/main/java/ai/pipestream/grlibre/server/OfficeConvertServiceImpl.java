package ai.pipestream.grlibre.server;

import ai.pipestream.grlibre.convert.ConversionEngine;
import ai.pipestream.grlibre.convert.ConversionFailedException;
import ai.pipestream.grlibre.convert.UnknownFormatException;
import ai.pipestream.office.v1.ConvertEvent;
import ai.pipestream.office.v1.ConvertRequestChunk;
import ai.pipestream.office.v1.ConvertStatus;
import ai.pipestream.office.v1.DocumentInfo;
import ai.pipestream.office.v1.GetServiceInfoRequest;
import ai.pipestream.office.v1.GetServiceInfoResponse;
import ai.pipestream.office.v1.OfficeConvertServiceGrpc;
import ai.pipestream.office.v1.PdfChunk;
import com.google.protobuf.ByteString;
import io.grpc.Status;
import io.grpc.stub.StreamObserver;
import java.io.ByteArrayOutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicLong;

/**
 * The gRPC face over a ConversionEngine. Chunks accumulate in memory under a
 * hard byte cap; a completed upload converts on a virtual thread, with a
 * semaphore bounding concurrent conversions to protect heap while documents
 * queue for the office process pool.
 */
public final class OfficeConvertServiceImpl
    extends OfficeConvertServiceGrpc.OfficeConvertServiceImplBase {

  public static final String SERVICE_VERSION = "0.1.0";
  public static final String API_VERSION = "v1";

  private static final int PDF_CHUNK_BYTES = 256 * 1024;

  private final ConversionEngine engine;
  private final long maxDocumentBytes;
  private final int poolSize;
  private final int maxTasksPerProcess;
  private final String libreOfficeVersion;
  private final Semaphore convertSlots;
  private final ExecutorService executor;

  final AtomicLong converted = new AtomicLong();
  final AtomicLong rejected = new AtomicLong();
  final AtomicLong failed = new AtomicLong();

  public OfficeConvertServiceImpl(ConversionEngine engine, long maxDocumentBytes,
                                  int maxConcurrentConversions, int poolSize,
                                  int maxTasksPerProcess, String libreOfficeVersion,
                                  ExecutorService executor) {
    this.engine = engine;
    this.maxDocumentBytes = maxDocumentBytes;
    this.poolSize = poolSize;
    this.maxTasksPerProcess = maxTasksPerProcess;
    this.libreOfficeVersion = libreOfficeVersion;
    this.convertSlots = new Semaphore(maxConcurrentConversions);
    this.executor = executor;
  }

  @Override
  public StreamObserver<ConvertRequestChunk> convertToPdf(StreamObserver<ConvertEvent> responses) {
    return new StreamObserver<>() {
      private final ByteArrayOutputStream buffer = new ByteArrayOutputStream();
      private String documentId = "";
      private String filename = "";
      private String contentType = "";
      private boolean sawComplete;
      private boolean aborted;

      @Override
      public void onNext(ConvertRequestChunk chunk) {
        if (aborted) return;
        if (documentId.isEmpty() && !chunk.getDocumentId().isEmpty()) {
          documentId = chunk.getDocumentId();
        }
        if (filename.isEmpty() && !chunk.getFilename().isEmpty()) {
          filename = chunk.getFilename();
        }
        if (contentType.isEmpty() && !chunk.getContentType().isEmpty()) {
          contentType = chunk.getContentType();
        }
        if (buffer.size() + (long) chunk.getData().size() > maxDocumentBytes) {
          aborted = true;
          rejected.incrementAndGet();
          responses.onError(
              Status.RESOURCE_EXHAUSTED
                  .withDescription("document exceeds " + maxDocumentBytes + " bytes")
                  .asRuntimeException());
          return;
        }
        try {
          chunk.getData().writeTo(buffer);
        } catch (java.io.IOException impossible) {
          throw new IllegalStateException("in-memory buffer write failed", impossible);
        }
        if (chunk.getComplete()) sawComplete = true;
      }

      @Override
      public void onError(Throwable error) {
        aborted = true;
      }

      @Override
      public void onCompleted() {
        if (aborted) return;
        if (!sawComplete || buffer.size() == 0) {
          rejected.incrementAndGet();
          responses.onError(
              Status.INVALID_ARGUMENT
                  .withDescription(buffer.size() == 0
                      ? "no document bytes received"
                      : "stream ended without a chunk marked complete")
                  .asRuntimeException());
          return;
        }
        byte[] bytes = buffer.toByteArray();
        String id = documentId;
        String name = filename;
        String type = contentType;
        executor.execute(() -> convertNow(id, name, type, bytes, responses));
      }
    };
  }

  private void convertNow(String documentId, String filename, String contentType,
                          byte[] bytes, StreamObserver<ConvertEvent> responses) {
    String sourceFormat;
    try {
      sourceFormat = engine.resolveFormat(filename, contentType);
    } catch (UnknownFormatException unknown) {
      rejected.incrementAndGet();
      responses.onError(Status.INVALID_ARGUMENT.withDescription(unknown.getMessage())
          .asRuntimeException());
      return;
    }
    try {
      convertSlots.acquire();
    } catch (InterruptedException interrupt) {
      Thread.currentThread().interrupt();
      responses.onError(Status.UNAVAILABLE.withDescription("server shutting down")
          .asRuntimeException());
      return;
    }
    try {
      long started = System.nanoTime();
      byte[] pdf = engine.convertToPdf(bytes, sourceFormat);
      long elapsedMillis = (System.nanoTime() - started) / 1_000_000L;
      responses.onNext(ConvertEvent.newBuilder()
          .setDocumentInfo(DocumentInfo.newBuilder()
              .setDocumentId(documentId)
              .setSourceFormat(sourceFormat))
          .build());
      for (int offset = 0; offset < pdf.length; offset += PDF_CHUNK_BYTES) {
        int length = Math.min(PDF_CHUNK_BYTES, pdf.length - offset);
        responses.onNext(ConvertEvent.newBuilder()
            .setPdfChunk(PdfChunk.newBuilder()
                .setData(ByteString.copyFrom(pdf, offset, length)))
            .build());
      }
      responses.onNext(ConvertEvent.newBuilder()
          .setStatus(ConvertStatus.newBuilder()
              .setState(ConvertStatus.State.STATE_OK)
              .setInputBytes(bytes.length)
              .setOutputBytes(pdf.length)
              .setConversionMillis(elapsedMillis))
          .build());
      responses.onCompleted();
      converted.incrementAndGet();
    } catch (ConversionFailedException failure) {
      failed.incrementAndGet();
      Status status = failure.timedOut()
          ? Status.DEADLINE_EXCEEDED.withDescription("conversion timed out")
          : Status.INTERNAL.withDescription(failure.getMessage());
      responses.onError(status.asRuntimeException());
    } catch (Exception unexpected) {
      failed.incrementAndGet();
      responses.onError(Status.INTERNAL
          .withDescription("conversion fault: " + unexpected.getMessage()).asRuntimeException());
    } finally {
      convertSlots.release();
    }
  }

  @Override
  public void getServiceInfo(GetServiceInfoRequest request,
                             StreamObserver<GetServiceInfoResponse> responses) {
    responses.onNext(
        GetServiceInfoResponse.newBuilder()
            .setServiceVersion(SERVICE_VERSION)
            .setLibreofficeVersion(libreOfficeVersion)
            .setApiVersion(API_VERSION)
            .addAllSupportedFormats(engine.supportedFormats())
            .setMaxDocumentBytes(maxDocumentBytes)
            .setPoolSize(poolSize)
            .setMaxTasksPerProcess(maxTasksPerProcess)
            .build());
    responses.onCompleted();
  }
}
