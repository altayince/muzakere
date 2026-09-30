"""Browser regressions using synthetic API responses, without client documents.

Install: python -m pip install playwright && python -m playwright install chromium
Run: python tests/web_ui_test.py
C++ web_tests.cpp covers the real API/exporter; this test covers browser behavior.
"""
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
from urllib.parse import urlparse

from playwright.sync_api import sync_playwright, expect

ASSETS = Path(__file__).resolve().parents[1] / "resources" / "web"
CELLS = dict(serviceDate="30.09.2026", office="İstanbul Anadolu 12. İcra Dairesi",
             caseNumber="2026/12345", amount="1.250,00", debtor="Örnek Borçlu",
             debtorId="00000000000", creditor="Örnek Alacaklı", iban="TR000000000000000000000000",
             firstNotice="Evet", notes="Sentetik test", recipient="Örnek Muhatap")


class DownloadHandler(BaseHTTPRequestHandler):
    # Chromium downloads can bypass Playwright interception. Serve real bytes
    # locally so the test checks completion, not merely a download-start event.
    def do_GET(self):
        files = {
            "/api/exports/opaque/download": ("hamdata.xlsx", b"synthetic workbook download"),
            "/api/response-exports/opaque/files/file-1": ("cevap.pdf", b"%PDF-synthetic"),
        }
        if self.path not in files:
            self.send_error(404)
            return
        filename, content = files[self.path]
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def log_message(self, *_args):
        pass


