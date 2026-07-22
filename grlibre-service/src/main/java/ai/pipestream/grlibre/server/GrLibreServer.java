package ai.pipestream.grlibre.server;

import ai.pipestream.grlibre.convert.LibreOfficeEngine;
import io.grpc.Grpc;
import io.grpc.InsecureServerCredentials;
import io.grpc.Server;
import io.grpc.protobuf.services.HealthStatusManager;
import io.grpc.protobuf.services.ProtoReflectionServiceV1;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.jodconverter.local.office.LocalOfficeManager;

/**
 * Diskless office-to-PDF bridge. A pool of headless LibreOffice processes
 * lives behind JODConverter; documents stream in over gRPC, PDFs stream out,
 * and the only writable paths (office working directory, user profile) sit on
 * memory-backed tmp storage. Runs under a read-only root filesystem.
 */
public final class GrLibreServer {

  public static void main(String[] args) throws Exception {
    int port = intFromEnv("GRLIBRE_PORT", 50053, 1, 65535);
    long maxDocumentBytes =
        intFromEnv("GRLIBRE_MAX_DOCUMENT_MIB", 100, 1, 2048) * 1024L * 1024L;
    int poolSize = intFromEnv("GRLIBRE_POOL_SIZE", 2, 1, 16);
    int maxTasksPerProcess = intFromEnv("GRLIBRE_MAX_TASKS_PER_PROCESS", 50, 1, 10000);
    int taskTimeoutSeconds = intFromEnv("GRLIBRE_TASK_TIMEOUT_SECONDS", 120, 5, 3600);
    int maxConcurrent = intFromEnv("GRLIBRE_MAX_CONCURRENT_CONVERSIONS", poolSize, 1, 64);
    int metricsIntervalSeconds = intFromEnv("GRLIBRE_METRICS_INTERVAL_SECONDS", 60, 0, 86400);

    int[] ports = new int[poolSize];
    for (int i = 0; i < poolSize; i++) ports[i] = 2002 + i;

    File workingDir = new File(System.getProperty("java.io.tmpdir"), "grlibre-office");
    if (!workingDir.isDirectory() && !workingDir.mkdirs()) {
      throw new IllegalStateException("cannot create office working dir " + workingDir);
    }

    LocalOfficeManager manager = LocalOfficeManager.builder()
        .portNumbers(ports)
        .maxTasksPerProcess(maxTasksPerProcess)
        .taskExecutionTimeout(taskTimeoutSeconds * 1000L)
        .workingDir(workingDir)
        .build();
    manager.start();

    ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor();
    OfficeConvertServiceImpl service = new OfficeConvertServiceImpl(
        new LibreOfficeEngine(manager), maxDocumentBytes, maxConcurrent,
        poolSize, maxTasksPerProcess, detectLibreOfficeVersion(), executor);

    HealthStatusManager health = new HealthStatusManager();
    Server server = Grpc.newServerBuilderForPort(port, InsecureServerCredentials.create())
        .addService(service)
        .addService(health.getHealthService())
        .addService(ProtoReflectionServiceV1.newInstance())
        .maxInboundMessageSize((int) Math.min(Integer.MAX_VALUE, maxDocumentBytes + (1 << 20)))
        .executor(executor)
        .build()
        .start();
    health.setStatus("", io.grpc.health.v1.HealthCheckResponse.ServingStatus.SERVING);
    System.out.println("grpc-libreoffice listening on " + port
        + " pool=" + poolSize + " maxTasksPerProcess=" + maxTasksPerProcess
        + " cap=" + maxDocumentBytes + "B");

    if (metricsIntervalSeconds > 0) {
      Thread metrics = new Thread(() -> {
        while (true) {
          try {
            Thread.sleep(metricsIntervalSeconds * 1000L);
          } catch (InterruptedException interrupt) {
            return;
          }
          System.out.println("grlibre metrics: docs{converted=" + service.converted.get()
              + ",rejected=" + service.rejected.get()
              + ",failed=" + service.failed.get() + "}");
        }
      }, "grlibre-metrics");
      metrics.setDaemon(true);
      metrics.start();
    }

    Runtime.getRuntime().addShutdownHook(new Thread(() -> {
      health.setStatus("", io.grpc.health.v1.HealthCheckResponse.ServingStatus.NOT_SERVING);
      server.shutdown();
      try {
        if (!server.awaitTermination(30, TimeUnit.SECONDS)) {
          server.shutdownNow();
        }
      } catch (InterruptedException interrupt) {
        server.shutdownNow();
        Thread.currentThread().interrupt();
      }
      try {
        manager.stop();
      } catch (Exception stopFailure) {
        System.err.println("office manager stop failed: " + stopFailure.getMessage());
      }
    }, "grlibre-shutdown"));

    server.awaitTermination();
  }

  private static String detectLibreOfficeVersion() {
    try {
      Process process = new ProcessBuilder("soffice", "--version")
          .redirectErrorStream(true).start();
      try (BufferedReader reader = new BufferedReader(
          new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
        String line = reader.readLine();
        process.waitFor(10, TimeUnit.SECONDS);
        return line == null ? "unknown" : line.strip();
      }
    } catch (Exception probeFailure) {
      return "unknown";
    }
  }

  private static int intFromEnv(String name, int fallback, int min, int max) {
    String raw = System.getenv(name);
    if (raw == null || raw.isBlank()) return fallback;
    int value;
    try {
      value = Integer.parseInt(raw.strip());
    } catch (NumberFormatException bad) {
      throw new IllegalArgumentException(name + " must be an integer, got \"" + raw + "\"");
    }
    if (value < min || value > max) {
      throw new IllegalArgumentException(
          name + " must be between " + min + " and " + max + ", got " + value);
    }
    return value;
  }
}
