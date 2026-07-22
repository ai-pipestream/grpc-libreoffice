package ai.pipestream.grlibre;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import ai.pipestream.grlibre.convert.LibreOfficeEngine;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.jodconverter.local.office.LocalOfficeManager;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIf;

/**
 * Real conversions through a headless LibreOffice. Skips when soffice is not
 * on the PATH, so the protocol suite stays runnable anywhere; the Docker build
 * always has LibreOffice and always runs these.
 */
@EnabledIf("sofficeAvailable")
class LibreOfficeIntegrationTest {

  private static LocalOfficeManager manager;
  private static LibreOfficeEngine engine;

  static boolean sofficeAvailable() {
    String path = System.getenv("PATH");
    if (path == null) return false;
    for (String dir : path.split(File.pathSeparator)) {
      if (new File(dir, "soffice").canExecute()) return true;
    }
    return false;
  }

  @BeforeAll
  static void startOffice() throws Exception {
    File workingDir = Files.createTempDirectory("grlibre-it").toFile();
    manager = LocalOfficeManager.builder()
        .portNumbers(2102)
        .maxTasksPerProcess(20)
        .taskExecutionTimeout(120_000L)
        .workingDir(workingDir)
        .build();
    manager.start();
    engine = new LibreOfficeEngine(manager);
  }

  @AfterAll
  static void stopOffice() throws Exception {
    if (manager != null) manager.stop();
  }

  @Test
  void plainTextConvertsToPdf() throws Exception {
    byte[] pdf = engine.convertToPdf(
        "Hello from grpc-libreoffice.\n".getBytes(StandardCharsets.UTF_8), "txt");
    assertTrue(pdf.length > 500, "PDF suspiciously small: " + pdf.length);
    assertEquals("%PDF-", new String(pdf, 0, 5, StandardCharsets.US_ASCII));
  }

  @Test
  void minimalDocxConvertsToPdf() throws Exception {
    byte[] pdf = engine.convertToPdf(minimalDocx("Converted by the office bridge."), "docx");
    assertTrue(pdf.length > 500, "PDF suspiciously small: " + pdf.length);
    assertEquals("%PDF-", new String(pdf, 0, 5, StandardCharsets.US_ASCII));
  }

  @Test
  void formatResolutionUsesRegistry() throws Exception {
    assertEquals("docx", engine.resolveFormat("report.DOCX", ""));
    assertEquals("odt", engine.resolveFormat("notes.odt", null));
    assertTrue(engine.supportedFormats().contains("docx"));
    assertTrue(engine.supportedFormats().contains("ods"));
  }

  /** A minimal but valid DOCX authored in memory: no fixture binaries. */
  private static byte[] minimalDocx(String text) throws Exception {
    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
      put(zip, "[Content_Types].xml", """
          <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
          <Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
            <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
            <Default Extension="xml" ContentType="application/xml"/>
            <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
          </Types>
          """);
      put(zip, "_rels/.rels", """
          <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
          <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
            <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
          </Relationships>
          """);
      put(zip, "word/document.xml", """
          <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
          <w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
            <w:body><w:p><w:r><w:t>%s</w:t></w:r></w:p></w:body>
          </w:document>
          """.formatted(text));
    }
    return bytes.toByteArray();
  }

  private static void put(ZipOutputStream zip, String name, String content) throws Exception {
    zip.putNextEntry(new ZipEntry(name));
    zip.write(content.getBytes(StandardCharsets.UTF_8));
    zip.closeEntry();
  }
}
