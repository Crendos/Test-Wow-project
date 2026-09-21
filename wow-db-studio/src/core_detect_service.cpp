#include "core_detect_service.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

QString CoreDetection::summary() const {
    QStringList parts;
    if (!coreName.isEmpty()) parts << coreName;
    if (!profile.isEmpty()) parts << profile;
    if (!clientBuild.isEmpty()) parts << ("клиент " + clientBuild);
    if (!branch.isEmpty()) parts << ("ветка " + branch);
    if (!patch.isEmpty()) parts << patch;
    QString s = parts.isEmpty() ? QString("не определено") : parts.join(" · ");
    if (confidence > 0) s += QString(" (уверенность %1%)").arg(confidence);
    return s;
}

static void addEvidence(CoreDetection &d, const QString &e, int score) {
    if (e.isEmpty()) return;
    d.evidence << e;
    d.confidence = qMin(100, d.confidence + score);
}

static QString versionTriple(const QString &s) {
    QRegularExpression re("(\\d+)\\.(\\d+)\\.(\\d+)");
    const auto m = re.match(s);
    if (!m.hasMatch()) return {};
    return m.captured(1) + "." + m.captured(2) + "." + m.captured(3);
}

static QString tripleFromDetection(const CoreDetection &d) {
    for (const auto &s : {d.clientBuild, d.branch, d.patch, d.profile}) {
        const QString t = versionTriple(s);
        if (!t.isEmpty()) return t;
    }
    const QString p = d.profile + QLatin1Char(' ') + d.branch + QLatin1Char(' ') + d.coreName;
    if (p.contains(QLatin1String("Nordrassil")) || p.contains(QLatin1String("7.3.5"))) return QStringLiteral("7.3.5");
    if (p.contains(QLatin1String("3.3.5"))) return QStringLiteral("3.3.5");
    if (p.contains(QLatin1String("4.3.4"))) return QStringLiteral("4.3.4");
    if (p.contains(QLatin1String("5.4.8"))) return QStringLiteral("5.4.8");
    return {};
}

static QString tripleFromBuildNumber(int build) {
    if (build == 5875) return QStringLiteral("1.12.1");
    if (build == 8606) return QStringLiteral("2.4.3");
    if (build == 12340 || (build >= 10505 && build <= 12340)) return QStringLiteral("3.3.5");
    if (build == 15595 || (build >= 15050 && build <= 15595)) return QStringLiteral("4.3.4");
    if (build == 18414 || (build >= 18291 && build <= 18414)) return QStringLiteral("5.4.8");
    if (build >= 24473 && build <= 26972) return QStringLiteral("7.3.5");
    if (build >= 26973 && build <= 33369) return QStringLiteral("8.3.7");
    if (build >= 35000 && build <= 41488) return QStringLiteral("9.2.7");
    if (build >= 45000 && build <= 54999) return QStringLiteral("10.2.7");
    if (build >= 55000 && build <= 59999) return QStringLiteral("11.0.0");
    if (build >= 60000) return QStringLiteral("12.0.0");
    return {};
}

static QString toWagoBuild(const CoreDetection &d, int build) {
    if (build <= 0) {
        QString s = d.clientBuild;
        s.replace(QLatin1String("3.3.5a."), QLatin1String("3.3.5."));
        return s;
    }
    QString triple = tripleFromDetection(d);
    if (triple.isEmpty()) triple = tripleFromBuildNumber(build);
    if (triple.isEmpty()) return QString::number(build);
    return triple + "." + QString::number(build);
}

