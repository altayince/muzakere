#include "accounting_adapters.hpp"
#include <QLocale>
#include <QRegularExpression>
#include <QFileInfo>
#include <QSet>
#include <QMap>
#include <algorithm>

namespace muz {
namespace {
QString upper(QString value) {
    // Qt builds without ICU may ignore locale-specific casing.
    value.replace(QChar('i'),QChar(0x0130)); value.replace(QChar(0x0131),QChar('I'));
    return value.toUpper().normalized(QString::NormalizationForm_C);
}
QString capture(const QString& text, const QString& pattern, int group = 1) {
    return QRegularExpression(pattern, QRegularExpression::DotMatchesEverythingOption).match(text).captured(group).simplified();
}
QString directory(const IncomingDocument& doc) {
    auto path = QString::fromStdString(doc.source_path).replace('\\', '/');
    return path.left(path.lastIndexOf('/'));
}
QString recipient_key(const std::string& value) {
    auto result=upper(QString::fromStdString(value));
    result.replace("ANONİM ŞİRKETİ","AŞ");result.replace("LİMİTED ŞİRKETİ","LTDŞTİ");
    result.remove(QRegularExpression(R"([^\p{L}\p{N}])"));return result;
}
void warn(AccountingRow& row, const QString& message) {
    const auto value = message.toStdString();
    if (std::find(row.warnings.begin(), row.warnings.end(), value) == row.warnings.end()) row.warnings.push_back(value);
}
void field(AccountingRow& row, Column column, const QString& value, const QString& snippet = {}) {
    row.cells[column] = value.trimmed().toStdString();
    if (!value.isEmpty()) row.evidence.push_back({column, row.document_id,
        (snippet.isEmpty() ? value : snippet).left(2000).toStdString(), "uyap-labels-v1", 0.95});
}
bool valid_iban(const QString& iban) {
    if (!QRegularExpression("^TR[0-9]{24}$").match(iban).hasMatch()) return false;
    const auto digits = iban.mid(4) + "2927" + iban.mid(2, 2);
    int remainder = 0;
    for (const auto digit : digits) remainder = (remainder * 10 + digit.digitValue()) % 97;
    return remainder == 1;
}
} // namespace

ParsedDocument parse_document(const IncomingDocument& document, const PdfText& pdf) {
    ParsedDocument parsed;
    parsed.document = document; parsed.text = pdf;
    auto& row = parsed.row;
    row.id = document.id; row.document_id = document.id; row.batch_id = document.batch_id;
    row.source_path = document.source_path; row.sha256 = document.file.sha256;
    row.source_text = pdf.text.toStdString();
    const auto lines = upper(pdf.text);
    const auto text = lines.simplified();
    parsed.envelope = text.contains("TEBLİĞ MAZBATASI") ||
        (text.contains("TEBLİĞ EVRAKI") && text.contains("BU ZARFTA"));
    if (!pdf.error.empty()) warn(row, QString::fromStdString(pdf.error));
    if (text.isEmpty()) {
        row.cells[first_notice] = "Belirsiz";
        warn(row, "OCR gerekli / PDF metni okunamadı");
        return parsed;
    }
    QSet<QString> cases;
    const QRegularExpression case_pattern(R"((\d{4})\s*/\s*(\d+)\s*(?:ESAS|İCRA))");
    auto matches = case_pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next(); cases.insert(match.captured(1) + '/' + match.captured(2));
    }
    if (cases.size() == 1) field(row, case_number, *cases.begin());
    else warn(row, cases.isEmpty() ? "Esas numarası okunamadı" : "Belgede birden fazla esas numarası var");
    if (parsed.envelope) {
        const auto line = lines.section('\n', 0, 0).simplified();
        const auto city = capture(lines, R"(\n([^\n]{2,60})\n\s*T\.C\.)");
        if (!city.isEmpty() && line.contains("İCRA")) field(row, office, city + ' ' + line);
        row.recipient=capture(text,R"(T\.C\.\s*(.{1,300}?)\s*\[[0-9]{5}-[0-9]{5}-[0-9]{5}\])").toStdString();
        return parsed;
    }
    row.recipient=capture(text,R"(1\.\s*ÜÇÜNCÜ ŞAHSIN.*?:\s*(.*?)\s*2\.\s*ALACAKLININ)").toStdString();
    if(row.recipient.empty())row.recipient=capture(text,R"((?:ÜÇÜNCÜ ŞAHSIN ADI|3\.ŞAHSIN ADI)\s*:\s*(.*?)\s*ALACAKLI\s*:)").toStdString();
    auto office_text = capture(text, R"(T\.C\.\s*(.{1,180}?)\s*\d{4}\s*/\s*\d+\s*ESAS)");
    office_text.remove(QRegularExpression(R"(\b\d{2}[/.-]\d{2}[/.-]\d{4}\b)"));
    if (office_text.contains("İCRA") && (office_text.contains("DAİRESİ") || office_text.contains("MÜDÜRLÜĞÜ")))
        field(row, office, office_text.simplified());
    else warn(row, "İcra dairesi okunamadı");

