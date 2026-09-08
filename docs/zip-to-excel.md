# ZIP to accounting Excel (MUZ-3)

MUZ-13 replaces the visible export with the real HAMDATA/MUHASEBE layout and two
raw/with-accounting buttons. See [current workflow](accounting-return.md). The
older column layout and draft/approved export details below describe the retained
legacy adapter, not the current desktop export buttons.

The desktop application owns the full workflow. The user supplies a ZIP; no
one-off Python-generated workbook or external API is part of the runtime.

1. Preserve the ZIP under its SHA-256 before opening it. Validate all member paths,
   duplicate/case-folded names, encryption flags, symlinks and expanded sizes.
   Extract into a temporary directory and use the existing import use case.
   Save archive metadata and `archive.zip!/member/path` provenance with the batch.
2. Load preserved PDFs after verifying their hashes. A local C++ PDFium worker
   reads text in a separate process, with a 30-second per-document timeout.
   It is crash isolation, not an OS security sandbox. No OCR, links, scripts,
   messages or instructions in PDFs are executed by the application.
3. Parse labelled UYAP fields into source-backed proposals. Turkish casing works
   without ICU. Money is validated as integer cents; IDs/IBAN stay text. New rows
   temporarily use the computer's local date at preparation as the service date,
   recorded separately from PDF evidence. It remains editable and is persisted;
   reopening, preparing again or exporting does not refresh saved dates.
   **TODO (MUZ-5): replace this temporary default with the service date from the
   KEP record during KEP integration.** ZIP filenames do not supply this date.
   Legacy rows with a missing date and no manual date edit are updated and saved
   when loaded; their approval is cleared for review before approved export.
   Dates explicitly cleared by the user remain blank.
4. Match envelopes by parent group plus case number, then compare office and
   recipient where available. Different recipients override the same folder/case.
   Unmatched or ambiguous envelopes stay visible. No automatic case creation,
   silent deduplication of notices, or automatic legal approval occurs.
5. Review and edit in the table, inspecting the original text beside it. Select
   multiple rows for date entry and approval. Source identities and original
   extraction evidence are immutable; manual values add separate evidence. Changes
   invalidate approval; batch switching, closing and export refuse unsaved edits.
   Saving or approving selected rows preserves unsaved changes in other rows.
6. Export all rows as an explicit review draft, or approved rows as the accounting
   workbook. Contradictory pairs are blocked from approval; missing office/case/
   debtor, unknown notice type, malformed dates or malformed money cannot pass
   approval. Other missing fields remain blank with warnings and require the
   operator's explicit review. Source hashes are checked again before export.

The first 11 output columns match the supplied example: Sıra No, Tebliğ Tarihi,
İcra Dairesi, Esas Numarası, Borç Miktarı (TL), Borçlu, Borçlu TCKN/VKN,
Alacaklı, İcra Dairesi İBAN, 89/1 Haciz İhbarnamesi mi?, Açıklama.
Columns L/M are Muhatap and Uyarılar; N is the empty Muhasebe return column.
Recipients are not included in
warnings or Açıklama; conflicting envelope recipients appear in Muhatap too.
Additional source/evidence worksheets
retain UUIDs, PDF hashes and matching confidence. Confidence is a rule weight,
not a calibrated probability. Values beginning with `=` remain strings.

Schema v2 upgrades v1 transactionally, preserving all earlier batches. Its tables
are `batch_archives`, `accounting_rows` and `accounting_exports`. An export records
the intended destination and hash as `prepared` before publishing the file using
no-overwrite rename; only a subsequent DB update records it as `published`.
A crash can leave a prepared record or a published file awaiting that update;
this is visible in the export ledger and must be reconciled manually. There is no
distributed filesystem/SQLite transaction or automatic crash recovery yet.

Limits: ZIP 256 MiB compressed, 10,000 entries, 64 MiB/member, 512 MiB total;
PDF 64 MiB, 200 pages, 200,000 characters/page and 2,000,000/document. Current
parsers cover labelled notice/general-letter layouts; arbitrary scans and new
layouts require review or a new extractor. Turkish TCKN/VKN values are extracted
as written, not verified against an identity registry. Manual reassignment of
contradictory pairs remains future work. Accounting return-sheet import and
response PDFs are implemented in [MUZ-11](accounting-return.md).

## Verification

Synthetic tests exercise ZIP traversal/collision rejection, native PDF text,
label extraction, money, ambiguous/recipient-conflicting pairs, v1 migration,
approval persistence, source tampering, no-overwrite exports and the actual Qt
import/edit/approve interaction. Real files stay local and ignored by Git.

The supplied 31 August ZIP contains 25 groups, 52 PDFs and one archive note.
It produces 27 candidate document rows: 23 first notices and four other letters
(three releases and one address enquiry). Two extra attachments address different
companies from the envelope; another document's recipient list does not include
its envelope's Banabi recipient. These three document conflicts remain in review;
the unmatched envelope is an additional visible review row (28 rows in the draft,
24 proposed pairs before splitting debtors). These are document counts, not the
current number of debtor rows. The archive note's count
is not used to assume an expected number of output rows.
With MUZ-9, four of those documents each have two debtors: the current draft has
32 rows (17 green, 11 yellow, four red), preserving 28 source-document groups.

For local acceptance/debugging, the same C++ service can run without UI dialogs:

```sh
muzakere --workspace /path/to/local-workspace --import-zip /path/to/input.zip --export-draft /path/to/new-review.xlsx
```

This command intentionally exports a draft only. Normal approval is an explicit
desktop action. Do not run real-data acceptance tests in GitHub Actions.

## Test reset (MUZ-7)

The desktop test button resets the active workspace's whole database after a
confirmation, including unsaved UI edits and all audit/export records. Schema
tables, indexes and append-only audit triggers are recreated in one SQLite
transaction; a failure rolls back the reset. No other workspace is touched.
Original ZIP/PDF files and previously generated workbooks remain on disk. Import
again to start a fresh test batch; old workbooks are not updated automatically.

## Separate debtors and review colors (MUZ-9)

Numbered debtor blocks are parsed as name/identifier pairs; a missing identifier
stays blank on that person's row. Every person gets an independent persisted row
and approval, sharing the source PDF, envelope, office and case. The document's
full amount is repeated rather than allocated; do not sum sibling rows as separate
debts. No multiple-debtor or duplicate-case warning is caused by splitting one PDF.

Schema v3 separates row ID from document ID. First rows retain their old ID;
additional IDs are deterministic per source document and debtor position. Existing
combined rows split only if the source reproduces the stored name/ID pairs exactly
and neither identity field was manually edited. Other fields and source links are
preserved, approvals cleared, and replacement/audit writes commit together. An
ambiguous legacy pair stays blocked for fresh import/review instead of guessing.

Green means no detected issues, yellow means warnings or missing optional data,
and red means an approval blocker (including recipient conflicts, invalid identity
format, malformed dates/money or missing required fields). Unsaved valid edits are
yellow. The UI and Excel share this classification; colors do not grant approval.
