#include <QApplication>
#include <QPalette>
#include <QColor>
#include "main_window.h"

// Светлая тема: мягкий белый фон, тёмный (не чисто-чёрный) текст, спокойный синий акцент.
// Контраст подобран так, чтобы не резать глаза: фон не кипенно-белый, текст не абсолютно-чёрный.
static const char *kStyle = R"QSS(
* { font-family: "Segoe UI", "Tahoma", sans-serif; font-size: 13px; }
QMainWindow { background: #eef1f6; }
QWidget { color: #1f2937; }
QToolTip { background: #ffffff; color: #1f2937; border: 1px solid #d6dce6; padding: 4px; border-radius: 4px; }

QTabWidget { background: #eef1f6; }
QTabWidget::pane { border: 1px solid #dbe1ea; border-radius: 10px; background: #ffffff; top: -1px; }
QTabBar { background: #eef1f6; }
QTabBar::tab {
    background: #cfd4dc; color: #475569; padding: 10px 20px;
    border: 1px solid #c2c8d2; border-bottom: none;
    border-top-left-radius: 9px; border-top-right-radius: 9px;
    margin-right: 4px; font-weight: 600;
}
QTabBar::tab:selected { background: #ffffff; color: #2563eb; border-color: #c3d4f5; }
QTabBar::tab:hover { background: #dde2e9; color: #1f2937; }
QScrollArea { border: none; background: #eef1f6; }
QScrollArea > QWidget > QWidget { background: #eef1f6; }

QGroupBox {
    border: 1px solid #e2e8f0; border-radius: 12px; margin-top: 16px;
    padding: 16px 14px 14px 14px; background: #ffffff; font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 14px; padding: 2px 8px;
    color: #2563eb; font-weight: 700; background: #ffffff; border-radius: 6px;
}

QPushButton {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:1 #eef2f8);
    color: #1f2937; border: 1px solid #c9d3e0; border-radius: 7px; padding: 7px 16px; font-weight: 600;
}
QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #f5f9ff, stop:1 #e6efff); border-color: #2563eb; color: #1d4ed8; }
QPushButton:pressed { background: #dbe7fb; }
QPushButton:disabled { background: #f1f4f8; color: #aab3c0; border-color: #e2e8f0; }

QLineEdit, QTextEdit, QComboBox, QSpinBox, QPlainTextEdit {
    background: #ffffff; color: #1f2937; border: 1px solid #d6dce6; border-radius: 7px;
    padding: 6px; selection-background-color: #cdd8ea; selection-color: #14213d;
}
QListWidget, QTableWidget, QTreeWidget, QTreeView {
    background: #e6eaf1; color: #1f2937; border: 1px solid #cfd6e0; border-radius: 7px;
    padding: 4px; selection-background-color: #cdd8ea; selection-color: #14213d;
}
QLineEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus, QTreeWidget:focus, QTreeView:focus { border: 1px solid #2563eb; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #ffffff; border: 1px solid #d6dce6; selection-background-color: #cfe0fb; }
QHeaderView::section {
    background: #dfe4ec; color: #475569; padding: 6px; border: 1px solid #cfd6e0; font-weight: 700;
}
QTableWidget { gridline-color: #d8dee8; }
QListWidget::item { padding: 5px; }
QListWidget::item:selected { background: #cdd8ea; color: #14213d; border-radius: 4px; }
QTreeWidget { alternate-background-color: #dde3ec; }
QTreeWidget::item { padding: 4px; border: none; }
QTreeWidget::item:hover { background: #d9e0ea; }
QTreeWidget::item:selected { background: #cdd8ea; color: #14213d; }
QTreeWidget::branch { background: #e6eaf1; border: none; }

QMenuBar { background: #f7fafd; color: #1f2937; }
QMenuBar::item:selected { background: #dbe7fb; color: #1d4ed8; }
QMenu { background: #ffffff; color: #1f2937; border: 1px solid #dbe1ea; }
QMenu::item:selected { background: #dbe7fb; color: #1d4ed8; }
QToolBar { background: #f7fafd; border-bottom: 1px solid #e2e8f0; spacing: 4px; padding: 4px; }
QDockWidget { titlebar-close-icon: none; background: #eef1f6; }
QDockWidget::title { background: #dfe4ec; padding: 6px; color: #2563eb; font-weight: 700; }
QMainWindow::separator { background: #d6dce6; width: 6px; height: 6px; }
QSplitter { background: #eef1f6; }
QSplitter::handle { background: #d6dce6; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QStatusBar { background: #f7fafd; color: #64748b; }
QLabel { color: #374151; }

/* Ползунки в стиле обычной Windows */
QScrollBar:vertical { background: #f0f0f0; width: 17px; margin: 0px; border: none; }
QScrollBar:horizontal { background: #f0f0f0; height: 17px; margin: 0px; border: none; }
QScrollBar::handle:vertical { background: #cdcdcd; border: 1px solid #8a8a8a; min-height: 24px; }
QScrollBar::handle:horizontal { background: #cdcdcd; border: 1px solid #8a8a8a; min-width: 24px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #a9c4e8; }
QScrollBar::add-line, QScrollBar::sub-line { background: #f0f0f0; border: 1px solid #8a8a8a; }
QScrollBar::add-line:vertical { height: 16px; subcontrol-position: bottom; subcontrol-origin: margin; }
QScrollBar::sub-line:vertical { height: 16px; subcontrol-position: top; subcontrol-origin: margin; }
QScrollBar::add-line:horizontal { width: 16px; subcontrol-position: right; subcontrol-origin: margin; }
QScrollBar::sub-line:horizontal { width: 16px; subcontrol-position: left; subcontrol-origin: margin; }
QScrollBar::add-line:hover, QScrollBar::sub-line:hover { background: #d8e6f6; }
QScrollBar::up-arrow:vertical { width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid #4a4a4a; }
QScrollBar::down-arrow:vertical { width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #4a4a4a; }
QScrollBar::left-arrow:horizontal { width: 0; height: 0; border-top: 4px solid transparent; border-bottom: 4px solid transparent; border-right: 5px solid #4a4a4a; }
QScrollBar::right-arrow:horizontal { width: 0; height: 0; border-top: 4px solid transparent; border-bottom: 4px solid transparent; border-left: 5px solid #4a4a4a; }
QScrollBar::add-page, QScrollBar::sub-page { background: #f0f0f0; }
QScrollBar:vertical:disabled, QScrollBar:horizontal:disabled { background: #e6e6e6; }
)QSS";

int main(int argc,char **argv){
    QApplication app(argc,argv);
    app.setApplicationName("WoW DB Studio");
    // Светлая палитра по умолчанию. Если Windows в тёмной теме, стандартная палитра Qt тёмная,
    // и области, не покрытые QSS (например полоса справа от последней вкладки), рисуются чёрным.
    // Явно задаём светлые базовые цвета, чтобы чёрного не было нигде.
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window,          QColor(0xee,0xf1,0xf6));
    pal.setColor(QPalette::WindowText,      QColor(0x1f,0x29,0x37));
    pal.setColor(QPalette::Base,            QColor(0xff,0xff,0xff));
    pal.setColor(QPalette::AlternateBase,   QColor(0xe6,0xea,0xf1));
    pal.setColor(QPalette::Text,            QColor(0x1f,0x29,0x37));
    pal.setColor(QPalette::Button,          QColor(0xf3,0xf5,0xf8));
    pal.setColor(QPalette::ButtonText,      QColor(0x1f,0x29,0x37));
    pal.setColor(QPalette::Highlight,       QColor(0x25,0x63,0xeb));
    pal.setColor(QPalette::HighlightedText, QColor(0xff,0xff,0xff));
    pal.setColor(QPalette::ToolTipBase,     QColor(0xff,0xff,0xff));
    pal.setColor(QPalette::ToolTipText,     QColor(0x1f,0x29,0x37));
    pal.setColor(QPalette::Link,            QColor(0x25,0x63,0xeb));
    app.setPalette(pal);
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QString::fromLatin1(kStyle));
    MainWindow w; w.show(); return app.exec();
}
