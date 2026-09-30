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
let workbookBusy = false;
let responsesBusy = false;
let returnBusy = false;
let returnGeneration = 0;
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

const fieldLabels = {
  serviceDate: "Tebliğ tarihi", office: "İcra dairesi", caseNumber: "Dosya numarası",
  amount: "Tutar", debtor: "Borçlu", debtorId: "TCKN / VKN", creditor: "Alacaklı",
  iban: "IBAN", firstNotice: "89/1", notes: "Açıklama"
};

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

// Invoke the picker during the click gesture, before generation/network awaits.
// Browsers without File System Access keep their normal download behavior.
async function chooseSaveFile(filename) {
  if (!window.showSaveFilePicker) return null;
  const pdf = filename.toLowerCase().endsWith(".pdf");
  return window.showSaveFilePicker({
    suggestedName: filename,
    types: [{ description: pdf ? "PDF belgesi" : "Excel çalışma kitabı",
      accept: pdf ? { "application/pdf": [".pdf"] }
        : { "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet": [".xlsx"] } }]
  });
}

async function saveDownload(url, filename, handle) {
  const response = await fetch(url);
  if (!response.ok) {
    const payload = await response.json().catch(() => ({}));
    throw new Error(payload.error?.message || "Dosya indirilemedi. Tekrar deneyin.");
  }
  const blob = await response.blob();
  if (handle) {
    const writer = await handle.createWritable();
    try {
      await writer.write(blob);
      await writer.close();
    } catch (error) {
      await writer.abort().catch(() => {});
      throw error;
    }
  } else {
    const objectUrl = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = objectUrl;
    link.download = filename;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(objectUrl), 60000);
  }
}

async function downloadFile(url, filename, status) {
  try {
    const handle = await chooseSaveFile(filename);
    status.textContent = `${filename} indiriliyor...`;
    await saveDownload(url, filename, handle);
    status.textContent = handle ? `${filename} kaydedildi.` : `${filename} tarayıcı indirmelerine gönderildi.`;
  } catch (error) {
    status.textContent = error.name === "AbortError" ? "Kaydetme iptal edildi." : error.message;
  }
}

downloadWorkbook.addEventListener("click", (event) => {
  event.preventDefault();
  downloadFile(downloadWorkbook.href, downloadWorkbook.download, message);
});

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
  ++returnGeneration;
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
    const item = document.createElement("div");
    item.className = "download-item";
    const name = document.createElement("span");
    name.textContent = file.filename;
    const link = document.createElement("a");
    link.href = file.downloadUrl;
    link.download = file.filename;
    link.className = "download-link";
    link.textContent = "PDF indir";
    link.setAttribute("aria-label", `${file.filename} PDF indir`);
    link.addEventListener("click", (event) => {
      event.preventDefault();
      downloadFile(file.downloadUrl, file.filename, returnMessage);
    });
    item.append(name, link);
    responseDownloads.append(item);
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
  const td = document.createElement("div");
  const label = document.createElement("label");
  label.textContent = fieldLabels[field];
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
  input.id = `edit-${item.id}-${field}`;
  label.htmlFor = input.id;
  input.addEventListener("input", () => {
    markDirty(item.id);
    const summary = tbody.querySelector(`tr[data-row-id="${CSS.escape(item.id)}"] [data-display="${field}"]`);
    if (summary) summary.textContent = input.value || "—";
  });
  td.append(label, input);
  return td;
}

function pairedCell(primary, secondary, primaryField = "", secondaryField = "") {
  const td = document.createElement("td");
  const title = document.createElement("strong");
  title.textContent = primary || "—";
  title.dataset.display = primaryField;
  const subtitle = document.createElement("span");
  subtitle.className = "cell-secondary";
  subtitle.textContent = secondary || "—";
  subtitle.dataset.display = secondaryField;
  td.append(title, subtitle);
  return td;
}

function readOnlyDetail(label, value) {
  const div = document.createElement("div");
  const title = document.createElement("strong");
  title.textContent = label;
  const text = document.createElement("p");
  text.textContent = value || "—";
  div.append(title, text);
  return div;
}

