# What the document fold captures

`ToDocument` folds the `StreamPages` event stream into one
`ai.pipestream.document.v1.Document`. The wire is the lossless boundary and
the fold is the lossy one, so this page is the honest ledger of what
survives the fold, what only survives untyped, and what is not captured yet.

It is a companion to the capture audit; the sections below follow the order
the audit ranked the gaps in.

## Captured in typed fields

| What | Where it lands |
|---|---|
| Resolved text of a field (page number, date, cross-reference, caption number, index or mail-merge result) | inline in `TextItemBase.text`, with an `InlineSpan.field_code` over its range |
| A cross-reference's destination | `InlineSpan.target`, a `FineRef` into the item the named anchor sits in |
| Per-run character formatting | `TextItemBase.spans` / `TableCell.spans`: `formatting` (bold, italic, underline, strikethrough, script), `font_family`, `font_size_pt`, `color`, `language` |
| Uniform character formatting | `TextItemBase.formatting`, unchanged, now including `script` |
| Paragraph style name | `TextItemBase.style_name`, verbatim |
| Comments | items under a `GROUP_LABEL_COMMENT_SECTION` group, back-linked from the annotated item's `comments` `FineRef` with the annotated range |
| Tracked changes | `Document.changes`, each a `ChangeRecord` targeting the item range it touches |
| Bookmarks | `Document.anchors`, each a `NamedAnchor` targeting the item range it names |
| Spreadsheet cell values | `TableCell.value`: number, boolean, civil date and time, formula, or error literal, with `number_format` |
| Sheet column widths | `TableData.columns[].width` (twips), one entry per used column |
| Sheet row position | `TableData.row_prov[].grid`: sheet name, row, first used column |
| Merged and split table cells | `TableCell.row_span` / `col_span` and the base-grid position the office cell name anchors at |
| Slide pictures | `PictureItem` with bytes, parented to its slide group, page-local geometry |
| Slide tables | `TableItem` folded from the table shape's cell grid, parented to its slide group |
| Speaker notes | `CONTENT_LAYER_NOTES` items under the slide group |
| Image alt text | `PictureMeta.description.text` |
| Document properties | `Document.source_meta`: title, author, created and modified instants, language, generator, keywords (also `BaseMeta.keywords`) |
| Coordinate unit | `PageItem.unit` = `"twip"` on every page |

## The document-absolute character space

Comments, tracked changes, and bookmarks all count their positions in one
character space that spans the whole body: every body run's text in emission
order, one newline after each body paragraph, resolved field runs included.
Nothing downstream can use those numbers directly, so the fold keeps an
index of that space while body paragraphs stream past and resolves every
anchor into an item reference plus a range in that item's own text.

Resolution happens when the terminal `RenderStatus` arrives, not when the
anchor does: a comment can close before the paragraph holding it is emitted,
and a cross-reference can name an anchor several pages later.

An anchor that falls in content the fold does not emit (a table cell, a
header, a footnote) keeps its record with no target rather than being
dropped. An unanchored change is still evidence that the change exists.

## Reaching the document only as untyped values

These still ride `custom_fields`, a `google.protobuf.Value` map. Each one
has a knowable shape and no typed home in the document schema yet; the map
is a holding pen, not a destination.

| What | Shape it wants |
|---|---|
| Comment identity and thread structure | author, initials, instant, resolved flag, parent comment reference, anchored text |
| Document properties beyond the metadata slot | subject, `modified_by`, printed instant and printer, template name, editing cycles and duration, per-name statistics, typed user properties |
| Page styles | name, size, four margins, column count, and the variant (shared, first, left, right) |
| Form fields | the schema's own `FormItem` / `FieldItem` / `FieldValueItem` subtree, which nothing populates today |
| Named ranges, database ranges, pivot tables | name, sheet reference, row and column spans, header and totals flags, axis field lists |
| Sheet attributes | index, name, visibility, tab color, print areas |
| Shape identity | shape type, name, text-frame chain names, z order, rotation |
| Embedded object identity | name, class id, kind |
| Index and note attribution | index service name and title; footnote label, endnote flag, and the citation mark's position |

## Dates and times

Instants are typed: `DocumentMeta.created` and `.modified` and
`ChangeRecord.timestamp` are `google.protobuf.Timestamp`, converted from the
epoch milliseconds the office wire carries. The office core does not report
the source's own spelling of a document date, so the `_raw` twins the schema
offers stay unset rather than holding a re-rendering of the parsed value.

A spreadsheet date is not an instant. It is a wall-clock value the document
writes without a timezone, so it stays one: the extractor resolves the
cell's serial against the document's own null date into calendar
components, and the fold puts them in `CellValue.datetime`, a
`CivilDateTime`. Nothing along that path invents an offset.

## Not captured yet

Extraction side:

- **Rich text inside text-table cells.** A cell's text is one flat string, so
  bold, links, and footnotes inside a table cell are invisible. The schema's
  `TableCell.spans` is wired and waiting; the walk has to emit per-cell runs
  first.
- **Master-page content on slides.** Only the master's name is read, so slide
  furniture (logos, footers, slide numbers, date placeholders) never
  appears. It belongs in `Document.furniture` on the furniture layer.
- **Hidden slides, transitions, animation order.** A hidden slide should map
  to `CONTENT_LAYER_INVISIBLE` the way a hidden sheet already does.
- **First-page and even/odd headers and footers.** Only the shared variants
  are read.
- **Calc cell styling**, hidden rows and columns, row heights, frozen panes,
  conditional formatting, data validation, sheet protection.
- **Cell hyperlinks**, image hyperlinks, and image maps.
- **The style catalogue.** `StyleFamilies` is never enumerated, so a consumer
  cannot tell that "Quote" is a blockquote and "Code" is code.
- **Footnote citation marks in the body.** The in-body note portion is still
  skipped, so a footnote has no in-text position.
- **OLE payload bytes.** Only the replacement graphic is encoded.

Fold side:

- **Chart series beyond the first** for bar, column, and pie charts. The
  tabular projection keeps them all; the typed annotation keeps one.
- **A spreadsheet chart still mints two pictures**, one carrying the source
  ranges and one carrying the data, and nothing links them.
- **List markers and nesting depth.** `ListItem.marker`, `.enumerated`, and
  the nesting level are unset, so nested lists flatten.
- **Printed page numbering restarts.** `Paragraph.page_number_offset` is on
  the wire and unread, so page numbers stay physical indices.
- **A sheet's used-range origin.** Only the end bounds become `num_rows` and
  `num_cols`, so a sheet whose data starts at M50 reports a mostly empty
  50 by 13 table.
- **Render warnings** stay beside the document in `ToDocumentResponse.status`
  rather than inside it, so a consumer holding only a `Document` cannot see
  that fonts were substituted.

## Cell names that anchor nowhere

A split cell's office name ("B7.1.2") starts with the base-grid cell it was
split from, so the fold places it there with its merge spans. A cell name
that parses to no position at all is the one case that still falls back to
`TableItem.meta.custom_fields["cell:<name>"]`, which keeps the text rather
than dropping it.
