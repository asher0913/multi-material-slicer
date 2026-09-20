#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <Qt>

namespace {

// A dark palette so any control the stylesheet doesn't fully cover (native menus,
// dialogs, tooltips) still matches the theme instead of flashing white.
QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x1b, 0x1e, 0x24);
    const QColor base(0x2d, 0x32, 0x3e);
    const QColor text(0xe8, 0xea, 0xed);
    const QColor accent(0x2c, 0xa0, 0xdc);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x24, 0x28, 0x33));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x2d, 0x32, 0x3e));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5e, 0x66, 0x75));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5e, 0x66, 0x75));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5e, 0x66, 0x75));
    return p;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MultiMaterial"));
    QApplication::setApplicationName(QStringLiteral("MultiMaterialSlicer"));

    // Fusion gives a consistent, stylesheet-friendly base across macOS / Windows.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(darkPalette());

    QFile styleFile(QStringLiteral(":/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    MainWindow window;
    window.show();
    return app.exec();
}
