const form = document.querySelector("#uploadForm");
const fileInput = document.querySelector("#zipFile");
const message = document.querySelector("#message");
const tbody = document.querySelector("#rows");
const reloadButton = document.querySelector("#reloadReview");
const saveButton = document.querySelector("#saveChanges");
const approveButton = document.querySelector("#approveRows");
const exportHamdataButton = document.querySelector("#exportHamdata");
const exportHamdataAccountingButton = document.querySelector("#exportHamdataAccounting");
const downloadPanel = document.querySelector("#downloadPanel");
const downloadLabel = document.querySelector("#downloadLabel");
const downloadWorkbook = document.querySelector("#downloadWorkbook");
const returnForm = document.querySelector("#returnForm");
const returnFileInput = document.querySelector("#returnFile");
const returnMessage = document.querySelector("#returnMessage");
const reloadReturnButton = document.querySelector("#reloadReturn");
const selectValidResponsesButton = document.querySelector("#selectValidResponses");
const previewSelectedResponseButton = document.querySelector("#previewSelectedResponse");
const saveResponseProfileButton = document.querySelector("#saveResponseProfile");
const generateResponsesButton = document.querySelector("#generateResponses");
const clearResponsePreviewButton = document.querySelector("#clearResponsePreview");
const responseLawyer = document.querySelector("#responseLawyer");
const responseAddress = document.querySelector("#responseAddress");
const responseDownloadPanel = document.querySelector("#responseDownloadPanel");
const responseDownloadLabel = document.querySelector("#responseDownloadLabel");
const responseDownloads = document.querySelector("#responseDownloads");
const responsePreviewPanel = document.querySelector("#responsePreviewPanel");
const responsePreviewTitle = document.querySelector("#responsePreviewTitle");
const responsePreviewStatus = document.querySelector("#responsePreviewStatus");
const responsePreviewFrame = document.querySelector("#responsePreviewFrame");
const returnRows = document.querySelector("#returnRows");

let currentBatchId = "";
let currentReturnId = "";
let currentRows = [];
let currentReturnRows = [];
let currentPreviewRowId = "";
const dirtyRows = new Set();

const labels = {
  ready: "Sorunsuz",
  review: "İnceleme",
  blocked: "Blokaj"
};

const editableFields = [
  ["serviceDate", "text"],
  ["office", "text"],
  ["caseNumber", "text"],
  ["amount", "text"],
  ["debtor", "text"],
  ["debtorId", "text"],
  ["creditor", "text"],
  ["iban", "text"],
  ["firstNotice", "select"],
  ["notes", "text"]
];

function clearDownload() {
  downloadPanel.hidden = true;
  downloadLabel.textContent = "";
  downloadWorkbook.removeAttribute("href");
  downloadWorkbook.removeAttribute("download");
}

function showDownload(payload) {
  downloadLabel.textContent = `${payload.filename} hazır.`;
  downloadWorkbook.href = payload.downloadUrl;
  downloadWorkbook.download = payload.filename;
  downloadPanel.hidden = false;
}

function clearResponseDownloads() {
  responseDownloadPanel.hidden = true;
  responseDownloadLabel.textContent = "";
  responseDownloads.replaceChildren();
}

function clearResponsePreview(statusText = "") {
  currentPreviewRowId = "";
  responsePreviewPanel.hidden = true;
  responsePreviewTitle.textContent = "Seçili satır";
  responsePreviewStatus.textContent = statusText;
  responsePreviewFrame.removeAttribute("srcdoc");
  updateButtons();
}

function resetReturnWorkflow(statusText = "") {
  currentReturnId = "";
  currentReturnRows = [];
  setReturnSummary();
  renderReturnRows([]);
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = statusText;
}

function showResponseDownloads(payload) {
  const files = payload.files || [];
  responseDownloadLabel.textContent = `${files.length} PDF hazır.`;
  responseDownloads.replaceChildren();
  for (const file of files) {
    const link = document.createElement("a");
    link.href = file.downloadUrl;
    link.download = file.filename;
    link.textContent = file.filename;
    responseDownloads.append(link);
  }
  responseDownloadPanel.hidden = false;
}