    auto creditor_block = capture(text, R"(2\.\s*ALACAKLININ.*?:\s*(.*?)\s*3\.\s*BORÇLUNUN)");
    if (creditor_block.isEmpty()) creditor_block = capture(text, R"(\bALACAKLI\s*:\s*(.*?)(?=VEKİLİ\s*:|BORÇLU\s*:))");
    auto creditor_name = creditor_block.section(QRegularExpression(R"((?:VEKİLİ(?:\s|:|$)|AV\.))"), 0, 0);
    creditor_name.remove(QRegularExpression(R"(,\s*\d{10,11}\s*(?:VERGİ|TC).*?$)"));
    field(row, creditor, creditor_name.simplified(), creditor_block);
    if (creditor_name.trimmed().isEmpty()) warn(row, "Alacaklı okunamadı");

    auto debtor_block = capture(text, R"(3\.\s*BORÇLUNUN.*?:\s*(.*?)\s*4\.\s*HACZİN)");
    if (debtor_block.isEmpty()) debtor_block = capture(text, R"(\bBORÇLU\s*:\s*(.*?)(?=BORÇ MİKTARI|YUKARIDA|İCRA MÜDÜR))");
    QStringList names, identifiers;
    const QRegularExpression party(R"((?:^|\s\d+\s*-\s*)([^,]+),\s*(\d{10,11})\s*(?:TC|T\.C\.|VERGİ))");
    auto parties = party.globalMatch(debtor_block);
    while (parties.hasNext()) {
        const auto match = parties.next();
        auto name = match.captured(1).simplified();
        name.remove(QRegularExpression(R"(^\d+\s*-\s*)"));
        names.append(name); identifiers.append(match.captured(2));
    }
    if (names.isEmpty() && debtor_block.contains(',')) {
        names.append(debtor_block.section(',', 0, 0).trimmed());
        warn(row, "Borçlu TCKN/VKN okunamadı");
    }
    field(row, debtor, names.join("; "), debtor_block);
    field(row, debtor_id, identifiers.join("; "), debtor_block);
    if (names.isEmpty()) warn(row, "Borçlu bilgileri okunamadı");
    if (names.size() > 1) warn(row, "Birden fazla borçlu var; tutar borçlulara bölünmedi");

    QSet<QString> amounts;
    const QRegularExpression money(R"((?:ALACAK TUTARI İLE FAİZ VE GİDERLER|BORÇ MİKTARI)\s*:\s*([0-9][0-9.]*,[0-9]{2})\s*TL)");
    auto money_matches = money.globalMatch(text);
    while (money_matches.hasNext()) amounts.insert(money_matches.next().captured(1));
    if (amounts.size() == 1 && parse_money(amounts.begin()->toStdString())) field(row, amount, *amounts.begin());
    else warn(row, amounts.size() > 1 ? "Birden fazla borç tutarı var" : "Borç tutarı belgede yok veya okunamadı");

    QSet<QString> ibans;
    const QRegularExpression iban_pattern(R"(\bTR(?:\s*[0-9]){24}\b)");
    auto iban_matches = iban_pattern.globalMatch(text);
    while (iban_matches.hasNext()) {
        auto value = iban_matches.next().captured(); value.remove(' '); ibans.insert(value);
    }
    if (ibans.size() == 1) {
        field(row, iban, *ibans.begin());
        if (!valid_iban(*ibans.begin())) warn(row, "IBAN kontrol basamakları doğrulanamadı");
    } else warn(row, ibans.isEmpty() ? "İcra dairesi IBAN bilgisi belgede yok" : "Birden fazla IBAN var");