def run():
    review_rows = [dict(id=f"row-{i}", cells=copy.deepcopy(CELLS), status=status,
                        approved=False, warnings=["Kontrol edin"] if status == "review" else [],
                        blockers=["Eksik bilgi"] if status == "blocked" else [])
                   for i, status in enumerate(["ready", "review", "blocked"])]
    review_rows[1]["cells"]["debtor"] = "Çok uzun sentetik borçlu unvanı " * 5
    return_rows = [dict(id=f"response-{i}", status=status, decision=decision,
                        amount={"text": "1.250,00"} if i == 0 else None,
                        **{key: value for key, value in CELLS.items() if key != "amount"},
                        warnings=["İnceleyin"] if status == "review" else [],
                        blockers=["Muhasebe eşleşmesi yok"] if status == "blocked" else [])
                   for i, (status, decision) in enumerate([("ready", "var"), ("review", "yok"), ("blocked", None)])]
    calls = []
    errors = []
    fail_export = False
    held_returns = []
    hold_return = False
    held_previews = []
    hold_preview = False
    held_reloads = []
    hold_reload = False

    def review_payload():
        return dict(batch={"id": "batch-1", "importedCount": 3}, rows=review_rows,
                    summary=dict(total=3, ready=1, review=1, blocked=1, approved=0))

    def route_request(route):
        nonlocal fail_export
        path = urlparse(route.request.url).path
        body = route.request.post_data_json if "application/json" in route.request.headers.get("content-type", "") else None
        calls.append((path, body))
        status = 200
        if path in ["/", "/app.js", "/styles.css"]:
            name = "index.html" if path == "/" else path[1:]
            content_type = {"index.html": "text/html", "app.js": "text/javascript", "styles.css": "text/css"}[name]
            route.fulfill(body=(ASSETS / name).read_text(encoding="utf-8-sig"), content_type=content_type)
            return
        if path == "/api/response-profile":
            payload = {"profile": {"lawyer": "Av. Örnek", "address": "Örnek adres"}}
        elif path == "/api/imports" or path.endswith("/review"):
            payload = review_payload()
        elif path.endswith("/review/save"):
            for edit in body["rows"]:
                row = next(row for row in review_rows if row["id"] == edit["id"])
                row["cells"].update(edit["cells"])
            payload = review_payload()
        elif "/exports/" in path and not path.endswith("/download"):
            if fail_export:
                status, payload = 400, {"error": {"message": "Sentetik dışa aktarma hatası"}}
            else:
                status = 201
                payload = dict(filename="hamdata.xlsx", downloadUrl="/api/exports/opaque/download")
        elif path == "/api/exports/opaque/download":
            route.continue_()
            return
        elif path.endswith("/preview"):
            if hold_preview:
                held_previews.append(route)
                return
            payload = {"html": "<p>" + body["profile"]["lawyer"] + "</p>"}
        elif path.endswith("/response-exports"):
            status = 201
            payload = {"files": [dict(filename="cevap.pdf", downloadUrl="/api/response-exports/opaque/files/file-1")]}
        elif path == "/api/response-exports/opaque/files/file-1":
            route.continue_()
            return
        elif path.startswith("/api/accounting-returns"):
            if hold_return and route.request.method == "POST":
                held_returns.append(route)
                return
            if hold_reload and route.request.method == "GET":
                held_reloads.append(route)
                return
            payload = {"return": {"id": "return-1"}, "rows": return_rows,
                       "summary": dict(total=3, var=1, yok=1, review=1, blocked=1)}
        else:
            raise AssertionError(f"Unexpected request: {path}")
        route.fulfill(status=status, json=payload)

    with ThreadingHTTPServer(("127.0.0.1", 0), DownloadHandler) as server, sync_playwright() as playwright:
        Thread(target=server.serve_forever, daemon=True).start()
        browser = playwright.chromium.launch()
        page = browser.new_page(viewport={"width": 1440, "height": 1000}, reduced_motion="reduce")
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.add_init_script("window.showSaveFilePicker = undefined;")
        page.route("**/*", route_request)
        page.goto(f"http://127.0.0.1:{server.server_port}/")
        expect(page.locator(".stage")).to_have_count(2)
        expect(page.locator("#stage-accounting #generateResponses")).to_have_count(1)
        expect(page.locator("#responsePreviewPanel")).to_be_hidden()
        page.locator("#zipFile").set_input_files({"name": "synthetic.zip", "mimeType": "application/zip", "buffer": b"zip"})
        page.locator("#uploadForm button").click()
        expect(page.locator("#rows > tr:not(.detail-row)")).to_have_count(3)
        editor = page.get_by_role("button", name="Düzenle: Örnek Borçlu", exact=True).first
        editor.focus()
        page.keyboard.press("Enter")
        expect(editor).to_have_attribute("aria-expanded", "true")
        expect(page.locator("#review-detail-row-0 [data-field]")).to_have_count(10)
        page.locator("#edit-row-0-debtor").fill("Düzeltilmiş Borçlu")
        expect(page.locator("#exportHamdata")).to_be_disabled()
        expect(page.locator("#exportHamdataAccounting")).to_be_disabled()
        expect(page.locator('#rows [data-display="debtor"]').first).to_have_text("Düzeltilmiş Borçlu")
        editor.click()
        page.locator("#saveChanges").click()
        expect(page.locator("#saveChanges")).to_be_disabled()
        saved = next(body for path, body in calls if path.endswith("/review/save"))
        assert len(saved["rows"][0]["cells"]) == 10
        assert saved["rows"][0]["cells"]["debtor"] == "Düzeltilmiş Borçlu"
        assert not any(path.endswith("/approve") for path, _ in calls)
        for button in ["#exportHamdata", "#exportHamdataAccounting"]:
            with page.expect_download() as download:
                page.locator(button).click()
            assert download.value.suggested_filename == "hamdata.xlsx"
            assert Path(download.value.path()).read_bytes() == b"synthetic workbook download"
            expect(page.locator("#downloadWorkbook")).to_be_visible()
        assert any(path.endswith("/exports/hamdata") for path, _ in calls)
        assert any(path.endswith("/exports/hamdata-with-accounting") for path, _ in calls)
        fail_export = True
        page.locator("#exportHamdata").click()
        expect(page.locator("#message")).to_have_text("Sentetik dışa aktarma hatası")
        expect(page.locator("#downloadPanel")).to_be_hidden()
        expect(page.locator("#exportHamdata")).to_be_enabled()

        def upload_return():
            page.locator("#returnFile").set_input_files({"name": "synthetic.xlsx", "mimeType": "application/octet-stream", "buffer": b"xlsx"})
            page.locator("#returnForm button").click()
            expect(page.locator("#returnRows > tr:not(.detail-row)")).to_have_count(3)

        upload_return()
        expect(page.locator('#returnRows input[value="response-2"]')).to_be_disabled()
        expect(page.locator('#returnRows [data-row-id="response-2"][data-role="preview-response"]')).to_be_disabled()
        expect(page.locator(".decision-blocked")).to_have_text("—")
        page.locator("#selectValidResponses").click()
        expect(page.locator("#returnRows input:checked")).to_have_count(2)
        page.locator("#responseLawyer").fill("Av. Güncel Form")
        page.locator('[data-role="preview-response"][data-row-id="response-0"]').click()
        expect(page.locator("#responsePreviewFrame")).to_have_attribute("srcdoc", "<p>Av. Güncel Form</p>")
        page.locator("#generateResponses").click()
        expect(page.locator("#responseDownloadLabel")).to_have_text("1 PDF hazır.")
        request_body = next(body for path, body in calls if path.endswith("/response-exports"))
        assert request_body["rowIds"] == ["response-0", "response-1"]
        assert request_body["profile"]["lawyer"] == "Av. Güncel Form"
        with page.expect_download() as download:
            page.get_by_role("link", name="cevap.pdf PDF indir").click()
        assert download.value.suggested_filename == "cevap.pdf"
        assert Path(download.value.path()).read_bytes() == b"%PDF-synthetic"

        # Native-save branch: picker is called in the user gesture, before the
        # API work; the downloaded bytes are written and the file is closed.
        fail_export = False
        page.evaluate("""() => {
          window.savedFiles = [];
          window.showSaveFilePicker = async options => {
            const record = { name: options.suggestedName, activation: navigator.userActivation.isActive };
            savedFiles.push(record);
            return { createWritable: async () => ({
              write: async blob => { record.text = await blob.text(); },
              close: async () => { record.closed = true; }, abort: async () => {}
            }) };
          };
        }""")
        for button in ["#exportHamdata", "#exportHamdataAccounting"]:
            page.locator(button).click()
            expect(page.locator("#message")).to_have_text("hamdata.xlsx kaydedildi.")
        page.get_by_role("link", name="cevap.pdf PDF indir").click()
        expect(page.locator("#returnMessage")).to_have_text("cevap.pdf kaydedildi.")
        saved_files = page.evaluate("savedFiles")
        assert [item["name"] for item in saved_files] == ["HAMDATA.xlsx", "HAMDATA-MUHASEBE.xlsx", "cevap.pdf"]
        assert all(item["activation"] and item["closed"] for item in saved_files)
        assert saved_files[0]["text"] == "synthetic workbook download"
        assert saved_files[2]["text"] == "%PDF-synthetic"
        before_cancel = len(calls)
        page.evaluate("() => { window.showSaveFilePicker = async () => { throw new DOMException('cancel', 'AbortError'); }; }")
        page.locator("#exportHamdata").click()
        expect(page.locator("#message")).to_have_text("Kaydetme iptal edildi.")
        assert len(calls) == before_cancel
        expect(page.locator("#exportHamdata")).to_be_enabled()

        # Hold a new upload pending: old rows remain but actions must not run.
        # Also deliver old preview/reload results late, after the new flow began.
        hold_preview = True
        page.locator('[data-role="preview-response"][data-row-id="response-0"]').click()
        expect(page.locator("#responsePreviewStatus")).to_have_text("Önizleme hazırlanıyor...")
        hold_reload = True
        page.locator("#reloadReturn").click()
        expect(page.locator("#returnMessage")).to_have_text("Muhasebe dönüşü yenileniyor...")
        hold_return = True
        page.locator("#returnForm button").click()
        expect(page.locator("#returnMessage")).to_have_text("Muhasebe dönüşü işleniyor...")
        for selector in ["#reloadReturn", "#selectValidResponses", "#previewSelectedResponse", "#generateResponses",
                         '[data-role="preview-response"][data-row-id="response-0"]']:
            expect(page.locator(selector)).to_be_disabled()
        expect(page.locator('#returnRows input[value="response-0"]')).to_be_disabled()
        assert len(held_returns) == len(held_previews) == len(held_reloads) == 1
        held_previews.pop().fulfill(json={"html": "<p>stale preview</p>"})
        held_reloads.pop().fulfill(json={"rows": [], "summary": {"total": 999}})
        expect(page.locator("#responsePreviewPanel")).to_be_hidden()
        expect(page.locator("#returnTotal")).to_have_text("3")
        held_returns.pop().fulfill(status=400, json={"error": {"message": "Sentetik yükleme hatası"}})
        expect(page.locator("#returnMessage")).to_have_text("Sentetik yükleme hatası")
        expect(page.locator("#returnRows input:checked")).to_have_count(2)
        expect(page.locator("#generateResponses")).to_be_enabled()
        expect(page.locator("#reloadReturn")).to_be_enabled()
        expect(page.locator('#returnRows input[value="response-2"]')).to_be_disabled()
        hold_return = hold_preview = hold_reload = False

        for width in [1440, 1024, 768, 390, 320]:
            page.set_viewport_size({"width": width, "height": 1000})
            assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth"), f"Page overflow at {width}"
            for table in page.locator(".table-wrap").all():
                if width >= 1024:
                    assert table.evaluate("el => el.scrollWidth <= el.clientWidth + 1"), f"Desktop table overflow at {width}"
                else:
                    assert table.evaluate("el => { el.scrollLeft = el.scrollWidth; return el.scrollLeft + el.clientWidth >= el.scrollWidth - 1; }"), (width, table.evaluate("el => [el.scrollLeft, el.clientWidth, el.scrollWidth, el.offsetWidth]"))
        page.set_viewport_size({"width": 1440, "height": 1000})
        screenshot = Path(__file__).resolve().parents[1] / ".tools" / "web-workspace-preview.png"
        if screenshot.parent.exists():
            page.screenshot(path=str(screenshot), full_page=True)
        upload_return()
        expect(page.locator("#responseDownloadPanel")).to_be_hidden()
        expect(page.locator("#responseDownloads a")).to_have_count(0)
        expect(page.locator("#responsePreviewPanel")).to_be_hidden()
        expect(page.locator("#returnRows input:checked")).to_have_count(0)
        assert not errors, errors
        browser.close()
        server.shutdown()
    print("Browser checks passed: row editing, direct Excel downloads, PDF actions, stale state and responsive layout.")


if __name__ == "__main__":
    run()