function setSummary(summary = {}) {
  for (const id of ["total", "ready", "review", "blocked", "approved"]) {
    document.querySelector(`#${id}`).textContent = summary[id] ?? 0;
  }
}

function setReturnSummary(summary = {}) {
  const values = {
    returnTotal: summary.total,
    returnVar: summary.var,
    returnYok: summary.yok,
    returnReview: summary.review,
    returnBlocked: summary.blocked
  };
  for (const [id, value] of Object.entries(values)) {
    document.querySelector(`#${id}`).textContent = value ?? 0;
  }
}

function cell(value) {
  const td = document.createElement("td");
  td.textContent = value || "";
  return td;
}

function editableCell(item, field, type) {
  const td = document.createElement("td");
  const input = type === "select" ? document.createElement("select") : document.createElement("input");
  if (type === "select") {
    for (const value of ["Evet", "Hayır", "Belirsiz"]) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = value;
      input.append(option);
    }
  } else {
    input.type = "text";
  }
  input.value = item.cells[field] || "";
  input.dataset.rowId = item.id;
  input.dataset.field = field;
  input.addEventListener("input", () => markDirty(item.id));
  td.append(input);
  return td;
}

function markDirty(rowId) {
  dirtyRows.add(rowId);
  clearDownload();
  const row = tbody.querySelector(`tr[data-row-id="${CSS.escape(rowId)}"]`);
  if (row) {
    row.classList.add("dirty");
    row.querySelector(".approval").textContent = "Kaydedilmedi";
  }
  updateButtons();
}

function selectedIds() {
  return [...tbody.querySelectorAll("input[data-role=select-row]:checked")].map((item) => item.value);
}

function rowPayload(rowId) {
  const cells = {};
  for (const [field] of editableFields) {
    const input = tbody.querySelector(`[data-row-id="${CSS.escape(rowId)}"][data-field="${field}"]`);
    cells[field] = input ? input.value : "";
  }
  return { id: rowId, cells };
}

function updateButtons() {
  const hasBatch = Boolean(currentBatchId);
  const hasDirty = dirtyRows.size > 0;
  const selectedResponses = selectedResponseRows();
  const hasValidResponse = currentReturnRows.some((row) => row.status !== "blocked");
  const hasProfile = responseLawyer.value.trim() !== "" && responseAddress.value.trim() !== "";
  reloadButton.disabled = !hasBatch;
  saveButton.disabled = !hasBatch || !hasDirty;
  approveButton.disabled = !hasBatch || selectedIds().length === 0;
  exportHamdataButton.disabled = !hasBatch || hasDirty;
  exportHamdataAccountingButton.disabled = !hasBatch || hasDirty;
  reloadReturnButton.disabled = !currentReturnId;
  selectValidResponsesButton.disabled = !currentReturnId || !hasValidResponse;
  previewSelectedResponseButton.disabled = !currentReturnId || selectedResponses.length !== 1;
  generateResponsesButton.disabled = !currentReturnId || selectedResponses.length === 0 || !hasProfile;
  clearResponsePreviewButton.disabled = responsePreviewPanel.hidden;
}

