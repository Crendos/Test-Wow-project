#include "database_service.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QSqlError>
#include <QSqlQuery>

DatabaseService::DatabaseService(QObject *parent) : QObject(parent) {}
DatabaseService::~DatabaseService() { disconnect(); }
QSqlDatabase DatabaseService::database() const { return QSqlDatabase::database(m_connectionName, false); }
bool DatabaseService::isOpen() const { return database().isOpen(); }
void DatabaseService::disconnect() {
    if (!QSqlDatabase::contains(m_connectionName)) return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

static void prependSearchDir(const QString &dir) {
    if (dir.isEmpty() || !QFileInfo::exists(dir)) return;
#ifdef Q_OS_WIN
    const QByteArray native = QDir::toNativeSeparators(dir).toLocal8Bit();
    qputenv("PATH", native + ";" + qgetenv("PATH"));
#else
    Q_UNUSED(dir);
#endif
}

// QMYSQL/QMARIADB listed in drivers() but addDatabase fails = нет libmysql.dll / libmariadb.dll.
static QString ensureMysqlClientLibraries() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList dlls = {
        QStringLiteral("libmysql.dll"),
        QStringLiteral("libmariadb.dll"),
        QStringLiteral("libmysqlclient.dll"),
    };
    const QStringList dirs = {
        app,
        app + QStringLiteral("/sqldrivers"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Server 8.4/bin"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Server 8.4/lib"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Server 8.0/bin"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Server 8.0/lib"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Server 8.0/lib/opt"),
        QStringLiteral("C:/Program Files/MySQL/Connector C++ 8.0/lib64"),
        QStringLiteral("C:/Program Files/MySQL/MySQL Connector C 6.1/lib"),
        QStringLiteral("C:/Program Files/MariaDB 11.4/lib"),
        QStringLiteral("C:/Program Files/MariaDB 11.2/lib"),
        QStringLiteral("C:/Program Files/MariaDB 10.11/lib"),
        qEnvironmentVariable("MYSQL_HOME") + QStringLiteral("/bin"),
        qEnvironmentVariable("MYSQL_HOME") + QStringLiteral("/lib"),
    };
    QStringList tried;
    for (const QString &dir : dirs) {
        if (dir.startsWith(QLatin1Char('/')) || dir.contains(QStringLiteral("//"))) continue;
        prependSearchDir(dir);
        for (const QString &dll : dlls) {
            const QString path = QDir(dir).filePath(dll);
            tried << QDir::toNativeSeparators(path);
            if (!QFileInfo::exists(path)) continue;
            QLibrary lib(path);
            if (lib.load())
                return QStringLiteral("Загружен клиент MySQL: %1").arg(QDir::toNativeSeparators(path));
            tried.last() += QStringLiteral(" (") + lib.errorString() + QStringLiteral(")");
        }
    }
    return QStringLiteral("Клиентская DLL MySQL не загружена. Искали:\n• %1")
        .arg(tried.mid(0, 12).join(QStringLiteral("\n• ")));
}

static QString driverHint(const QStringList &available) {
    return QString::fromUtf8(
        "Qt не загрузил драйвер MySQL (QMYSQL).\n"
        "Доступные драйверы: %1\n\n"
        "Что сделать (Windows, Qt MSVC 64-bit):\n"
        "1) Рядом с WowDbStudio.exe должна быть папка sqldrivers\\ с файлом qsqlmysql.dll\n"
        "   (обычно C:\\Qt\\6.11.2\\msvc2022_64\\plugins\\sqldrivers\\qsqlmysql.dll).\n"
        "2) Рядом с .exe положите libmysql.dll или libmariadb.dll (64-bit) и при необходимости\n"
        "   libssl-3-x64.dll / libcrypto-3-x64.dll.\n"
        "3) Официальный установщик Qt часто НЕ содержит QMYSQL — плагин нужно собрать из исходников Qt\n"
        "   (см. README) либо поставить MySQL/MariaDB ODBC-драйвер: тогда программа подключится через QODBC.\n"
        "4) Разрядность Qt, компилятора, плагина и libmysql.dll должна быть одинаковой: x64.")
        .arg(available.isEmpty() ? QString::fromUtf8("(нет)") : available.join(", "));
}

static bool openMysqlStyle(QSqlDatabase &db, const DbProfile &p, QString *error) {
    db.setHostName(p.host);
    db.setPort(p.port);
    db.setDatabaseName(p.database);
    db.setUserName(p.user);
    db.setPassword(p.password);
    if (db.open()) return true;
    if (error) *error = db.lastError().text();
    return false;
}

bool DatabaseService::connectTo(const DbProfile &p, QString *error) {
    disconnect();
    const QString clientNote = ensureMysqlClientLibraries();
    const QStringList available = QSqlDatabase::drivers();

    const QStringList native = {"QMYSQL", "QMARIADB"};
    QString lastNative;
    for (const auto &name : native) {
        if (!QSqlDatabase::isDriverAvailable(name)) continue;
        QString openError;
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(name, m_connectionName);
            ok = openMysqlStyle(db, p, &openError);
        }
        if (ok) return true;
        lastNative = openError;
        disconnect();
        const bool missingPlugin = openError.contains(QStringLiteral("driver not loaded"), Qt::CaseInsensitive)
            || openError.contains(QStringLiteral("can not load requested driver"), Qt::CaseInsensitive);
        if (!missingPlugin) {
            if (error) *error = openError;
            return false;
        }
    }

    if (QSqlDatabase::isDriverAvailable("QODBC")) {
        const QStringList odbc = {
            "MySQL ODBC 9.4 Unicode Driver", "MySQL ODBC 9.3 Unicode Driver",
            "MySQL ODBC 9.2 Unicode Driver", "MySQL ODBC 9.1 Unicode Driver",
            "MySQL ODBC 8.4 Unicode Driver", "MySQL ODBC 8.3 Unicode Driver",
            "MySQL ODBC 8.0 Unicode Driver", "MySQL ODBC 8.0 ANSI Driver",
            "MariaDB ODBC 3.2 Driver", "MariaDB ODBC 3.1 Driver",
        };
        QString last;
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", m_connectionName);
            for (const auto &drv : odbc) {
                const QString dsn = QString("DRIVER={%1};SERVER=%2;PORT=%3;DATABASE=%4;USER=%5;PASSWORD=%6;OPTION=3;")
                                        .arg(drv, p.host).arg(p.port).arg(p.database, p.user, p.password);
                db.setDatabaseName(dsn);
                if (db.open()) { ok = true; break; }
                last = db.lastError().text();
            }
        }
        if (ok) return true;
        disconnect();
        if (error) {
            *error = QString::fromUtf8(
                "Плагин QMYSQL есть, но без libmysql.dll он не открывается.\n%1\n"
                "Драйвер: %2\nODBC: %3\n\n%4")
                         .arg(clientNote, lastNative, last, driverHint(available));
        }
        return false;
    }

    if (error) *error = clientNote + QLatin1Char('\n') + driverHint(available);
    return false;
}
QStringList DatabaseService::tables(QString *error) const {
    if (!isOpen()) { if(error) *error = tr("Нет подключения к MySQL."); return {}; }
    return database().tables(QSql::Tables);
}
QStringList DatabaseService::databases(QString *error) const {
    if (!isOpen()) { if(error) *error = tr("Нет подключения к MySQL."); return {}; }
    QStringList out; QSqlQuery q(database());
    if (!q.exec("SHOW DATABASES")) { if(error) *error=q.lastError().text(); return {}; }
    while (q.next()) out << q.value(0).toString();
    return out;
}
bool DatabaseService::execute(const QString &sql, QString *error, qint64 *affected) {
    QSqlQuery q(database());
    if (!q.exec(sql)) { if(error) *error=q.lastError().text(); return false; }
    if (affected) *affected=q.numRowsAffected(); return true;
}
