package ai.pipestream.grlibre.convert;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.TimeoutException;
import org.jodconverter.core.document.DefaultDocumentFormatRegistry;
import org.jodconverter.core.document.DocumentFormat;
import org.jodconverter.core.office.OfficeException;
import org.jodconverter.core.office.OfficeManager;
import org.jodconverter.local.LocalConverter;

/**
 * ConversionEngine over a pooled JODConverter office manager. Streams in and
 * out of memory; the only filesystem the office processes see is the manager's
 * working directory, which the server points at memory-backed tmp storage.
 */
public final class LibreOfficeEngine implements ConversionEngine {

  // Extensions probed against the JODConverter format registry at startup.
  // Only the ones the registry resolves to a loadable format are advertised.
  private static final List<String> CANDIDATE_EXTENSIONS = List.of(
      "doc", "docx", "dot", "dotx", "rtf", "txt", "html", "odt", "ott", "fodt", "wpd",
      "xls", "xlsx", "xlt", "xltx", "csv", "tsv", "ods", "ots", "fods",
      "ppt", "pptx", "pot", "potx", "odp", "otp", "fodp",
      "odg", "fodg", "vsd", "vsdx");

  private final OfficeManager manager;
  private final List<String> supported;

  public LibreOfficeEngine(OfficeManager manager) {
    this.manager = manager;
    this.supported = CANDIDATE_EXTENSIONS.stream()
        .filter(extension -> DefaultDocumentFormatRegistry.getFormatByExtension(extension) != null)
        .toList();
  }

  @Override
  public String resolveFormat(String filename, String contentType) throws UnknownFormatException {
    String extension = extensionOf(filename);
    if (extension != null) {
      DocumentFormat format = DefaultDocumentFormatRegistry.getFormatByExtension(extension);
      if (format != null) return format.getExtension();
    }
    if (contentType != null && !contentType.isBlank()) {
      String bare = contentType.split(";", 2)[0].strip().toLowerCase(Locale.ROOT);
      DocumentFormat format = DefaultDocumentFormatRegistry.getFormatByMediaType(bare);
      if (format != null) return format.getExtension();
    }
    throw new UnknownFormatException(
        "cannot determine source format from filename \"" + filename
            + "\" or content type \"" + contentType + "\"");
  }

  @Override
  public byte[] convertToPdf(byte[] document, String sourceFormat) throws ConversionFailedException {
    DocumentFormat format = DefaultDocumentFormatRegistry.getFormatByExtension(sourceFormat);
    if (format == null) {
      throw new ConversionFailedException("unregistered source format " + sourceFormat, null, false);
    }
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    try {
      LocalConverter.make(manager)
          .convert(new ByteArrayInputStream(document))
          .as(format)
          .to(output)
          .as(DefaultDocumentFormatRegistry.PDF)
          .execute();
    } catch (OfficeException failure) {
      throw new ConversionFailedException(
          "office conversion failed: " + failure.getMessage(), failure, isTimeout(failure));
    }
    return output.toByteArray();
  }

  @Override
  public List<String> supportedFormats() {
    return supported;
  }

  private static boolean isTimeout(Throwable failure) {
    for (Throwable cause = failure; cause != null; cause = cause.getCause()) {
      if (cause instanceof TimeoutException) return true;
    }
    return false;
  }

  private static String extensionOf(String filename) {
    if (filename == null) return null;
    int dot = filename.lastIndexOf('.');
    if (dot < 0 || dot == filename.length() - 1) return null;
    return filename.substring(dot + 1).toLowerCase(Locale.ROOT);
  }
}