function renderRows(rows) {
  currentRows = rows;
  dirtyRows.clear();
  tbody.replaceChildren();
  if (!rows.length) {
    const row = document.createElement("tr");
    row.className = "empty";
    const td = cell("İnceleme satırı bulunamadı.");
    td.colSpan = 14;
    row.append(td);
    tbody.append(row);
    updateButtons();
    return;
  }
  for (const item of rows) {
    const row = document.createElement("tr");
    row.className = item.status;
    row.dataset.rowId = item.id;
    const selector = document.createElement("input");
    selector.type = "checkbox";
    selector.value = item.id;
    selector.dataset.role = "select-row";
    selector.addEventListener("change", updateButtons);
    const selectCell = document.createElement("td");
    selectCell.append(selector);
    const status = cell(labels[item.status] || item.status);
    status.className = "badge";
    const approval = document.createElement("div");
    approval.className = "approval";
    approval.textContent = item.approved ? "Onaylı" : "Onay bekliyor";
    status.append(approval);
    row.append(
      selectCell,
      status,
      editableCell(item, "serviceDate", "text"),
      editableCell(item, "office", "text"),
      editableCell(item, "caseNumber", "text"),
      editableCell(item, "amount", "text"),
      editableCell(item, "debtor", "text"),
      editableCell(item, "debtorId", "text"),
      editableCell(item, "creditor", "text"),
      editableCell(item, "iban", "text"),
      editableCell(item, "firstNotice", "select"),
      editableCell(item, "notes", "text"),
      cell(item.cells.recipient),
      cell([...(item.warnings || []), ...(item.blockers || [])].join("; "))
    );
    row.lastElementChild.className = "issues";
    tbody.append(row);
  }
  updateButtons();
}

function selectedResponseIds() {
  return selectedResponseRows().map((row) => row.id);
}

function selectedResponseRows() {
  const ids = new Set([...returnRows.querySelectorAll("input[data-role=select-response]:checked")].map((item) => item.value));
  return currentReturnRows.filter((row) => ids.has(row.id) && row.status !== "blocked");
}

function responseRowById(rowId) {
  return currentReturnRows.find((row) => row.id === rowId);
}

function renderReturnRows(rows) {
  currentReturnRows = rows;
  returnRows.replaceChildren();
  if (!rows.length) {
    const row = document.createElement("tr");
    row.className = "empty";
    const td = cell("Muhasebe dönüş satırı bulunamadı.");
    td.colSpan = 12;
    row.append(td);
    returnRows.append(row);
    updateButtons();
    return;
  }
  for (const item of rows) {
    const row = document.createElement("tr");
    row.className = item.status;
    row.dataset.rowId = item.id;
    const selector = document.createElement("input");
    selector.type = "checkbox";
    selector.value = item.id;
    selector.dataset.role = "select-response";
    selector.disabled = item.status === "blocked";
    selector.addEventListener("change", updateButtons);
    const selectCell = document.createElement("td");
    selectCell.append(selector);
    const preview = document.createElement("button");
    preview.type = "button";
    preview.className = "small-action";
    preview.textContent = "Önizle";
    preview.dataset.role = "preview-response";
    preview.dataset.rowId = item.id;
    preview.disabled = item.status === "blocked";
    preview.addEventListener("click", () => previewResponse(item.id));
    const previewCell = document.createElement("td");
    previewCell.append(preview);
    const status = cell(labels[item.status] || item.status);
    status.className = "badge";
    const decisionText = item.decision === "var" || item.decision === "yok" ? item.decision.toUpperCase() : "—";
    const decision = cell(decisionText);
    decision.className = item.decision === "var" ? "decision-var" : item.decision === "yok" ? "decision-yok" : "decision-blocked";
    row.append(
      selectCell,
      previewCell,
      status,
      decision,
      cell(item.amount?.text || item.accountingInput || ""),
      cell(item.debtor),
      cell(item.debtorId),
      cell(item.caseNumber),
      cell(item.office),
      cell(item.creditor),
      cell(item.recipient),
      cell([...(item.warnings || []), ...(item.blockers || [])].join("; "))
    );
    row.lastElementChild.className = "issues";
    returnRows.append(row);
  }
  updateButtons();
}

async function loadReturn() {
  if (!currentReturnId) return;
  returnMessage.textContent = "Muhasebe dönüşü yenileniyor...";
  const response = await fetch(`/api/accounting-returns/${encodeURIComponent(currentReturnId)}`);
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error?.message || "Muhasebe dönüşü alınamadı.");
  setReturnSummary(payload.summary);
  renderReturnRows(payload.rows || []);
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = `Muhasebe dönüşü güncel. Return: ${payload.return.id}`;
  updateButtons();
}

