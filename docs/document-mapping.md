# What the document fold captures

`ToDocument` folds the `StreamPages` event stream into one
`ai.pipestream.document.v1.Document`. The wire is the lossless boundary and
the fold is the lossy one, so this page is the honest ledger of what
survives the fold, what only survives untyped, and what is not captured yet.

It is a companion to the capture audit; the sections below follow the order
the audit ranked the gaps in.

Every fact this collector extracts now has a typed field to land in. No
value rides `custom_fields` any more.

## Captured in typed fields

| What | Where it lands |
|---|---|
| Resolved text of a field (page number, date, cross-reference, caption number, index or mail-merge result) | inline in `TextItemBase.text`, with an `InlineSpan.field_code` over its range |
| A cross-reference's destination | `InlineSpan.target`, a `FineRef` into the item the named anchor sits in |
| Every hyperlink, not just the first | one `InlineSpan.hyperlink` per run over its own range; `TextItemBase.hyperlink` still names the first |
| Per-run character formatting | `TextItemBase.spans` / `TableCell.spans`: `formatting` (bold, italic, underline, strikethrough, monospace, small caps, overline, script), `font_family`, `font_size_pt`, `color`, `language`, `style_name`, `highlight_color` |
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
| The page style in force on a page | `PageItem.style_name`, resolving into `Document.page_styles` |

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

## Where the rest of the office plane lands

Everything that used to sit in a `custom_fields` value map has a typed home
now, and the map copies are gone.

| What | Where it lands |
|---|---|
| Comment identity and threading | `TextItemBase.comment_meta`: author, initials, timestamp, resolved, `parent` `FineRef` to the comment it replies to, anchored text |
| Shape identity | `TextItemBase.shape` / `PictureItem.shape` (`ShapeMeta`): shape type, name, text-frame chain names, z order, rotation in degrees |
| Footnote and endnote attribution | `TextItemBase.footnote_meta`: label, endnote flag |
| Index attribution | `TextItemBase.index_meta`: the source's index service name and title |
| The property block | `DocumentMeta`: subject, `modified_by`, printed instant and printer, template, editing cycles and duration, `DocumentStatistics`, typed `UserProperty` values |
| Page styles | `Document.page_styles` (`PageStyle`): name, size, four margins, column count |
| Named and database ranges | `Document.named_ranges` (`NamedRange`): name, `GridSpan`, header and totals flags, kind |
| Pivot tables | `Document.pivots` (`PivotSpec`): name, source and output `GridSpan`, the four axis field lists |
| Sheet attributes | `GroupItem.sheet` (`SheetMeta`): index, visibility, tab color, print areas as grid spans |
| Embedded object identity | `Document.attachments` (`SubDocumentRef`): id, name, media type, `class_id`, `kind`, and the item the payload became |
| A picture's accessibility title | `PictureMeta.accessibility_title`, beside `description` |
| Character style, highlight, overline | `InlineSpan.style_name`, `.highlight_color`, `Formatting.overline` |
| Form field identity | `FieldItem.field_name`, `.options`, `.selected_index` (presence-tracked), `.span` (a `FineRef` into the item space), `.parameters` |
| A permanently shown spreadsheet note | `CommentMeta.shown` |
| A chart's data sources | `PictureItem.chart` (`ChartMeta`): source `GridSpan`s and the two header flags |
| A name defined as a formula | `NamedRange.expression`, with `range` unset |
| Form fields | the schema's own form subtree, below |

### The form subtree

A form field is no longer a text item with a bag of attributes. Each one
becomes:

```
#/body
  #/field_regions/0            FieldRegionItem   the form area
    #/field_items/N            FieldItem         one per office form field
      #/texts/k                FieldHeadingItem  the field's label
      #/texts/v                FieldValueItem    its value, with `kind`
  #/form_items/0               FormItem          the key-to-value graph
```

`FormItem.graph` carries one `GraphCell` per label and per value, with the
cell's `item_ref` pointing at the item it describes and a
`GRAPH_LINK_LABEL_TO_VALUE` link joining the pair. A checkbox's value cell
is a `GRAPH_CELL_LABEL_CHECKBOX`, and its item carries
`DOC_ITEM_LABEL_CHECKBOX_SELECTED` or `_UNSELECTED`. The integrity walk
resolves every one of those references like any other.

## Which page style applies where

Every page names its own style. The wire carries it on `PageImage.page_style`
and the fold puts it on `PageItem.style_name`, where it resolves by name into
the `PageStyle` declarations `Document.page_styles` already collected.

The name is read off the laid-out document, not inferred from the style
declarations: extraction walks the view cursor's page cursor one page at a
time and reads the cursor's own `PageStyleName`, which is the style the
layout put on the page the cursor is standing on. A document that switches
styles mid-flow therefore reports the switch on the page it happens, and a
style carrying many pages names all of them. Spreadsheets answer per sheet
(the sheet's `PageStyle` property) and presentations and drawings per page
(the page's master), both indexed by part; a presentation's notes pages have
no style of their own and carry none. Only text documents emit the
`PageStyleInfo` catalogue, so on the other classes the name arrives with
nothing declared to resolve it against.

Because the name rides the page image, it is present exactly when page
images are: a request that deselects the pages part gets `PageItem`s with
sizes and no style name.

The style catalogue arrives after the page images do, so the fold checks the
names against it when the stream closes. A name that matches no declaration
is kept, because it is still what the layout reported, and named in a
warning rather than silently dropped.

A header or footer block still reports the page style it belongs to, which
is a different fact: it says which pages the block repeats on, not which
style a page uses. That name stays on the block item's `custom_fields`,
where it was.

## Still without a typed home

Nothing. Three shapes are worth naming because they are not losses but
choices:

- A `FieldItem.parameters` value is a string. Fieldmark parameters are an
  open per-field vocabulary and the schema types the map that way; the wire
  carries them typed, and a stored list keeps one entry per key
  (`Entries[0]`, `Entries[1]`) rather than a joined string, so nothing has
  to be split back apart.
- A draw-page form control and an in-text fieldmark are told apart by
  whether the field carries a `span`: a fieldmark anchors in the annotation
  text space, a control has only its box. The office wire's own boolean for
  the distinction is not kept separately.
- A shape group's own shape type is not recorded. It is always the office
  core's group shape, which `GROUP_LABEL_PICTURE_AREA` already says.

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
- **What a cross-reference points at.** `InlineSpan.reference_kind` exists,
  but a text document's cross-reference names a bookmark, a reference mark,
  or a sequence, and none of those maps onto citation, footnote, claim, or
  section with any confidence. It stays unset rather than guessing.
- **The style catalogue.** `StyleFamilies` is never enumerated, so a consumer
  cannot tell that "Quote" is a blockquote and "Code" is code.
- **Footnote citation marks in the body.** The in-body note portion is still
  skipped, so a footnote has no in-text position.
- **OLE payload bytes.** Only the replacement graphic is encoded.

Fold side:

- **Chart series beyond the first** for bar, column, and pie charts. The
  tabular projection keeps them all; the typed annotation keeps one.
- **A spreadsheet chart still mints two pictures**, one carrying the source
  ranges in `ChartMeta` and one carrying the typed chart data, and nothing
  links them.
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
