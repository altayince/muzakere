# Accounting return and response PDFs (MUZ-13)

The first main tab exports HAMDATA alone or HAMDATA with a MUHASEBE sheet.
The second imports the accountant's returned XLSX, previews VAR/YOK responses
and generates one PDF per selected valid debtor row. It runs locally without
Office, Python, the original ZIP, or a connection to the original computer.

HAMDATA preserves all case/debtor rows in the real office workbook's 11-column
layout. MUHASEBE deduplicates by TCKN/VKN, never by name or case number. A/B hold
identity/name; C/D hold account reference/amount, initially blank and unheaded as
in the supplied file. Replies join by identity and fan out to every matching
HAMDATA case. Different identities with the same name remain separate. Numeric
VKN values are padded to ten digits to restore zeros lost by Excel numeric cells;
text identities are preserved. Missing identities stay in HAMDATA for review and
are excluded from the outgoing accounting list. Name differences for the same
identity appear as warnings; missing or duplicate accounting matches are blockers.

Hidden _MUZ snapshots preserve original row IDs and blockers for generated files.
Fingerprints are order-independent; missing/changed/extra source rows are rejected.
This catches accidental edits, not deliberate metadata tampering. External real
HAMDATA/MUHASEBE workbooks do not need private metadata or the original ZIP.
The second-tab selection and preview are the review step for those files.
Legacy MUZ-11 Kaynaklar/İcra Dosyaları returns remain readable, including their
approval/demo rules. The random return button and CLI option have been removed.

HAMDATA reply deadline and delivery channel stay blank when unknown. They are
not inferred from the reference workbook. Extra MUHASEBE identities (including
the supplied İİK 78 list) do not create PDFs without matching case details in
HAMDATA. No new legal document type or deadline calculation is inferred.

Numbers, including zero and negative values, choose VAR and are stored as exact
integer cents. Turkish numeric text is accepted. Empty/whitespace cells choose
YOK. Formula, date, boolean, invalid text, excessive precision and out-of-range
amounts are errors, never silently YOK. The numeric bound is 9e14 cents.
Workbooks are limited to 16 MiB compressed, 10,000 data rows and 100 columns;
ZIP path, expansion and CRC checks run before QXlsx opens the workbook.

Account references in C/E/etc. are not money. D/F/etc. contain reply amounts.
A row with multiple amounts stays blocked pending an explicit aggregation rule;
the application never silently chooses a balance. Valid 89/1 case details and a
recipient are required. One PDF is generated per valid HAMDATA row. Multiple
notices for a debtor retain independent offices/case numbers and PDFs.

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
