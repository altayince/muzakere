# Accounting return and response PDFs (MUZ-11)

The first main tab exports accounting data with an empty final Muhasebe column.
The second imports the accountant's returned XLSX, previews VAR/YOK responses
and generates one PDF per selected valid debtor row. It runs locally without
Office, Python, the original ZIP, or a connection to the original computer.

Kaynaklar carries stable row IDs, approved source snapshots, draft/demo flags and
the MUZ-RETURN-1 format marker. Sıra No maps visible rows to these snapshots, so
whole-row sorting is supported. Missing, duplicate or unknown rows reject the
workbook. Changes to exported source fields block the affected row. This catches
accidental edits; workbook metadata is not a cryptographic signature or proof of
authorship. Preserve the other sheets and columns when editing Muhasebe.

Numbers, including zero and negative values, choose VAR and are stored as exact
integer cents. Turkish numeric text is accepted. Empty/whitespace cells choose
YOK. Formula, date, boolean, invalid text, excessive precision and out-of-range
amounts are errors, never silently YOK. The numeric bound is 9e14 cents.
Workbooks are limited to 16 MiB compressed, 10,000 data rows and 100 columns;
ZIP path, expansion and CRC checks run before QXlsx opens the workbook.

Only approved, otherwise valid 89/1 rows with a recipient can generate ordinary
responses. The separate demo export bypasses the approval requirement only for
test data and adds TEST VERİSİ — TASLAK to every PDF. It still blocks source errors
and non-89/1 documents. Normal outgoing workbooks always leave Muhasebe blank.

The supplied VAR/YOK texts are represented by bundled HTML resources. Source
PDFs and their example identities/signature images are not distributed. Office,
case, recipient, debtor name/ID, creditor, service date, lawyer/address and the
accounting amount are escaped before insertion. Qt creates A4 PDFs with text
and automatic pagination; no signature or transmission is performed. Preview is
HTML and may wrap differently from the final paginated PDF.

Schema v4 adds accounting_returns, response_rows, response_exports, response_profile
and append-only response_events. Return bytes are preserved with SHA-256. Import
and generation are recorded independently of the original ZIP batch, allowing
use on another computer. Generation rechecks the workbook hash and reads saved
rows. It creates a unique output directory, PDF hashes and a manifest, records
prepared exports, publishes the directory, then marks exports published. A crash
between publication and the final database update needs manual reconciliation.
The test database reset clears both workflows and profile records, keeping files.

The profile is editable and persisted locally. An optional response-profile.json
beside the executable supplies initial defaults; private deployment profiles are
never included in the public build definition or repository. The Windows package
includes required Qt plugins, PDFium and compiler runtimes. Its smoke test uses a
temporary installation and a PATH containing only Windows system directories,
then uninstalls only that temporary installation. An existing registered app
installation causes the smoke test to refuse replacement.

Synthetic tests cover amounts, blank/zero distinctions, row sorting and identity
changes, standalone persistence, native PDF text, tampering, reset and the actual
second-tab import/generation controls. Real client fixtures stay local.