static void applyKnownBuild(CoreDetection &d, int build) {
    if (build <= 0) return;
    d.gameBuild = build;
    struct Map { int build; const char *profile; const char *branch; };
    static const Map k[] = {
        {5875,  "TrinityCore 3.3.5a", "1.12"},
        {6005,  "TrinityCore 3.3.5a", "1.12"},
        {8606,  "TrinityCore 3.3.5a", "2.4.3"},
        {12340, "TrinityCore 3.3.5a", "3.3.5"},
        {15595, "TrinityCore 4.3.4",  "4.3.4"},
        {18414, "TrinityCore 5.4.8",  "5.4.8"},
        {26972, "TrinityCore 7.3.5",  "7.3.5"},
        {33369, "TrinityCore 10.x",   "master"},
    };
    const bool nord = d.profile.contains(QLatin1String("Nordrassil"));
    for (const auto &m : k) {
        if (m.build == build) {
            if (!nord) d.profile = QString::fromLatin1(m.profile);
            if (d.branch.isEmpty() || !nord) d.branch = QString::fromLatin1(m.branch);
            break;
        }
    }
    if (d.profile.isEmpty()) {
        if (build >= 50000) { d.profile = "TrinityCore 12.x (upstream master)"; d.branch = "master"; }
        else if (build >= 45000) { d.profile = "TrinityCore 10.x"; d.branch = "master"; }
        else if (build >= 24473 && build <= 26972) { d.profile = "TrinityCore 7.3.5"; d.branch = "7.3.5"; }
    }
    d.clientBuild = toWagoBuild(d, build);
}

