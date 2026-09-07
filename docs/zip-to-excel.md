# ZIP to accounting Excel (MUZ-3)

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
   without ICU. Money is validated as integer cents; IDs/IBAN stay text. Service
   dates require an explicit labelled value or manual entry, never a filename date.
4. Match envelopes by parent group plus case number, then compare office and
   recipient where available. Different recipients override the same folder/case.
   Unmatched or ambiguous envelopes stay visible. No automatic case creation,
   silent deduplication of notices, or automatic legal approval occurs.
5. Review and edit in the table, inspecting the original text beside it. Select
   multiple rows for date entry and approval. Source identities and original
   extraction evidence are immutable; manual values add separate evidence. Changes
   invalidate approval; batch switching, closing and export refuse unsaved edits.
6. Export all rows as an explicit review draft, or approved rows as the accounting
   workbook. Contradictory pairs are blocked from approval; missing office/case/
   debtor, unknown notice type, malformed dates or malformed money cannot pass
   approval. Other missing fields remain blank with warnings and require the
   operator's explicit review. Source hashes are checked again before export.

Output columns exactly match the supplied example: Sıra No, Tebliğ Tarihi,
İcra Dairesi, Esas Numarası, Borç Miktarı (TL), Borçlu, Borçlu TCKN/VKN,
Alacaklı, İcra Dairesi İBAN, 89/1 Haciz İhbarnamesi mi?, Açıklama.
Recipients and warnings appear in Açıklama. Additional source/evidence worksheets
retain UUIDs, PDF hashes and matching confidence. Confidence is a rule weight,
not a calibrated probability. Values beginning with `=` remain strings.

Schema v2 upgrades v1 transactionally, preserving all earlier batches. New tables
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
contradictory pairs and accounting return-sheet import are future work.

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
24 proposed pairs). The archive note's count
is not used to assume an expected number of output rows.

For local acceptance/debugging, the same C++ service can run without UI dialogs:

```sh
muzakere --workspace /path/to/local-workspace --import-zip /path/to/input.zip --export-draft /path/to/new-review.xlsx
```

This command intentionally exports a draft only. Normal approval is an explicit
desktop action. Do not run real-data acceptance tests in GitHub Actions.
