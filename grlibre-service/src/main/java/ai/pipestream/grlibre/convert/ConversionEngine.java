package ai.pipestream.grlibre.convert;

/**
 * Converts one office document to PDF, fully in memory-backed storage. The
 * gRPC layer depends on this interface so protocol behavior is testable
 * without a LibreOffice installation.
 */
public interface ConversionEngine {

  /**
   * Resolves the canonical source format extension for a document, from the
   * filename extension first and the content type second.
   *
   * @throws UnknownFormatException when neither resolves to a convertible format.
   */
  String resolveFormat(String filename, String contentType) throws UnknownFormatException;

  /**
   * Converts the document to PDF.
   *
   * @param sourceFormat canonical extension previously returned by resolveFormat.
   * @return the PDF bytes.
   * @throws ConversionFailedException when the office process rejects or fails
   *         the document; {@link ConversionFailedException#timedOut()} reports
   *         whether the per-task timeout elapsed.
   */
  byte[] convertToPdf(byte[] document, String sourceFormat) throws ConversionFailedException;

  /** Canonical extensions this engine accepts as source formats. */
  java.util.List<String> supportedFormats();
}