async function loadReview() {
  if (!currentBatchId) return;
  clearDownload();
  message.textContent = "İnceleme satırları yenileniyor...";
  const response = await fetch(`/api/batches/${encodeURIComponent(currentBatchId)}/review`);
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error?.message || "İnceleme satırları alınamadı.");
  setSummary(payload.summary);
  renderRows(payload.rows || []);
  message.textContent = "İnceleme satırları güncel.";
}

async function postJson(url, body = {}, expectedStatus = 200) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const payload = await response.json();
  if (!response.ok || response.status !== expectedStatus) throw new Error(payload.error?.message || "İşlem tamamlanamadı.");
  return payload;
}

async function generateWorkbook(kind) {
  if (!currentBatchId) return;
  if (dirtyRows.size > 0) throw new Error("Excel oluşturmadan önce değişiklikleri kaydedin.");
  const payload = await postJson(`/api/batches/${encodeURIComponent(currentBatchId)}/exports/${kind}`);
  showDownload(payload);
  message.textContent = `${payload.filename} oluşturuldu.`;
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const [file] = fileInput.files;
  if (!file) return;
  const button = form.querySelector("button");
  button.disabled = true;
  clearDownload();
  resetReturnWorkflow();
  message.textContent = "ZIP işleniyor...";
  try {
    const body = new FormData();
    body.append("file", file);
    const response = await fetch("/api/imports", { method: "POST", body });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error?.message || "İçe aktarma başarısız.");
    currentBatchId = payload.batch.id;
    setSummary(payload.summary);
    renderRows(payload.rows || []);
    message.textContent = `${payload.batch.importedCount} belge işlendi. Batch: ${payload.batch.id}`;
  } catch (error) {
    message.textContent = error.message;
  } finally {
    button.disabled = false;
  }
});

reloadButton.addEventListener("click", async () => {
  try {
    await loadReview();
  } catch (error) {
    message.textContent = error.message;
  }
});

saveButton.addEventListener("click", async () => {
  try {
    clearDownload();
    const rows = [...dirtyRows].map(rowPayload);
    const payload = await postJson(`/api/batches/${encodeURIComponent(currentBatchId)}/review/save`, { rows });
    setSummary(payload.summary);
    renderRows(payload.rows || []);
    message.textContent = "Değişiklikler kaydedildi. Onay ayrıca verilir.";
  } catch (error) {
    message.textContent = error.message;
  }
});

approveButton.addEventListener("click", async () => {
  try {
    clearDownload();
    const rowIds = selectedIds();
    if (rowIds.some((id) => dirtyRows.has(id))) throw new Error("Onaydan önce seçili satırlardaki değişiklikleri kaydedin.");
    const payload = await postJson(`/api/batches/${encodeURIComponent(currentBatchId)}/review/approve`, { rowIds });
    setSummary(payload.summary);
    renderRows(payload.rows || []);
    message.textContent = "Seçili satırlar onaylandı.";
  } catch (error) {
    message.textContent = error.message;
  }
});

exportHamdataButton.addEventListener("click", async () => {
  try {
    await generateWorkbook("hamdata");
  } catch (error) {
    message.textContent = error.message;
  }
});

exportHamdataAccountingButton.addEventListener("click", async () => {
  try {
    await generateWorkbook("hamdata-with-accounting");
  } catch (error) {
    message.textContent = error.message;
  }
});


returnForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const [file] = returnFileInput.files;
  if (!file) return;
  const button = returnForm.querySelector("button");
  button.disabled = true;
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = "Muhasebe dönüşü işleniyor...";
  try {
    const body = new FormData();
    body.append("file", file);
    const response = await fetch("/api/accounting-returns", { method: "POST", body });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error?.message || "Muhasebe dönüşü içe aktarılamadı.");
    currentReturnId = payload.return.id;
    setReturnSummary(payload.summary);
    renderReturnRows(payload.rows || []);
    clearResponseDownloads();
    clearResponsePreview();
    returnMessage.textContent = `${payload.summary.total} satır eşleşti. Return: ${payload.return.id}`;
    updateButtons();
  } catch (error) {
    returnMessage.textContent = error.message;
  } finally {
    button.disabled = false;
  }
});