    const auto heading = text.left(1700);
    if (heading.contains("BİRİNCİ HACİZ İHBARNAMESİ")) {
        field(row, first_notice, "Evet", "BİRİNCİ HACİZ İHBARNAMESİ");
        row.cells[notes] = "Birinci haciz ihbarnamesi";
    } else if (heading.contains("ADRES ARAŞTIRMA YAZISI")) {
        field(row, first_notice, "Hayır", "ADRES ARAŞTIRMA YAZISI"); row.cells[notes] = "Adres araştırma yazısı";
    } else if ((text.contains("KONULAN HACZİN") && (text.contains("KALDIRILMASINA") || text.contains("KALDIRILDIĞINDAN"))) ||
               (text.contains("HACZİN") && (text.contains("FEKKİNE KARAR") || text.contains("HACZİN KALDIRILMASINI")))) {
        field(row, first_notice, "Hayır", "HACZİN KALDIRILMASI"); row.cells[notes] = "Haciz kaldırma / fek yazısı";
    } else if (heading.contains("İKİNCİ HACİZ İHBARNAMESİ") || heading.contains("ÜÇÜNCÜ HACİZ İHBARNAMESİ")) {
        field(row, first_notice, "Hayır", heading.left(300)); row.cells[notes] = "İkinci/üçüncü haciz ihbarnamesi";
    } else {
        row.cells[first_notice] = "Belirsiz"; warn(row, "Belge türü manuel incelenmeli");
    }
    if (QRegularExpression(R"(FAİZ.{0,20}MASRAF.{0,15}HARİÇ)").match(text).hasMatch())
        row.cells[notes] += "; Faiz ve masraflar hariç";
    // A date in a filename, header or archive is never silently a service date.
    const auto date = capture(text, R"(TEBLİĞ TARİHİ\s*:\s*([0-9]{2}[./][0-9]{2}[./][0-9]{4}))");
    if (!date.isEmpty()) field(row, service_date, QString(date).replace('/', '.'));
    else warn(row, "Tebliğ tarihi belgede yok; kullanıcı girişi gerekli");
    return parsed;
}

std::vector<AccountingRow> match_accounting(std::vector<ParsedDocument> documents) {
    std::vector<AccountingRow> rows;
    QMap<QString, int> case_counts;
    for (const auto& parsed : documents) {
        if (!parsed.envelope && !parsed.row.cells[case_number].empty())
            ++case_counts[QString::fromStdString(parsed.row.cells[office] + "|" + parsed.row.cells[case_number])];
    }
    for (auto& parsed : documents) {
        if (parsed.envelope) continue;
        auto& row = parsed.row;
        std::vector<const ParsedDocument*> candidates;
        bool folder_conflict = false;
        for (const auto& other : documents) {
            if (!other.envelope) continue;
            if (directory(other.document) == directory(parsed.document)) {
                if (!row.cells[case_number].empty() && row.cells[case_number] == other.row.cells[case_number]) {
                    if (!other.row.cells[office].empty() && other.row.cells[office] != row.cells[office]) folder_conflict = true;
                    else if (!other.row.recipient.empty() && !row.recipient.empty() &&
                        !recipient_key(row.recipient).contains(recipient_key(other.row.recipient))) {
                        folder_conflict=true;
                        warn(row,"Zarf ile evrak muhatapları çelişiyor; onaylı çıktıya alınamaz");
                        warn(row,"Zarf muhatabı: "+QString::fromStdString(other.row.recipient));
                        row.source_text+="\n\n--- ÇELİŞKİLİ ZARF (EŞLEŞTİRİLMEDİ) ---\n"+other.text.text.toStdString();
                    }
                    else candidates.push_back(&other);
                } else folder_conflict = true;
            }
        }
        if (candidates.size() == 1 && !folder_conflict) {
            row.envelope_id = candidates.front()->document.id;
            row.match_confidence = candidates.front()->row.cells[office].empty() ? 0.85 : 0.95;
            row.source_text += "\n\n--- ÖNERİLEN ZARF ---\n" + candidates.front()->text.text.toStdString();
            if(!candidates.front()->row.recipient.empty()) {
                if(row.recipient.empty())warn(row,"Evrakta muhatap alanı ayrı doğrulanamadı; kaynak metni inceleyin");
                row.recipient=candidates.front()->row.recipient;
            }
        } else {
            row.pair_conflict=folder_conflict || candidates.size()>1;
            warn(row, folder_conflict ? "Zarf ile evrak bilgileri çelişiyor; manuel eşleştirme gerekli" :
                candidates.empty() ? "Eşleşen zarf bulunamadı" : "Birden fazla olası zarf var");
        }
        if (case_counts.value(QString::fromStdString(row.cells[office] + "|" + row.cells[case_number])) > 1)
            warn(row, "Aynı esas numarası tekrar ediyor; belge ve muhatapları ayrı inceleyin");
        rows.push_back(std::move(row));
    }
    for (auto& parsed:documents) {
        if (!parsed.envelope) continue;
        const bool used=std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.envelope_id==parsed.document.id;});
        if(!used) {
            parsed.row.cells[first_notice]="Belirsiz";
            warn(parsed.row,"Eşleşmeyen zarf; asıl evrak eksik veya eşleşme çelişkili");
            rows.push_back(std::move(parsed.row));
        }
    }
    return rows;
}
} // namespace muz
