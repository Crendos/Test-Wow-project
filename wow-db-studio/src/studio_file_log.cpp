#include "studio_file_log.h"
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

QString StudioFileLog::dir() {
    QString root = QCoreApplication::applicationDirPath();
    if (root.isEmpty())
        root = QDir::currentPath();
    QDir d(root);
    if (!d.exists(QStringLiteral("logs")))
        d.mkpath(QStringLiteral("logs"));
    return d.filePath(QStringLiteral("logs"));
}

QString StudioFileLog::append(const QString &category, const QString &text) {
    if (text.trimmed().isEmpty())
        return {};
    QString cat = category.trimmed().toLower();
    cat.replace(QLatin1Char(' '), QLatin1Char('-'));
    cat.remove(QRegularExpression(QStringLiteral("[^a-z0-9._-]")));
    if (cat.isEmpty())
        cat = QStringLiteral("studio");
    const QString path = dir() + QLatin1Char('/') + cat + QLatin1Char('-')
        + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return {};
    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    if (!text.contains(QLatin1Char('[')))
        out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << ' ';
    out << text;
    if (!text.endsWith(QLatin1Char('\n')))
        out << '\n';
    return path;
}