reloadReturnButton.addEventListener("click", async () => {
  try {
    await loadReturn();
  } catch (error) {
    returnMessage.textContent = error.message;
  }
});


async function loadResponseProfile() {
  const response = await fetch("/api/response-profile");
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error?.message || "Profil alınamadı.");
  responseLawyer.value = payload.profile?.lawyer || "";
  responseAddress.value = payload.profile?.address || "";
  updateButtons();
}

async function previewResponse(rowId) {
  const row = responseRowById(rowId);
  if (!currentReturnId || !row || row.status === "blocked") {
    clearResponsePreview("Bu satır önizlenemez.");
    return;
  }
  currentPreviewRowId = rowId;
  responsePreviewPanel.hidden = false;
  responsePreviewTitle.textContent = `${row.caseNumber || "Dosya"} · ${row.debtor || "Borçlu"}`;
  responsePreviewStatus.textContent = "Önizleme hazırlanıyor...";
  responsePreviewFrame.removeAttribute("srcdoc");
  updateButtons();
  try {
    const response = await fetch(`/api/accounting-returns/${encodeURIComponent(currentReturnId)}/responses/${encodeURIComponent(rowId)}/preview`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        profile: {
          lawyer: responseLawyer.value,
          address: responseAddress.value
        }
      })
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error?.message || "Önizleme alınamadı.");
    if (currentPreviewRowId !== rowId) return;
    responsePreviewFrame.srcdoc = payload.html || "";
    responsePreviewStatus.textContent = "Önizleme güncel.";
  } catch (error) {
    if (currentPreviewRowId !== rowId) return;
    responsePreviewFrame.removeAttribute("srcdoc");
    responsePreviewStatus.textContent = error.message;
  } finally {
    updateButtons();
  }
}

async function saveResponseProfile() {
  const payload = await postJson("/api/response-profile", {
    lawyer: responseLawyer.value,
    address: responseAddress.value
  });
  responseLawyer.value = payload.profile?.lawyer || "";
  responseAddress.value = payload.profile?.address || "";
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = "Profil kaydedildi.";
}

selectValidResponsesButton.addEventListener("click", () => {
  for (const input of returnRows.querySelectorAll("input[data-role=select-response]")) {
    input.checked = !input.disabled;
  }
  updateButtons();
});

previewSelectedResponseButton.addEventListener("click", async () => {
  const [row] = selectedResponseRows();
  if (!row) return;
  await previewResponse(row.id);
});

clearResponsePreviewButton.addEventListener("click", () => clearResponsePreview());

saveResponseProfileButton.addEventListener("click", async () => {
  try {
    await saveResponseProfile();
  } catch (error) {
    returnMessage.textContent = error.message;
  }
});

responseLawyer.addEventListener("input", () => {
  clearResponseDownloads();
  clearResponsePreview();
  updateButtons();
});
responseAddress.addEventListener("input", () => {
  clearResponseDownloads();
  clearResponsePreview();
  updateButtons();
});

if (returnForm) {
  loadResponseProfile().catch((error) => { returnMessage.textContent = error.message; });
}

generateResponsesButton.addEventListener("click", async () => {
  try {
    clearResponseDownloads();
    const rowIds = selectedResponseIds();
    const payload = await postJson(`/api/accounting-returns/${encodeURIComponent(currentReturnId)}/response-exports`, {
      rowIds,
      profile: {
        lawyer: responseLawyer.value,
        address: responseAddress.value
      }
    }, 201);
    showResponseDownloads(payload);
    returnMessage.textContent = `${(payload.files || []).length} PDF oluşturuldu.`;
  } catch (error) {
    returnMessage.textContent = error.message;
  }
});
