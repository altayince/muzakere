#include <catch2/catch_session.hpp>
#include <QApplication>
#include <QFontDatabase>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    for(const auto* font:{"times.ttf","timesbd.ttf","timesi.ttf","timesbi.ttf","arial.ttf"})
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/"+font);
#endif
    return Catch::Session().run(argc, argv);
}