static void applyVersionText(CoreDetection &d, const QString &text) {
    const QString t = text;
    const QString low = t.toLower();
    if (low.contains("nordrassil")) { d.coreName = "Nordrassil"; d.profile = "Nordrassil Core 7.3.5"; d.branch = "7.3.5"; }
    else if (low.contains("cyphercore")) { d.coreName = "CypherCore"; d.profile = "CypherCore (C#)"; }
    else if (low.contains("azerothcore")) { d.coreName = "AzerothCore"; d.profile = "Другой / кастомный"; }
    else if (low.contains("trinity")) { d.coreName = "TrinityCore"; }
    else if (d.coreName.isEmpty() && !t.isEmpty()) d.coreName = "TrinityCore";

    QRegularExpression ver("(\\d+)\\.(\\d+)\\.(\\d+)(?:\\.(\\d+))?");
    const auto m = ver.match(t);
    if (m.hasMatch()) {
        const int a = m.captured(1).toInt();
        const int b = m.captured(2).toInt();
        const QString full = m.captured(0);
        if (d.clientBuild.isEmpty()) d.clientBuild = full;
        if (a == 3 && b == 3) { d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5"; }
        else if (a == 4 && b == 3) { d.profile = "TrinityCore 4.3.4"; d.branch = "4.3.4"; }
        else if (a == 5 && b == 4) { d.profile = "TrinityCore 5.4.8"; d.branch = "5.4.8"; }
        else if (a == 7 && b == 3) {
            if (!d.profile.contains(QLatin1String("Nordrassil"))) { d.profile = "TrinityCore 7.3.5"; d.branch = "7.3.5"; }
        }
        else if (a == 10) { d.profile = "TrinityCore 10.x"; d.branch = "master"; }
        else if (a >= 12) { d.profile = "TrinityCore 12.x (upstream master)"; d.branch = "master"; }
        else if (a == 11) { d.profile = "TrinityCore 12.x (upstream master)"; d.branch = "master"; }
    }

    if (low.contains("3.3.5")) { d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5"; if (d.clientBuild.isEmpty()) d.clientBuild = "3.3.5a.12340"; }
    if (low.contains("4.3.4")) { d.profile = "TrinityCore 4.3.4"; d.branch = "4.3.4"; if (d.clientBuild.isEmpty()) d.clientBuild = "4.3.4.15595"; }
    if (low.contains("5.4.8")) { d.profile = "TrinityCore 5.4.8"; d.branch = "5.4.8"; }
    if (low.contains("7.3.5") && !d.profile.contains(QLatin1String("Nordrassil"))) { d.profile = "TrinityCore 7.3.5"; d.branch = "7.3.5"; }
    if (low.contains("tdb 335") || low.contains("tdb_full_world_335") || low.contains("335.")) {
        d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5";
        if (d.clientBuild.isEmpty()) d.clientBuild = "3.3.5a.12340";
    }
    if (low.contains("tdb 434") || low.contains("434.")) { d.profile = "TrinityCore 4.3.4"; d.branch = "4.3.4"; }
    if (low.contains("(master") || low.contains("branch master") || low.contains("on master")) {
        if (d.branch.isEmpty()) d.branch = "master";
        if (d.profile.isEmpty()) d.profile = "TrinityCore master";
    }
}

static QStringList systemSchemas() {
    return {"mysql", "information_schema", "performance_schema", "sys", "sys_schema"};
}

CoreDetection CoreDetectService::fromDatabase(const QSqlDatabase &db) {
    CoreDetection d;
    if (!db.isOpen()) { addEvidence(d, "Нет подключения к MySQL", 0); return d; }

    QSqlQuery q(db);
    if (!q.exec("SELECT table_schema, table_name FROM information_schema.tables "
                "WHERE table_schema NOT IN ('mysql','information_schema','performance_schema','sys') "
                "AND table_name IN ('version','realmlist','updates','build_info','hotfix_data','hotfix_blob',"
                "'item_template','item_sparse','spell','spell_template','creature_template')")) {
        addEvidence(d, "information_schema недоступна: " + q.lastError().text(), 0);
        return d;
    }

    QHash<QString, QStringList> byTable;
    while (q.next()) {
        const QString schema = q.value(0).toString();
        const QString table = q.value(1).toString().toLower();
        byTable[table] << schema;
    }

    // version table (world)
    for (const auto &schema : byTable.value("version")) {
        QSqlQuery v(db);
        const QString sql = QString("SELECT * FROM `%1`.`version` LIMIT 1").arg(schema);
        if (!v.exec(sql) || !v.next()) continue;
        const auto rec = v.record();
        QStringList bits;
        for (int i = 0; i < rec.count(); ++i) {
            const QString name = rec.fieldName(i);
            const QString val = v.value(i).toString().trimmed();
            if (val.isEmpty()) continue;
            bits << name + "=" + val;
            if (name.contains("core", Qt::CaseInsensitive) || name.contains("db", Qt::CaseInsensitive)
                || name.contains("version", Qt::CaseInsensitive))
                applyVersionText(d, val);
            if (name.compare("cache_id", Qt::CaseInsensitive) == 0) {
                const int id = val.toInt();
                if (id == 335) { d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5"; d.clientBuild = "3.3.5a.12340"; }
                else if (id == 434) { d.profile = "TrinityCore 4.3.4"; d.branch = "4.3.4"; d.clientBuild = "4.3.4.15595"; }
                else if (id == 548) { d.profile = "TrinityCore 5.4.8"; d.branch = "5.4.8"; }
                else if (id == 735) { d.profile = "TrinityCore 7.3.5"; d.branch = "7.3.5"; }
            }
        }
        addEvidence(d, schema + ".version: " + bits.join("; "), 40);
        if (d.patch.isEmpty()) d.patch = bits.join(" | ");
    }

    // realmlist.gamebuild — клиентская сборка для wago.tools (26365 → 7.3.5.26365).
    int realmGameBuild = 0;
    for (const auto &schema : byTable.value("realmlist")) {
        QSqlQuery r(db);
        if (!r.exec(QString("SELECT * FROM `%1`.`realmlist`").arg(schema))) continue;
        while (r.next()) {
            const auto rec = r.record();
            for (int i = 0; i < rec.count(); ++i) {
                const QString name = rec.fieldName(i);
                if (name.compare("gamebuild", Qt::CaseInsensitive) != 0
                    && name.compare("game_build", Qt::CaseInsensitive) != 0)
                    continue;
                const int build = r.value(i).toInt();
                if (build < 5875) continue;
                if (realmGameBuild == 0) realmGameBuild = build;
                addEvidence(d, schema + ".realmlist." + name + "=" + QString::number(build), 35);
            }
        }
    }

    QString fromBuildInfo;
    auto readBuildInfoRow = [&](const QString &schema, int build) -> QString {
        QSqlQuery b(db);
        b.prepare(QString("SELECT * FROM `%1`.`build_info` WHERE `build` = ? LIMIT 1").arg(schema));
        b.addBindValue(build);
        if (!b.exec() || !b.next()) return {};
        const auto rec = b.record();
        int major = 0, minor = 0, bugfix = 0, buildCol = build;
        for (int i = 0; i < rec.count(); ++i) {
            const QString n = rec.fieldName(i).toLower();
            const int v = rec.value(i).toInt();
            if (n == "majorversion" || n == "major") major = v;
            else if (n == "minorversion" || n == "minor") minor = v;
            else if (n == "bugfixversion" || n == "bugfix" || n == "patch") bugfix = v;
            else if (n == "build") buildCol = v;
        }
        if (major <= 0) return {};
        return QString("%1.%2.%3.%4").arg(major).arg(minor).arg(bugfix).arg(buildCol);
    };
    if (realmGameBuild > 0) {
        for (const auto &schema : byTable.value("build_info")) {
            fromBuildInfo = readBuildInfoRow(schema, realmGameBuild);
            if (!fromBuildInfo.isEmpty()) {
                addEvidence(d, schema + ".build_info → " + fromBuildInfo, 40);
                break;
            }
        }
        if (fromBuildInfo.isEmpty()) {
            QSqlQuery find(db);
            if (find.exec("SELECT table_schema FROM information_schema.tables "
                          "WHERE table_name = 'build_info' "
                          "AND table_schema NOT IN ('mysql','information_schema','performance_schema','sys') LIMIT 1")
                && find.next()) {
                fromBuildInfo = readBuildInfoRow(find.value(0).toString(), realmGameBuild);
                if (!fromBuildInfo.isEmpty())
                    addEvidence(d, find.value(0).toString() + ".build_info → " + fromBuildInfo, 40);
            }
        }
        applyKnownBuild(d, realmGameBuild);
        if (!fromBuildInfo.isEmpty())
            d.clientBuild = fromBuildInfo;
        else
            d.clientBuild = toWagoBuild(d, realmGameBuild);
    }

    // schema fingerprints
    if (byTable.contains("hotfix_data") || byTable.contains("hotfix_blob")) {
        addEvidence(d, "Найдены hotfix-таблицы (ядро 6.x+ / retail)", 15);
        if (d.profile.isEmpty()) { d.profile = "TrinityCore 12.x (upstream master)"; d.branch = "master"; }
    }
    if (byTable.contains("item_template") && !byTable.contains("item_sparse")) {
        addEvidence(d, "Есть item_template, нет item_sparse (классическая схема 3.3.5–5.x)", 10);
        if (d.profile.isEmpty()) { d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5"; }
    }
    if (byTable.contains("spell_template") && !byTable.contains("spell")) {
        addEvidence(d, "Таблица spell_template (WotLK/Cata-стиль)", 8);
    }

    // creature_template columns
    if (byTable.contains("creature_template")) {
        const QString schema = byTable.value("creature_template").first();
        QSqlQuery c(db);
        c.prepare("SELECT column_name FROM information_schema.columns WHERE table_schema=? AND table_name='creature_template'");
        c.addBindValue(schema);
        QSet<QString> cols;
        if (c.exec()) while (c.next()) cols.insert(c.value(0).toString().toLower());
        if (cols.contains("healthscalingexpansion")) {
            addEvidence(d, schema + ".creature_template.HealthScalingExpansion → Legion+", 12);
            if (d.profile.isEmpty() || d.profile.contains("3.3.5")) {
                d.profile = "TrinityCore 7.3.5"; d.branch = "7.3.5";
            }
        } else if (cols.contains("healthmodifier") || cols.contains("health_mod")) {
            addEvidence(d, schema + ".creature_template health modifier (WotLK-стиль)", 8);
        }
        if (cols.contains("verifiedbuild")) {
            addEvidence(d, schema + ".creature_template.VerifiedBuild", 6);
        }
    }

    if (d.coreName.isEmpty() && !d.profile.isEmpty()) {
        d.coreName = d.profile.startsWith("Cypher") ? "CypherCore" : "TrinityCore";
    }
    if (d.patch.isEmpty() && !d.clientBuild.isEmpty())
        d.patch = "Клиент / build " + d.clientBuild;
    if (d.confidence == 0 && !d.profile.isEmpty()) d.confidence = 25;
    return d;
}

static QString readHead(const QString &path, qint64 maxBytes = 16384) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.read(maxBytes));
}

static void scanBuildInfo(CoreDetection &d, const QString &text, const QString &from) {
    if (text.isEmpty()) return;
    applyVersionText(d, text);
    QRegularExpression re("(\\d+\\.\\d+\\.\\d+\\.\\d+)");
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString v = it.next().captured(1);
        d.clientBuild = v;
        applyVersionText(d, v);
        addEvidence(d, from + ": " + v, 30);
        break;
    }
    QRegularExpression buildRe("\\b(12340|15595|18414|26972|5875)\\b");
    const auto bm = buildRe.match(text);
    if (bm.hasMatch()) applyKnownBuild(d, bm.captured(1).toInt());
}

CoreDetection CoreDetectService::fromFolder(const QString &path) {
    CoreDetection d;
    const QFileInfo root(path);
    if (path.trimmed().isEmpty() || !root.exists() || !root.isDir()) {
        // Поле на вкладке «Подключение» необязательно: ядро уже читается из MySQL.
        return d;
    }
    addEvidence(d, "Папка: " + QDir::toNativeSeparators(root.absoluteFilePath()), 5);

    const QString abs = root.absoluteFilePath();
    if (abs.contains(QLatin1String("nordrassil"), Qt::CaseInsensitive)
        || QFileInfo(abs).fileName().contains(QLatin1String("nordrassil"), Qt::CaseInsensitive)) {
        d.coreName = "Nordrassil";
        d.profile = "Nordrassil Core 7.3.5";
        d.branch = "7.3.5";
        addEvidence(d, "Имя папки содержит Nordrassil", 30);
    }
    const QStringList named = {
        abs + "/README.md", abs + "/readme.md", abs + "/README",
        abs + "/.build.info", abs + "/_retail_/.build.info", abs + "/_classic_/.build.info",
        abs + "/.flavor.info", abs + "/_retail_/.flavor.info",
        abs + "/revision_data.h", abs + "/src/server/shared/revision_data.h",
        abs + "/src/common/GitRevision.cpp",
        abs + "/CMakeLists.txt",
        abs + "/.git/HEAD",
        abs + "/worldserver.conf", abs + "/etc/worldserver.conf",
    };
    for (const auto &p : named) {
        if (!QFileInfo::exists(p)) continue;
        const QString body = readHead(p);
        const QString base = QFileInfo(p).fileName();
        if (base.compare("README.md", Qt::CaseInsensitive) == 0 || base.compare("readme.md", Qt::CaseInsensitive) == 0) {
            applyVersionText(d, body.left(4000));
            addEvidence(d, "README: распознан текст версии", 20);
        } else if (base.contains("build.info") || base.contains("flavor.info")) {
            scanBuildInfo(d, body, base);
        } else if (base == "HEAD") {
            QString branch = body.trimmed();
            if (branch.startsWith("ref: refs/heads/")) branch = branch.mid(16);
            d.branch = branch;
            addEvidence(d, ".git/HEAD → " + branch, 15);
            if (branch.contains("3.3.5")) { d.profile = "TrinityCore 3.3.5a"; d.clientBuild = d.clientBuild.isEmpty() ? "3.3.5a.12340" : d.clientBuild; }
            if (branch == "master") { if (d.profile.isEmpty()) d.profile = "TrinityCore master"; }
        } else if (base.startsWith("CMakeLists")) {
            if (body.contains("Nordrassil", Qt::CaseInsensitive)) {
                d.coreName = "Nordrassil"; d.profile = "Nordrassil Core 7.3.5"; d.branch = "7.3.5";
                addEvidence(d, "CMakeLists/README: Nordrassil Core 7.3.5", 40);
            }
            if (body.contains("TrinityCore")) { d.coreName = d.coreName.isEmpty() ? "TrinityCore" : d.coreName; addEvidence(d, "CMakeLists.txt: TrinityCore", 25); }
            if (body.contains("CypherCore")) { d.coreName = "CypherCore"; d.profile = "CypherCore (C#)"; addEvidence(d, "CMakeLists.txt: CypherCore", 25); }
            applyVersionText(d, body.left(2000));
        } else {
            applyVersionText(d, body);
            scanBuildInfo(d, body, base);
        }
    }

    // Walk shallow for client/core markers (depth 2).
    QDirIterator it(abs, QDir::Files, QDirIterator::Subdirectories);
    int seen = 0;
    while (it.hasNext() && seen < 4000) {
        const QString p = it.next();
        ++seen;
        const QFileInfo fi(p);
        const QString rel = QDir(abs).relativeFilePath(p);
        if (rel.count('/') > 3) continue;
        const QString name = fi.fileName();
        if (name.compare(".build.info", Qt::CaseInsensitive) == 0)
            scanBuildInfo(d, readHead(p), rel);
        else if (name.endsWith(".csproj", Qt::CaseInsensitive) && name.contains("Cypher", Qt::CaseInsensitive)) {
            d.coreName = "CypherCore"; d.profile = "CypherCore (C#)";
            addEvidence(d, "Найден " + rel, 30);
        } else if (name.compare("Wow.exe", Qt::CaseInsensitive) == 0
                   || name.compare("WowClassic.exe", Qt::CaseInsensitive) == 0
                   || name.compare("Wow-64.exe", Qt::CaseInsensitive) == 0) {
            addEvidence(d, "Клиентский исполняемый файл: " + rel, 15);
            if (d.coreName.isEmpty()) { /* client folder */ }
        }
    }

    if (QFileInfo::exists(abs + "/Data/common.MPQ") || QFileInfo::exists(abs + "/Data/patch.MPQ")
        || QFileInfo::exists(abs + "/data/common.MPQ")) {
        addEvidence(d, "MPQ Data/ → клиент 3.3.5a (или старше CASC)", 20);
        if (d.clientBuild.isEmpty()) d.clientBuild = "3.3.5a.12340";
        if (d.profile.isEmpty()) { d.profile = "TrinityCore 3.3.5a"; d.branch = "3.3.5"; }
    }
    if (QDir(abs + "/Data/casc").exists() || QDir(abs + "/_retail_").exists() || QDir(abs + "/Data/indices").exists()) {
        addEvidence(d, "CASC / _retail_ → клиент 6.x+", 15);
        if (d.profile.isEmpty()) { d.profile = "TrinityCore 12.x (upstream master)"; d.branch = "master"; }
    }
    if (QDir(abs + "/src/server").exists() || QDir(abs + "/src/server/game").exists()) {
        if (d.coreName.isEmpty()) d.coreName = "TrinityCore";
        addEvidence(d, "Каталог src/server — исходники ядра C++", 20);
    }

    if (d.coreName.isEmpty() && !d.profile.isEmpty())
        d.coreName = d.profile.contains(QLatin1String("Nordrassil")) ? QString("Nordrassil")
                   : d.profile.startsWith("Cypher") ? QString("CypherCore") : QString("TrinityCore");
    if (d.patch.isEmpty() && !d.clientBuild.isEmpty())
        d.patch = "Клиент / build " + d.clientBuild;
    return d;
}

CoreDetection CoreDetectService::merge(CoreDetection a, const CoreDetection &b) {
    auto pick = [](QString &dst, const QString &src) { if (dst.isEmpty() && !src.isEmpty()) dst = src; };
    // Патч Wago берётся из БД (realmlist.gamebuild). Папка не перебивает его.
    if (a.profile.isEmpty()) a.profile = b.profile;
    else if (!b.profile.isEmpty() && a.confidence < b.confidence && a.gameBuild <= 0) a.profile = b.profile;
    pick(a.branch, b.branch);
    if (a.gameBuild <= 0 && b.gameBuild > 0) {
        a.gameBuild = b.gameBuild;
        a.clientBuild = b.clientBuild;
    } else if (a.gameBuild <= 0 && !b.clientBuild.isEmpty()) {
        a.clientBuild = b.clientBuild;
    }
    pick(a.coreName, b.coreName);
    if (!b.patch.isEmpty()) {
        if (a.patch.isEmpty()) a.patch = b.patch;
        else if (!a.patch.contains(b.patch)) a.patch += " · " + b.patch;
    }
    a.evidence += b.evidence;
    a.confidence = qMin(100, qMax(a.confidence, b.confidence) + (b.confidence > 0 && a.confidence > 0 ? 10 : 0));
    if (a.coreName.isEmpty() && !a.profile.isEmpty())
        a.coreName = a.profile.contains(QLatin1String("Nordrassil")) ? QString("Nordrassil")
                   : a.profile.startsWith("Cypher") ? QString("CypherCore") : QString("TrinityCore");
    return a;
}
