const form = document.querySelector("#uploadForm");
const fileInput = document.querySelector("#zipFile");
const message = document.querySelector("#message");
const tbody = document.querySelector("#rows");

const labels = {
  ready: "Sorunsuz",
  review: "İnceleme",
  blocked: "Blokaj"
};

function setSummary(summary = {}) {
  for (const id of ["total", "ready", "review", "blocked"]) {
    document.querySelector(`#${id}`).textContent = summary[id] ?? 0;
  }
}

function cell(value) {
  const td = document.createElement("td");
  td.textContent = value || "";
  return td;
}

function renderRows(rows) {
  tbody.replaceChildren();
  if (!rows.length) {
    const row = document.createElement("tr");
    row.className = "empty";
    const td = cell("İnceleme satırı bulunamadı.");
    td.colSpan = 9;
    row.append(td);
    tbody.append(row);
    return;
  }
  for (const item of rows) {
    const row = document.createElement("tr");
    row.className = item.status;
    const status = cell(labels[item.status] || item.status);
    status.className = "badge";
    row.append(
      status,
      cell(item.cells.serviceDate),
      cell(item.cells.office),
      cell(item.cells.caseNumber),
      cell(item.cells.debtor),
      cell(item.cells.debtorId),
      cell(item.cells.amount),
      cell(item.cells.recipient),
      cell([...(item.warnings || []), ...(item.blockers || [])].join("; "))
    );
    tbody.append(row);
  }
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const [file] = fileInput.files;
  if (!file) return;
  const button = form.querySelector("button");
  button.disabled = true;
  message.textContent = "ZIP işleniyor...";
  try {
    const body = new FormData();
    body.append("file", file);
    const response = await fetch("/api/imports", { method: "POST", body });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error?.message || "İçe aktarma başarısız.");
    setSummary(payload.summary);
    renderRows(payload.rows || []);
    message.textContent = `${payload.batch.importedCount} belge işlendi. Batch: ${payload.batch.id}`;
  } catch (error) {
    message.textContent = error.message;
  } finally {
    button.disabled = false;
  }
});