function rowDetails(item, editable) {
  const detail = document.createElement("tr");
  detail.className = "detail-row";
  detail.id = `${editable ? "review" : "return"}-detail-${item.id}`;
  detail.hidden = true;
  const td = document.createElement("td");
  td.colSpan = 7;
  const fields = document.createElement("div");
  fields.className = "detail-grid";
  if (editable) {
    for (const [field, type] of editableFields) fields.append(editableCell(item, field, type));
    fields.append(readOnlyDetail("Muhatap", item.cells.recipient));
  } else {
    for (const [label, field] of [["Alacaklı", "creditor"], ["Muhatap", "recipient"], ["Muhasebe hücresi", "accountingInput"]]) {
      fields.append(readOnlyDetail(label, item[field]));
    }
  }
  const issues = document.createElement("div");
  issues.className = "issues";
  for (const [key, title, className] of [["warnings", "Uyarı", "warning"], ["blockers", "Blokaj", "blocker"]]) {
    for (const text of item[key] || []) {
      const p = document.createElement("p");
      p.className = className;
      p.textContent = `${title}: ${text}`;
      issues.append(p);
    }
  }
  if (!issues.childElementCount) issues.textContent = "Uyarı veya blokaj bulunmuyor.";
  td.append(fields, issues);
  detail.append(td);
  const button = document.createElement("button");
  button.type = "button";
  button.className = "small-action secondary";
  button.textContent = editable ? "Düzenle" : "Ayrıntılar";
  button.setAttribute("aria-label", `${button.textContent}: ${item.cells?.debtor || item.debtor || "Dosya"}`);
  button.setAttribute("aria-expanded", "false");
  button.setAttribute("aria-controls", detail.id);
  button.addEventListener("click", () => {
    detail.hidden = !detail.hidden;
    button.setAttribute("aria-expanded", String(!detail.hidden));
  });
  return { detail, button };
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
  exportHamdataButton.disabled = !hasBatch || hasDirty || workbookBusy;
  exportHamdataAccountingButton.disabled = !hasBatch || hasDirty || workbookBusy;
  reloadReturnButton.disabled = !currentReturnId || returnBusy || responsesBusy;
  selectValidResponsesButton.disabled = !currentReturnId || !hasValidResponse || returnBusy || responsesBusy;
  previewSelectedResponseButton.disabled = !currentReturnId || selectedResponses.length !== 1 || returnBusy || responsesBusy;
  generateResponsesButton.disabled = !currentReturnId || selectedResponses.length === 0 || !hasProfile || responsesBusy || returnBusy;
  returnForm.querySelector("button").disabled = returnBusy || responsesBusy;
  for (const item of currentReturnRows) {
    const row = returnRows.querySelector(`tr[data-row-id="${CSS.escape(item.id)}"]`);
    if (!row) continue;
    for (const action of row.querySelectorAll("input, [data-role=preview-response]")) {
      action.disabled = item.status === "blocked" || returnBusy || responsesBusy;
    }
  }
  document.querySelector("#responseSelectionStatus").textContent = selectedResponses.length
    ? `${selectedResponses.length} dosya seçili.${hasProfile ? " PDF oluşturmaya hazır." : " Vekil adı ve adresini girin."}`
    : "PDF oluşturmak için uygun satırları seçin ve vekil bilgilerini girin.";
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
    td.colSpan = 7;
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
    selector.setAttribute("aria-label", `${item.cells.debtor || "Dosya"} satırını seç`);
    selector.addEventListener("change", updateButtons);
    const selectCell = document.createElement("td");
    selectCell.append(selector);
    const status = cell(labels[item.status] || item.status);
    status.className = "badge";
    const approval = document.createElement("div");
    approval.className = "approval";
    approval.textContent = item.approved ? "Onaylı" : "Onay bekliyor";
    status.append(approval);
    const { detail, button } = rowDetails(item, true);
    const action = document.createElement("td");
    action.append(button);
    const amount = cell(item.cells.amount);
    const date = cell(item.cells.serviceDate);
    amount.dataset.display = "amount";
    date.dataset.display = "serviceDate";
    row.append(
      selectCell,
      status,
      pairedCell(item.cells.debtor, item.cells.debtorId, "debtor", "debtorId"),
      pairedCell(item.cells.caseNumber, item.cells.office, "caseNumber", "office"),
      amount, date, action
    );
    tbody.append(row, detail);
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
    td.colSpan = 7;
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
    selector.setAttribute("aria-label", `${item.debtor || "Dosya"} cevabını seç`);
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
    const { detail, button } = rowDetails(item, false);
    const previewCell = document.createElement("td");
    const actions = document.createElement("div");
    actions.className = "row-actions";
    actions.append(preview, button);
    previewCell.append(actions);
    const status = cell(labels[item.status] || item.status);
    status.className = "badge";
    const decisionText = item.decision === "var" || item.decision === "yok" ? item.decision.toUpperCase() : "—";
    const decision = cell(decisionText);
    decision.className = item.decision === "var" ? "decision-var" : item.decision === "yok" ? "decision-yok" : "decision-blocked";
    row.append(
      selectCell,
      status,
      pairedCell(item.debtor, item.debtorId),
      pairedCell(item.caseNumber, item.office),
      decision,
      cell(item.amount?.text || item.accountingInput || ""),
      previewCell
    );
    returnRows.append(row, detail);
  }
  updateButtons();
}

