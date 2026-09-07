// PDFium's stable public C API, isolated in a single-threaded native process.
// ABI: https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_text.h
#include <QCoreApplication>
#include <QLibrary>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
template<class T> T symbol(QLibrary& lib, const char* name) {
    const auto result = reinterpret_cast<T>(lib.resolve(name));
    if (!result) throw std::runtime_error("pdfium_api_unavailable");
    return result;
}
template<class T> struct Handle {
    T pointer;
    void (*close)(T);
    ~Handle() { if (pointer) close(pointer); }
};
QString read_pdf(const QByteArray& bytes) {
    QLibrary lib(QCoreApplication::applicationDirPath() + "/pdfium");
    if (!lib.load()) throw std::runtime_error("pdfium_unavailable");
    const auto init = symbol<void (*)()>(lib, "FPDF_InitLibrary");
    const auto destroy = symbol<void (*)()>(lib, "FPDF_DestroyLibrary");
    const auto load = symbol<void* (*)(const void*, size_t, const char*)>(lib, "FPDF_LoadMemDocument64");
    const auto close = symbol<void (*)(void*)>(lib, "FPDF_CloseDocument");
    const auto page_count = symbol<int (*)(void*)>(lib, "FPDF_GetPageCount");
    const auto page_load = symbol<void* (*)(void*, int)>(lib, "FPDF_LoadPage");
    const auto page_close = symbol<void (*)(void*)>(lib, "FPDF_ClosePage");
    const auto text_load = symbol<void* (*)(void*)>(lib, "FPDFText_LoadPage");
    const auto text_close = symbol<void (*)(void*)>(lib, "FPDFText_ClosePage");
    const auto count_chars = symbol<int (*)(void*)>(lib, "FPDFText_CountChars");
    const auto get_text = symbol<int (*)(void*, int, int, unsigned short*)>(lib, "FPDFText_GetText");
    init();
    struct LibraryGuard { void (*destroy)(); ~LibraryGuard() { destroy(); } } library_guard{destroy};
    Handle<void*> doc{load(bytes.constData(), static_cast<size_t>(bytes.size()), nullptr), close};
    if (!doc.pointer) throw std::runtime_error("pdf_invalid_or_encrypted");
    const auto pages = page_count(doc.pointer);
    if (pages <= 0 || pages > 200) throw std::runtime_error("pdf_page_limit");
    QString result;
    for (int page = 0; page < pages; ++page) {
        Handle<void*> p{page_load(doc.pointer, page), page_close};
        if (!p.pointer) throw std::runtime_error("pdf_page_unreadable");
        Handle<void*> text{text_load(p.pointer), text_close};
        if (!text.pointer) throw std::runtime_error("pdf_text_unreadable");
        const auto chars = count_chars(text.pointer);
        if (chars < 0 || chars > 200000 || result.size() + chars > 2000000) throw std::runtime_error("pdf_text_limit");
        std::vector<unsigned short> buffer(static_cast<size_t>(chars) + 1);
        const auto written = get_text(text.pointer, 0, chars, buffer.data());
        if (written > 0) result += QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), written - 1);
        result += '\n';
    }
    return result;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin),_O_BINARY);
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 1;
    QJsonObject response;
    try {
        QByteArray bytes;
        while (true) {
            const auto chunk=input.read(64*1024);
            if(chunk.isEmpty()) break;
            bytes+=chunk;
            if(bytes.size()>64*1024*1024) throw std::runtime_error("pdf_size_limit");
        }
        const auto text = read_pdf(bytes);
        response["text"] = text;
        if (text.trimmed().isEmpty()) response["error"] = "OCR gerekli / metin içeriği yok";
    } catch (const std::exception& error) { response["error"] = QString::fromUtf8(error.what()); }
    const auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    return output.write(bytes) == bytes.size() && output.flush() ? 0 : 1;
}
