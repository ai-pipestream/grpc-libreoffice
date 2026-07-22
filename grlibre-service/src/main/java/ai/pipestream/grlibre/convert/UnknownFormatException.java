package ai.pipestream.grlibre.convert;

/** The source format could not be determined from filename or content type. */
public final class UnknownFormatException extends Exception {
  public UnknownFormatException(String message) {
    super(message);
  }
}