async function loadReturn() {
  if (!currentReturnId || returnBusy || responsesBusy) return;
  const generation = returnGeneration;
  returnMessage.textContent = "Muhasebe dönüşü yenileniyor...";
  const response = await fetch(`/api/accounting-returns/${encodeURIComponent(currentReturnId)}`);
  const payload = await response.json();
  if (generation !== returnGeneration) return;
  if (!response.ok) throw new Error(payload.error?.message || "Muhasebe dönüşü alınamadı.");
  setReturnSummary(payload.summary);
  renderReturnRows(payload.rows || []);
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = "Muhasebe dönüş listesi güncel.";
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
  if (!currentBatchId || workbookBusy) return;
  if (dirtyRows.size > 0) throw new Error("Excel oluşturmadan önce değişiklikleri kaydedin.");
  workbookBusy = true;
  updateButtons();
  clearDownload();
  message.textContent = "Excel hazırlanıyor...";
  try {
    const filename = kind === "hamdata" ? "HAMDATA.xlsx" : "HAMDATA-MUHASEBE.xlsx";
    const handle = await chooseSaveFile(filename);
    const payload = await postJson(`/api/batches/${encodeURIComponent(currentBatchId)}/exports/${kind}`, {}, 201);
    showDownload(payload);
    await saveDownload(payload.downloadUrl, payload.filename, handle);
    message.textContent = handle ? `${payload.filename} kaydedildi.`
      : `${payload.filename} tarayıcı indirmelerine gönderildi. Gerekirse “Excel'i tekrar indir” bağlantısını kullanın.`;
  } catch (error) {
    if (error.name !== "AbortError") throw error;
    message.textContent = "Kaydetme iptal edildi.";
  } finally {
    workbookBusy = false;
    updateButtons();
  }
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
    message.textContent = `${payload.batch.importedCount} belge işlendi. Bilgileri inceleyip Excel dosyanızı oluşturabilirsiniz.`;
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
  if (returnBusy || responsesBusy) return;
  const [file] = returnFileInput.files;
  if (!file) return;
  returnBusy = true;
  const generation = ++returnGeneration;
  updateButtons();
  clearResponseDownloads();
  clearResponsePreview();
  returnMessage.textContent = "Muhasebe dönüşü işleniyor...";
  try {
    const body = new FormData();
    body.append("file", file);
    const response = await fetch("/api/accounting-returns", { method: "POST", body });
    const payload = await response.json();
    if (generation !== returnGeneration) return;
    if (!response.ok) throw new Error(payload.error?.message || "Muhasebe dönüşü içe aktarılamadı.");
    currentReturnId = payload.return.id;
    setReturnSummary(payload.summary);
    renderReturnRows(payload.rows || []);
    clearResponseDownloads();
    clearResponsePreview();
    returnMessage.textContent = `${payload.summary.total} satır incelendi. PDF oluşturmak için uygun satırları seçin.`;
    updateButtons();
  } catch (error) {
    if (generation === returnGeneration) returnMessage.textContent = error.message;
  } finally {
    returnBusy = false;
    updateButtons();
  }
});

reloadReturnButton.addEventListener("click", async () => {
  const generation = returnGeneration;
  try {
    await loadReturn();
  } catch (error) {
    if (generation === returnGeneration) returnMessage.textContent = error.message;
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
  if (returnBusy || responsesBusy) return;
  const generation = returnGeneration;
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
    if (generation !== returnGeneration) return;
    responsePreviewFrame.srcdoc = payload.html || "";
    responsePreviewStatus.textContent = "Önizleme güncel.";
  } catch (error) {
    if (currentPreviewRowId !== rowId) return;
    if (generation !== returnGeneration) return;
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
  if (returnBusy || responsesBusy) return;
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
  if (generateResponsesButton.disabled) return;
  responsesBusy = true;
  const generation = returnGeneration;
  updateButtons();
  returnMessage.textContent = "Cevap PDF'leri hazırlanıyor...";
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
    if (generation !== returnGeneration) return;
    showResponseDownloads(payload);
    responseDownloadPanel.scrollIntoView({ block: "nearest" });
    returnMessage.textContent = `${(payload.files || []).length} PDF hazır. Aşağıdaki “PDF indir” düğmeleriyle kaydedin.`;
  } catch (error) {
    if (generation === returnGeneration) returnMessage.textContent = error.message;
  } finally {
    responsesBusy = false;
    updateButtons();
  }
});
