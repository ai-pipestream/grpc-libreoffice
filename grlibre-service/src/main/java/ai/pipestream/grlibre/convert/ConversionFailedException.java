package ai.pipestream.grlibre.convert;

/** The office process rejected or failed a conversion. */
public final class ConversionFailedException extends Exception {

  private final boolean timedOut;

  public ConversionFailedException(String message, Throwable cause, boolean timedOut) {
    super(message, cause);
    this.timedOut = timedOut;
  }

  /** Whether the per-task timeout elapsed before the conversion finished. */
  public boolean timedOut() {
    return timedOut;
  }
}
