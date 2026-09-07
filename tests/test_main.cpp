#include <catch2/catch_session.hpp>
#include <QApplication>
#include <QFontDatabase>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf");
#endif
    return Catch::Session().run(argc, argv);
}
