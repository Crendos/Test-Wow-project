#include "qa_bot_service.h"
#include "database_service.h"
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <cmath>

const char *QaBotService::kGeodesist = "Геодезист";
const char *QaBotService::kBestiary = "Бестиарий";
const char *QaBotService::kQuest = "Квестолог";
const char *QaBotService::kQuartermaster = "Интендант";
const char *QaBotService::kDirector = "Режиссёр";

static bool identOk(const QString &s) {
    if (s.isEmpty()) return false;
    for (QChar c : s)
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')) return false;
    return true;
}

static QString qf(const QString &schema, const QString &table) {
    if (identOk(schema)) return QStringLiteral("`%1`.`%2`").arg(schema, table);
    return QStringLiteral("`%1`").arg(table);
}

static bool hasTable(QSqlDatabase db, const QString &schema, const QString &table) {
    if (!identOk(table)) return false;
    QSqlQuery q(db);
    if (identOk(schema)) {
        q.prepare(QStringLiteral(
            "SELECT 1 FROM information_schema.tables WHERE table_schema=? AND table_name=? LIMIT 1"));
        q.addBindValue(schema);
        q.addBindValue(table);
    } else {
        q.prepare(QStringLiteral(
            "SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name=? LIMIT 1"));
        q.addBindValue(table);
    }
    return q.exec() && q.next();
}

static bool hasCol(QSqlDatabase db, const QString &schema, const QString &table, const QString &col) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM information_schema.columns WHERE table_schema=? AND table_name=? AND column_name=? LIMIT 1"));
    q.addBindValue(identOk(schema) ? schema : db.databaseName());
    q.addBindValue(table);
    q.addBindValue(col);
    return q.exec() && q.next();
}

static QString worldSchema(QSqlDatabase db) {
    const QString cur = db.databaseName();
    if (hasTable(db, cur, QStringLiteral("creature")) || hasTable(db, cur, QStringLiteral("creature_template")))
        return cur;
    if (hasTable(db, QStringLiteral("world"), QStringLiteral("creature_template")))
        return QStringLiteral("world");
    return cur;
}

static QList<int> extractIds(const QString &task) {
    QList<int> ids;
    auto it = QRegularExpression(QStringLiteral("\\b(\\d{1,10})\\b")).globalMatch(task);
    while (it.hasNext() && ids.size() < 16) {
        const int v = it.next().captured(1).toInt();
        if (v > 0 && !ids.contains(v)) ids << v;
    }
    return ids;
}

struct ParsedPos {
    bool ok = false;
    int map = -1;
    double x = 0, y = 0, z = 0, o = 0;
    bool hasO = false;
};

static ParsedPos parsePos(const QString &task) {
    ParsedPos p;
    auto grab = [&](const char *key, double *out) {
        const auto re = QRegularExpression(QStringLiteral("%1\\s*[:=]\\s*(-?\\d+(?:[.,]\\d+)?)")
                                               .arg(QLatin1String(key)),
                                           QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(task);
        if (!m.hasMatch()) return false;
        *out = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        return true;
    };
    int mapI = -1;
    double x = 0, y = 0, z = 0, o = 0;
    {
        const auto m = QRegularExpression(QStringLiteral("map\\s*[:=]\\s*(\\d+)"),
                                          QRegularExpression::CaseInsensitiveOption)
                           .match(task);
        if (m.hasMatch()) mapI = m.captured(1).toInt();
    }
    const bool hx = grab("x", &x);
    const bool hy = grab("y", &y);
    const bool hz = grab("z", &z);
    const bool ho = grab("o", &o) || grab("ori", &o) || grab("orientation", &o);
    if (hx && hy && hz) {
        p.ok = true;
        p.x = x;
        p.y = y;
        p.z = z;
        p.map = mapI;
        p.o = o;
        p.hasO = ho;
        return p;
    }
    // «0 -8833.2 627.1 94.0 1.57» = map x y z [o]
    const auto m = QRegularExpression(
                       QStringLiteral("(?:^|\\s)(\\d{1,4})\\s+(-?\\d+(?:[.,]\\d+)?)\\s+(-?\\d+(?:[.,]\\d+)?)\\s+(-?\\d+(?:[.,]\\d+)?)(?:\\s+(-?\\d+(?:[.,]\\d+)?))?"))
                       .match(task);
    if (m.hasMatch()) {
        p.ok = true;
        p.map = m.captured(1).toInt();
        p.x = m.captured(2).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        p.y = m.captured(3).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        p.z = m.captured(4).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        if (!m.captured(5).isEmpty()) {
            p.o = m.captured(5).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
            p.hasO = true;
        }
    }
    return p;
}

static QString sqlLit(const QString &s) {
    QString t = s;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + t + QLatin1Char('\'');
}

static QString wanderCol(QSqlDatabase db, const QString &world) {
    if (hasCol(db, world, QStringLiteral("creature"), QStringLiteral("wander_distance")))
        return QStringLiteral("wander_distance");
    if (hasCol(db, world, QStringLiteral("creature"), QStringLiteral("spawndist")))
        return QStringLiteral("spawndist");
    return QString();
}

static int countWhere(QSqlDatabase db, const QString &sql) {
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

static bool rowExists(QSqlDatabase db, const QString &tableSql, const QString &col, int id) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE `%2` = ? LIMIT 1").arg(tableSql, col));
    q.addBindValue(id);
    return q.exec() && q.next();
}

static void addFix(QaRunResult &r, const QString &bot, const QString &problem,
                   const QString &retail, const QString &sql, const QString &schema) {
    QaFix f;
    f.bot = bot;
    f.problem = problem;
    f.retail = retail;
    f.sql = sql.trimmed();
    f.schema = schema;
    r.fixes.push_back(f);
}

static QString esc(const QString &s) { return s; }

static void botGeodesist(QaRunResult &r, QSqlDatabase db, const QString &world,
                         const QString &task, const QList<int> &ids, const ParsedPos &pos) {
    const QString bot = QString::fromUtf8(QaBotService::kGeodesist);
    const QString retail = QStringLiteral(
        "Ретейл: точка спавна — map + XYZ в ярдах, orientation в радианах "
        "(0 = север / +X, π/2 ≈ запад / +Y, π = юг). "
        "MovementType 0 = стоит, 1 = бродит в радиусе wander_distance, "
        "2 = идёт по пути (waypoint_data / waypoint_path). "
        "Случайный бродяжничество по всей карте на ретейле нет.");
    r.report += QStringLiteral("—— %1 ——\n").arg(bot);
    if (!hasTable(db, world, QStringLiteral("creature"))) {
        r.report += QStringLiteral("Нет world.creature — координат спавнов нет.\n\n");
        addFix(r, bot, QStringLiteral("Нет таблицы world.creature"), retail, QString(), world);
        return;
    }
    const QString cr = qf(world, QStringLiteral("creature"));
    const QString wcol = wanderCol(db, world);
    const bool hasMove = hasCol(db, world, QStringLiteral("creature"), QStringLiteral("MovementType"));
    const bool hasOri = hasCol(db, world, QStringLiteral("creature"), QStringLiteral("orientation"));

    auto inspectEntry = [&](int entry) {
        QSqlQuery q(db);
        QString sql = QStringLiteral("SELECT guid, id, map, position_x, position_y, position_z");
        if (hasOri) sql += QStringLiteral(", orientation");
        if (hasMove) sql += QStringLiteral(", MovementType");
        if (!wcol.isEmpty()) sql += QStringLiteral(", `%1`").arg(wcol);
        sql += QStringLiteral(" FROM %1 WHERE id = ? LIMIT 30").arg(cr);
        q.prepare(sql);
        q.addBindValue(entry);
        if (!q.exec()) {
            r.report += QStringLiteral("  SQL: %1\n").arg(q.lastError().databaseText());
            return;
        }
        int n = 0;
        while (q.next()) {
            ++n;
            const qint64 guid = q.value(0).toLongLong();
            const int map = q.value(2).toInt();
            const double x = q.value(3).toDouble();
            const double y = q.value(4).toDouble();
            const double z = q.value(5).toDouble();
            r.report += QStringLiteral("  guid=%1 map=%2 xyz=(%3, %4, %5)\n")
                            .arg(guid).arg(map)
                            .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
            if (std::fabs(x) < 0.01 && std::fabs(y) < 0.01) {
                addFix(r, bot,
                       QStringLiteral("guid %1 NPC %2 стоит в (0,0) map=%3 — это не ретейл, точка сломана")
                           .arg(guid).arg(entry).arg(map),
                       retail,
                       pos.ok ? QStringLiteral(
                                    "UPDATE %1 SET map=%2, position_x=%3, position_y=%4, position_z=%5%6 "
                                    "WHERE guid=%7;")
                                    .arg(cr)
                                    .arg(pos.map >= 0 ? pos.map : map)
                                    .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4).arg(pos.z, 0, 'f', 4)
                                    .arg(pos.hasO && hasOri
                                             ? QStringLiteral(", orientation=%1").arg(pos.o, 0, 'f', 4)
                                             : QString())
                                    .arg(guid)
                              : QString(),
                       world);
            }
            if (hasMove) {
                const int mt = q.value(hasOri ? 7 : 6).toInt();
                double wander = 0;
                if (!wcol.isEmpty())
                    wander = q.value(q.record().indexOf(wcol)).toDouble();
                if (mt == 1 && wander > 40.0) {
                    addFix(r, bot,
                           QStringLiteral("guid %1 NPC %2 MovementType=1 wander=%3 — бродит слишком далеко (на ретейле обычно стоит или короткий путь)")
                               .arg(guid).arg(entry).arg(wander, 0, 'f', 1),
                           retail,
                           QStringLiteral("UPDATE %1 SET MovementType=0%2 WHERE guid=%3;")
                               .arg(cr)
                               .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`=0").arg(wcol))
                               .arg(guid),
                           world);
                }
            }
        }
        if (n == 0) {
            r.report += QStringLiteral("  Нет спавнов id=%1.\n").arg(entry);
            QString sqlFix;
            if (pos.ok && pos.map >= 0) {
                sqlFix = QStringLiteral(
                             "INSERT INTO %1 (`guid`,`id`,`map`,`position_x`,`position_y`,`position_z`,`orientation`) "
                             "SELECT g, %2, %3, %4, %5, %6, %7 FROM (SELECT IFNULL(MAX(`guid`),0)+1 AS g FROM %1) s;")
                             .arg(cr)
                             .arg(entry)
                             .arg(pos.map)
                             .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4).arg(pos.z, 0, 'f', 4)
                             .arg(pos.hasO ? pos.o : 0.0, 0, 'f', 4);
            }
            addFix(r, bot,
                   QStringLiteral("NPC %1 нет на карте (world.creature). Wago Creature — не спавн.")
                       .arg(entry),
                   retail, sqlFix, world);
        }
    };

    if (ids.isEmpty()) {
        QString sql = QStringLiteral(
            "SELECT id, COUNT(*) c FROM %1 WHERE ABS(position_x)<0.01 AND ABS(position_y)<0.01 "
            "GROUP BY id ORDER BY c DESC LIMIT 20").arg(cr);
        QSqlQuery q(db);
        if (q.exec(sql)) {
            int n = 0;
            while (q.next()) {
                ++n;
                addFix(r, bot,
                       QStringLiteral("NPC %1: %2 спавн(ов) в (0,0) — сломанные координаты")
                           .arg(q.value(0).toInt()).arg(q.value(1).toInt()),
                       retail, QString(), world);
            }
            if (n == 0) r.report += QStringLiteral("  Спавнов в (0,0) не найдено.\n");
        }
        if (hasMove && !wcol.isEmpty()) {
            QSqlQuery q2(db);
            q2.exec(QStringLiteral("SELECT id, guid, `%1` FROM %2 WHERE MovementType=1 AND `%1`>40 LIMIT 20")
                        .arg(wcol, cr));
            while (q2.next()) {
                const qint64 guid = q2.value(1).toLongLong();
                addFix(r, bot,
                       QStringLiteral("guid %1 NPC %2 бродит wander>%3")
                           .arg(guid).arg(q2.value(0).toInt()).arg(q2.value(2).toDouble(), 0, 'f', 0),
                       retail,
                       QStringLiteral("UPDATE %1 SET MovementType=0, `%2`=0 WHERE guid=%3;")
                           .arg(cr, wcol).arg(guid),
                       world);
            }
        }
    } else {
        for (int id : ids) inspectEntry(id);
    }

    const QString low = task.toLower();
    if (low.contains(QStringLiteral("не брод")) || low.contains(QStringLiteral("стоять"))
        || low.contains(QStringLiteral("не гуля")) || low.contains(QLatin1String("wander"))) {
        for (int id : ids) {
            if (hasMove) {
                addFix(r, bot,
                       QStringLiteral("Задача: NPC %1 не должен бродить по карте").arg(id),
                       retail,
                       QStringLiteral("UPDATE %1 SET MovementType=0%2 WHERE id=%3;")
                           .arg(cr)
                           .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`=0").arg(wcol))
                           .arg(id),
                       world);
            }
        }
    }
    if (pos.ok && !ids.isEmpty()) {
        r.report += QStringLiteral("  Заданные координаты: map=%1 xyz=(%2,%3,%4) o=%5\n")
                        .arg(pos.map)
                        .arg(pos.x, 0, 'f', 2).arg(pos.y, 0, 'f', 2).arg(pos.z, 0, 'f', 2)
                        .arg(pos.hasO ? QString::number(pos.o, 'f', 3) : QStringLiteral("—"));
        for (int id : ids) {
            addFix(r, bot,
                   QStringLiteral("Переставить все спавны NPC %1 в заданную точку (как вы указали / ретейл)")
                       .arg(id),
                   retail,
                   QStringLiteral("UPDATE %1 SET map=%2, position_x=%3, position_y=%4, position_z=%5%6 WHERE id=%7;")
                       .arg(cr)
                       .arg(pos.map >= 0 ? pos.map : 0)
                       .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4).arg(pos.z, 0, 'f', 4)
                       .arg(pos.hasO && hasOri ? QStringLiteral(", orientation=%1").arg(pos.o, 0, 'f', 4)
                                               : QString())
                       .arg(id),
                   world);
        }
    }
    r.report += QLatin1Char('\n');
}

static QString firstCol(QSqlDatabase db, const QString &schema, const QString &table,
                        const QStringList &names) {
    for (const QString &n : names)
        if (hasCol(db, schema, table, n)) return n;
    return QString();
}

static void botBestiary(QaRunResult &r, QSqlDatabase db, const QString &world,
                        const QString &hotfix, const QList<int> &ids) {
    const QString bot = QString::fromUtf8(QaBotService::kBestiary);
    const QString retail = QStringLiteral(
        "Ретейл: ID существа = Creature.db2 = creature_template.entry. "
        "Имя/титул — Creature.db2; картинка — CreatureDisplayInfo (не сама таблица Creature). "
        "Trinity 12.x master НЕ грузит Creature.db2: модель для геймплея — "
        "world.creature_template_model + hotfixes.creature_display_info.");
    r.report += QStringLiteral("—— %1 ——\n").arg(bot);
    if (!hasTable(db, world, QStringLiteral("creature_template"))) {
        addFix(r, bot, QStringLiteral("Нет creature_template — подключите world"), retail, QString(), world);
        r.report += QStringLiteral("  нет creature_template\n\n");
        return;
    }
    const QString ct = qf(world, QStringLiteral("creature_template"));
    auto one = [&](int entry) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT entry FROM %1 WHERE entry=? LIMIT 1").arg(ct));
        q.addBindValue(entry);
        if (!q.exec() || !q.next()) {
            addFix(r, bot, QStringLiteral("NPC %1 нет в creature_template — ядро его не знает").arg(entry),
                   retail,
                   QStringLiteral("-- Каркас (статы с ретейла/TDB всё равно нужны):\n"
                                  "INSERT INTO %1 (entry, name, npcflag, type, faction) "
                                  "VALUES (%2, 'TODO', 0, 7, 35);")
                       .arg(ct).arg(entry),
                   world);
            return;
        }
        QList<int> displays;
        if (hasTable(db, world, QStringLiteral("creature_template_model"))) {
            QSqlQuery m(db);
            m.prepare(QStringLiteral("SELECT CreatureDisplayID FROM %1 WHERE CreatureID=?")
                          .arg(qf(world, QStringLiteral("creature_template_model"))));
            m.addBindValue(entry);
            if (m.exec())
                while (m.next()) displays << m.value(0).toInt();
            if (displays.isEmpty()) {
                int wagoDisp = 0;
                const QString dcol = firstCol(db, hotfix, QStringLiteral("creature"),
                                              {QStringLiteral("DisplayID1"), QStringLiteral("DisplayID_0"),
                                               QStringLiteral("DisplayID")});
                if (!dcol.isEmpty() && hasTable(db, hotfix, QStringLiteral("creature"))) {
                    QSqlQuery w(db);
                    w.prepare(QStringLiteral("SELECT `%1` FROM %2 WHERE ID=? LIMIT 1")
                                  .arg(dcol, qf(hotfix, QStringLiteral("creature"))));
                    w.addBindValue(entry);
                    if (w.exec() && w.next()) wagoDisp = w.value(0).toInt();
                }
                addFix(r, bot,
                       QStringLiteral("NPC %1 без модели (creature_template_model) — в клиенте «?»").arg(entry),
                       retail,
                       wagoDisp > 0
                           ? QStringLiteral(
                                 "INSERT INTO %1 (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability, VerifiedBuild) "
                                 "VALUES (%2, 0, %3, 1, 1, 69497);")
                                 .arg(qf(world, QStringLiteral("creature_template_model")))
                                 .arg(entry)
                                 .arg(wagoDisp)
                           : QStringLiteral("-- Укажите DisplayID с ретейла (Wago CreatureDisplayInfo / Creature.DisplayID_0)\n"
                                            "INSERT INTO %1 (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability) "
                                            "VALUES (%2, 0, /*DISPLAY*/, 1, 1);")
                                 .arg(qf(world, QStringLiteral("creature_template_model")))
                                 .arg(entry),
                       world);
            }
        }
        if (hasTable(db, hotfix, QStringLiteral("creature_display_info"))) {
            for (int d : displays) {
                QSqlQuery dqi(db);
                dqi.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE ID=?")
                                .arg(qf(hotfix, QStringLiteral("creature_display_info"))));
                dqi.addBindValue(d);
                if (dqi.exec() && dqi.next() && dqi.value(0).toInt() == 0) {
                    addFix(r, bot,
                           QStringLiteral("DisplayID %1 NPC %2 нет в hotfixes.creature_display_info — импорт Wago CreatureDisplayInfo")
                               .arg(d).arg(entry),
                           retail, QString(), hotfix);
                }
            }
        }
        if (hasTable(db, hotfix, QStringLiteral("creature"))) {
            const QString ncol = firstCol(db, hotfix, QStringLiteral("creature"),
                                          {QStringLiteral("Name"), QStringLiteral("Name_lang")});
            if (!ncol.isEmpty()) {
                QSqlQuery n(db);
                n.prepare(QStringLiteral("SELECT `%1` FROM %2 WHERE ID=? LIMIT 1")
                              .arg(ncol, qf(hotfix, QStringLiteral("creature"))));
                n.addBindValue(entry);
                if (n.exec() && n.next()) {
                    const QString wagoName = n.value(0).toString();
                    if (!wagoName.isEmpty() && hasTable(db, world, QStringLiteral("creature_template_locale"))) {
                        addFix(r, bot,
                               QStringLiteral("Синхронизировать имя NPC %1 с ретейлом (Wago): «%2»").arg(entry).arg(wagoName),
                               retail,
                               QStringLiteral(
                                   "INSERT INTO %1 (ID, locale, Name, Title, VerifiedBuild) VALUES (%2,'ruRU',%3,'',69497) "
                                   "ON DUPLICATE KEY UPDATE Name=VALUES(Name);")
                                   .arg(qf(world, QStringLiteral("creature_template_locale")))
                                   .arg(entry)
                                   .arg(sqlLit(wagoName)),
                               world);
                    }
                }
            }
        }
        r.report += QStringLiteral("  NPC %1: шаблон есть, моделей=%2\n").arg(entry).arg(displays.size());
    };
    if (ids.isEmpty()) {
        if (hasTable(db, world, QStringLiteral("creature_template_model"))
            && hasTable(db, hotfix, QStringLiteral("creature_display_info"))) {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                       "SELECT m.CreatureID, m.CreatureDisplayID FROM %1 m "
                       "LEFT JOIN %2 d ON d.ID=m.CreatureDisplayID "
                       "WHERE m.CreatureDisplayID>0 AND d.ID IS NULL LIMIT 20")
                       .arg(qf(world, QStringLiteral("creature_template_model")),
                            qf(hotfix, QStringLiteral("creature_display_info"))));
            int n = 0;
            while (q.next()) {
                ++n;
                addFix(r, bot,
                       QStringLiteral("NPC %1 DisplayID %2 нет в creature_display_info")
                           .arg(q.value(0).toInt()).arg(q.value(1).toInt()),
                       retail, QString(), hotfix);
            }
            r.report += n ? QStringLiteral("  битых DisplayID: %1+\n").arg(n)
                          : QStringLiteral("  модели vs creature_display_info: чисто\n");
        }
    } else {
        for (int id : ids) one(id);
    }
    r.report += QLatin1Char('\n');
}

static void botQuest(QaRunResult &r, QSqlDatabase db, const QString &world, const QList<int> &ids, const QaRetailCatalog &cat) {
    const QString bot = QString::fromUtf8(QaBotService::kQuest);
    const QString retail = QStringLiteral(
        "Ретейл: квест живёт в QuestV2 / Quest. Выдача — NPC (gossip/квест-флаг), "
        "world: quest_template + creature_queststarter/ender + quest_objectives. "
        "Без стартера восклицателя над NPC нет.");
    r.report += QStringLiteral("—— %1 ——\n").arg(bot);
    if (!hasTable(db, world, QStringLiteral("quest_template"))) {
        addFix(r, bot, QStringLiteral("Нет quest_template"), retail, QString(), world);
        r.report += QLatin1Char('\n');
        return;
    }
    const QString qt = qf(world, QStringLiteral("quest_template"));
    auto one = [&](int id) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT ID FROM %1 WHERE ID=? LIMIT 1").arg(qt));
        q.addBindValue(id);
        if (!q.exec() || !q.next()) {
            addFix(r, bot, QStringLiteral("Квест %1 нет в quest_template").arg(id), retail, QString(), world);
            return;
        }
        if (hasTable(db, world, QStringLiteral("creature_queststarter"))) {
            QSqlQuery s(db);
            s.prepare(QStringLiteral("SELECT id FROM %1 WHERE quest=?").arg(qf(world, QStringLiteral("creature_queststarter"))));
            s.addBindValue(id);
            QStringList st;
            if (s.exec())
                while (s.next()) st << s.value(0).toString();
            if (st.isEmpty()) {
                addFix(r, bot,
                       QStringLiteral("Квест %1 никто не выдаёт (creature_queststarter пуст)").arg(id),
                       retail,
                       QStringLiteral("INSERT INTO %1 (id, quest) VALUES (/*NPC_ENTRY*/,%2);")
                           .arg(qf(world, QStringLiteral("creature_queststarter")))
                           .arg(id),
                       world);
            } else {
                r.report += QStringLiteral("  квест %1 выдают NPC %2\n").arg(id).arg(st.join(QStringLiteral(",")));
            }
        }
        if (hasTable(db, world, QStringLiteral("creature_questender"))) {
            QSqlQuery e(db);
            e.prepare(QStringLiteral("SELECT id FROM %1 WHERE quest=?").arg(qf(world, QStringLiteral("creature_questender"))));
            e.addBindValue(id);
            QStringList en;
            if (e.exec())
                while (e.next()) en << e.value(0).toString();
            if (en.isEmpty()) {
                addFix(r, bot,
                       QStringLiteral("Квест %1 некому сдать").arg(id),
                       retail,
                       QStringLiteral("INSERT INTO %1 (id, quest) VALUES (/*NPC_ENTRY*/,%2);")
                           .arg(qf(world, QStringLiteral("creature_questender")))
                           .arg(id),
                       world);
            }
        }
    };
    if (ids.isEmpty()) {
        if (hasTable(db, world, QStringLiteral("creature_queststarter"))) {
            QSqlQuery q(db);
            QString sql;
            if (cat.scope.enabled && cat.mapId > 0 && hasTable(db, world, QStringLiteral("creature"))) {
                const QString c = qf(world, QStringLiteral("creature"));
                sql = QStringLiteral(
                    "SELECT DISTINCT q.ID FROM %1 q JOIN ("
                    "SELECT s.quest FROM %2 s JOIN %3 c1 ON c1.id=s.id WHERE c1.map=%4 "
                    "UNION SELECT e.quest FROM %5 e JOIN %3 c2 ON c2.id=e.id WHERE c2.map=%4"
                    ") scoped ON scoped.quest=q.ID LIMIT 50")
                          .arg(qt, qf(world, QStringLiteral("creature_queststarter")), c,
                               QString::number(cat.mapId), qf(world, QStringLiteral("creature_questender")));
            } else if (cat.scope.enabled && cat.scope.zoneId > 0 && hasTable(db, world, QStringLiteral("creature"))
                       && hasCol(db, world, QStringLiteral("creature"), QStringLiteral("zoneId"))) {
                const QString c = qf(world, QStringLiteral("creature"));
                sql = QStringLiteral(
                    "SELECT DISTINCT q.ID FROM %1 q JOIN ("
                    "SELECT s.quest FROM %2 s JOIN %3 c1 ON c1.id=s.id WHERE c1.zoneId=%4 "
                    "UNION SELECT e.quest FROM %5 e JOIN %3 c2 ON c2.id=e.id WHERE c2.zoneId=%4"
                    ") scoped ON scoped.quest=q.ID LIMIT 50")
                          .arg(qt, qf(world, QStringLiteral("creature_queststarter")), c,
                               QString::number(cat.scope.zoneId), qf(world, QStringLiteral("creature_questender")));
            } else {
                sql = QStringLiteral("SELECT q.ID FROM %1 q LEFT JOIN %2 s ON s.quest=q.ID WHERE s.quest IS NULL LIMIT 15")
                          .arg(qt, qf(world, QStringLiteral("creature_queststarter")));
            }
            if (q.exec(sql)) {
                int n = 0;
                while (q.next() && n < 50) {
                    ++n;
                    one(q.value(0).toInt());
                }
                r.report += QStringLiteral("  Квестов в области: проверено %1%2\n")
                                .arg(n)
                                .arg(cat.scope.enabled ? QStringLiteral(" — ") + cat.scope.name : QString());
            }
        }
    } else {
        for (int id : ids) one(id);
    }
    r.report += QLatin1Char('\n');
}

static void botQuarter(QaRunResult &r, QSqlDatabase db, const QString &world,
                       const QString &hotfix, const QList<int> &ids, const QaRetailCatalog &cat) {
    const QString bot = QString::fromUtf8(QaBotService::kQuartermaster);
    const QString retail = QStringLiteral(
        "Ретейл: предмет = Item.db2 + ItemSparse (имя, иконка, статы). "
        "На 6.x+ world.item_template часто пуст: клиент берёт hotfixes.item / item_sparse. "
        "Лут — creature_loot_template; вендор — npc_vendor.");
    r.report += QStringLiteral("—— %1 ——\n").arg(bot);
    auto one = [&](int id) {
        bool sparse = false, item = false, tmpl = false;
        if (hasTable(db, hotfix, QStringLiteral("item_sparse"))) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE ID=?").arg(qf(hotfix, QStringLiteral("item_sparse"))));
            q.addBindValue(id);
            sparse = q.exec() && q.next() && q.value(0).toInt() > 0;
            if (!sparse)
                addFix(r, bot,
                       QStringLiteral("Предмет %1 нет в item_sparse — клиент: Retrieving item info").arg(id),
                       retail,
                       QStringLiteral("-- Импортируйте Wago ItemSparse (все строки) в %1.item_sparse")
                           .arg(hotfix),
                       hotfix);
        }
        if (hasTable(db, hotfix, QStringLiteral("item"))) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE ID=?").arg(qf(hotfix, QStringLiteral("item"))));
            q.addBindValue(id);
            item = q.exec() && q.next() && q.value(0).toInt() > 0;
            if (!item)
                addFix(r, bot, QStringLiteral("Предмет %1 нет в hotfixes.item").arg(id), retail,
                       QStringLiteral("-- Импорт Wago Item → %1.item").arg(hotfix), hotfix);
        }
        if (hasTable(db, world, QStringLiteral("item_template"))) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE entry=?")
                          .arg(qf(world, QStringLiteral("item_template"))));
            q.addBindValue(id);
            tmpl = q.exec() && q.next() && q.value(0).toInt() > 0;
        }
        r.report += QStringLiteral("  item %1: template=%2 item.db2=%3 sparse=%4\n")
                        .arg(id)
                        .arg(tmpl ? QStringLiteral("да") : QStringLiteral("нет"))
                        .arg(item ? QStringLiteral("да") : QStringLiteral("нет"))
                        .arg(sparse ? QStringLiteral("да") : QStringLiteral("нет"));
        if (hasTable(db, world, QStringLiteral("npc_vendor")) && !ids.isEmpty()) {
            QSqlQuery v(db);
            v.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE item=?")
                          .arg(qf(world, QStringLiteral("npc_vendor"))));
            v.addBindValue(id);
            if (v.exec() && v.next())
                r.report += QStringLiteral("    вендоров с этим item: %1\n").arg(v.value(0).toInt());
        }
    };
    if (ids.isEmpty()) {
        if (cat.scope.enabled && cat.mapId > 0 && hasTable(db, world, QStringLiteral("npc_vendor")) && hasTable(db, world, QStringLiteral("creature"))) {
            QSqlQuery q(db);
            q.exec(QStringLiteral("SELECT DISTINCT v.item FROM %1 v JOIN %2 c ON c.id=v.entry WHERE c.map=%3 LIMIT 40")
                       .arg(qf(world, QStringLiteral("npc_vendor")), qf(world, QStringLiteral("creature"))).arg(cat.mapId));
            int n = 0;
            while (q.next() && n < 40) { one(q.value(0).toInt()); ++n; }
            r.report += QStringLiteral("  Вендорные предметы области «%1»: проверено %2.\n").arg(cat.scope.name).arg(n);
        } else {
            r.report += QStringLiteral("  Укажите ID предмета или NPC-вендора в задаче.\n");
        }
    } else {
        for (int id : ids) one(id);
    }
    r.report += QLatin1Char('\n');
}

static void botDirector(QaRunResult &r, QSqlDatabase db, const QString &world,
                        const QString &task, const QList<int> &ids, const QaRetailCatalog &cat) {
    const QString bot = QString::fromUtf8(QaBotService::kDirector);
    const QString retail = QStringLiteral(
        "Ретейл: NPC идёт по заранее записанному пути (клиентские spline / серверные waypoints), "
        "не блуждает по карте. Trinity: MovementType=2 + путь. "
        "SmartAI (smart_scripts) — события/действия. AIName='SmartAI' обязателен, если есть скрипты.");
    r.report += QStringLiteral("—— %1 ——\n").arg(bot);
    const QString cr = qf(world, QStringLiteral("creature"));
    if (!cat.communityReferenceSummary.isEmpty()) {
        r.report += QStringLiteral("  Community: %1\n  Observed/MDT coordinate records: %2\n")
                        .arg(cat.communityReferenceSummary).arg(cat.communityPoints.size());
    }
    const bool hasMove = hasCol(db, world, QStringLiteral("creature"), QStringLiteral("MovementType"));
    const bool hasWp = hasTable(db, world, QStringLiteral("waypoint_data"));
    const bool hasPath = hasTable(db, world, QStringLiteral("waypoint_path"));
    const bool hasSmart = hasTable(db, world, QStringLiteral("smart_scripts"));
    const bool hasTmpl = hasTable(db, world, QStringLiteral("creature_template"));
    const QString low = task.toLower();
    const bool wantPath = low.contains(QStringLiteral("маршрут")) || low.contains(QStringLiteral("траект"))
                          || low.contains(QStringLiteral("waypoint")) || low.contains(QStringLiteral("путь"))
                          || low.contains(QStringLiteral("иди"));

    auto one = [&](int entry) {
        if (hasTmpl) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT AIName FROM %1 WHERE entry=?")
                          .arg(qf(world, QStringLiteral("creature_template"))));
            q.addBindValue(entry);
            QString ai;
            if (q.exec() && q.next()) ai = q.value(0).toString();
            int smartN = 0;
            if (hasSmart) {
                QSqlQuery s(db);
                s.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE entryorguid=? AND source_type=0")
                              .arg(qf(world, QStringLiteral("smart_scripts"))));
                s.addBindValue(entry);
                if (s.exec() && s.next()) smartN = s.value(0).toInt();
            }
            r.report += QStringLiteral("  NPC %1 AIName=%2 smart_scripts=%3\n")
                            .arg(entry)
                            .arg(ai.isEmpty() ? QStringLiteral("—") : ai)
                            .arg(smartN);
            if (smartN > 0 && ai.compare(QStringLiteral("SmartAI"), Qt::CaseInsensitive) != 0) {
                addFix(r, bot,
                       QStringLiteral("NPC %1 имеет SmartAI-скрипты, но AIName≠SmartAI — скрипты не играются").arg(entry),
                       retail,
                       QStringLiteral("UPDATE %1 SET AIName='SmartAI' WHERE entry=%2;")
                           .arg(qf(world, QStringLiteral("creature_template")))
                           .arg(entry),
                       world);
            }
        }
        if (wantPath && hasMove) {
            const QString wcol = wanderCol(db, world);
            addFix(r, bot,
                   QStringLiteral("NPC %1: выключить бродяжничество, включить путь (как на ретейле)").arg(entry),
                   retail,
                   QStringLiteral("UPDATE %1 SET MovementType=2%2 WHERE id=%3;")
                       .arg(cr)
                       .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`=0").arg(wcol))
                       .arg(entry),
                   world);
            if (hasWp) {
                addFix(r, bot,
                       QStringLiteral("NPC %1: заготовка waypoint_data (подставьте точки с ретейла/сниффа)").arg(entry),
                       retail,
                       QStringLiteral(
                           "-- path_id = guid спавна. Точки: point, position_x/y/z, orientation, delay\n"
                           "INSERT INTO %1 (id, point, position_x, position_y, position_z, orientation, delay) VALUES\n"
                           "(/*guid*/, 1, /*x*/, /*y*/, /*z*/, 0, 0),\n"
                           "(/*guid*/, 2, /*x*/, /*y*/, /*z*/, 0, 0);\n"
                           "UPDATE %2 SET path_id=/*guid*/ WHERE guid=/*guid*/;")
                           .arg(qf(world, QStringLiteral("waypoint_data")),
                                hasTable(db, world, QStringLiteral("creature_addon"))
                                    ? qf(world, QStringLiteral("creature_addon"))
                                    : cr),
                       world);
            } else if (hasPath) {
                addFix(r, bot,
                       QStringLiteral("NPC %1: ядро использует waypoint_path — нужен PathId и ноды").arg(entry),
                       retail, QString(), world);
            }
        }
    };
    if (ids.isEmpty()) {
        if (hasSmart && hasTmpl) {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                       "SELECT s.entryorguid, COUNT(*) FROM %1 s "
                       "LEFT JOIN %2 t ON t.entry=s.entryorguid "
                       "WHERE s.source_type=0 AND (t.AIName IS NULL OR t.AIName<>'SmartAI') "
                       "GROUP BY s.entryorguid LIMIT 15")
                       .arg(qf(world, QStringLiteral("smart_scripts")),
                            qf(world, QStringLiteral("creature_template"))));
            int n = 0;
            while (q.next()) {
                ++n;
                const int e = q.value(0).toInt();
                addFix(r, bot,
                       QStringLiteral("entry %1: скрипты есть, AIName не SmartAI").arg(e),
                       retail,
                       QStringLiteral("UPDATE %1 SET AIName='SmartAI' WHERE entry=%2;")
                           .arg(qf(world, QStringLiteral("creature_template")))
                           .arg(e),
                       world);
            }
            r.report += QStringLiteral("  рассинхрон SmartAI (выборка): %1\n").arg(n);
        }
        if (!wantPath)
            r.report += QStringLiteral("  Для маршрута напишите: «NPC 123 идти по траектории» + точки x y z.\n");
    } else {
        for (int id : ids) one(id);
    }
    r.report += QLatin1Char('\n');
}

static QString communityEvidenceFor(const QaRetailCatalog &cat, int spellId) {
    const QString ev = cat.communitySpellEvidence.value(spellId);
    return ev.isEmpty() ? QStringLiteral("нет multi-source evidence") : ev;
}

static void appendCommunityReferenceReport(QaRunResult &r, const QaRetailCatalog &cat) {
    if (cat.communityReferenceSummary.isEmpty()) return;
    r.report += QStringLiteral("—— Community Retail Reference ——\n%1\n")
                    .arg(cat.communityReferenceSummary);
    int common = 0;
    int shown = 0;
    for (auto it = cat.communitySpellEvidence.constBegin(); it != cat.communitySpellEvidence.constEnd(); ++it) {
        if (shown >= 18) break;
        if (!it.value().contains(QLatin1String("score="))) continue;
        const double score = cat.communitySpellScores.value(it.key(), 0.0);
        if (score >= 0.82 && it.value().contains(QLatin1String(","))) {
            ++common;
            r.report += QStringLiteral("  Spell %1: %2\n").arg(it.key()).arg(it.value());
            ++shown;
        }
    }
    r.report += QStringLiteral("  Сильных multi-source spell совпадений: %1; coordinate observations: %2\n\n")
                    .arg(common).arg(cat.communityPoints.size());
}

static void zoneAllBots(QaRunResult &r, QSqlDatabase db, const QString &world,
                        const QString &hotfix, const QString &task, const QaRetailCatalog &cat) {
    if (cat.mapId <= 0 && cat.wowheadZone <= 0) return;
    const int map = cat.mapId;
    const QString cr = qf(world, QStringLiteral("creature"));
    const QString go = qf(world, QStringLiteral("gameobject"));
    const QString wh = QStringLiteral("https://www.wowhead.com/zone=%1").arg(cat.wowheadZone);
    r.report += QStringLiteral("—— Ретейл-источник ——\n%1\n%2\nmap=%3 dungeonMap=%4 wowheadZone=%5\n\n")
                    .arg(cat.source, cat.note)
                    .arg(map)
                    .arg(cat.dungeonMap)
                    .arg(cat.wowheadZone);
    appendCommunityReferenceReport(r, cat);

    // 1 Геодезист: заселённость, входы
    {
        const QString bot = QString::fromUtf8(QaBotService::kGeodesist);
        const QString retail = QStringLiteral(
            "Ретейл: зона имеет карту (Map.db2) и плотность мобов/объектов. "
            "Вход в подземелье — areatrigger / дверь GO, не «дырка в текстуре». "
            "Сравнение с Wowhead: %1").arg(wh);
        if (map > 0 && hasTable(db, world, QStringLiteral("creature"))) {
            const int spawns = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE map=%2").arg(cr).arg(map));
            const int kinds = countWhere(db, QStringLiteral("SELECT COUNT(DISTINCT id) FROM %1 WHERE map=%2").arg(cr).arg(map));
            r.report += QStringLiteral("  Заселённость map=%1: спавнов=%2 типов NPC=%3; Wowhead NPC=%4\n")
                            .arg(map).arg(spawns).arg(kinds).arg(cat.npcs.size());
            if (spawns == 0) {
                addFix(r, bot,
                       QStringLiteral("Локация «%1» (map=%2) пустая: 0 строк world.creature").arg(cat.zoneName).arg(map),
                       retail,
                       QStringLiteral("-- Не ставить NPC в (0,0). Нужен TDB/снифф спавнов map=%1 или координаты с ретейла.\n"
                                      "-- Wago Creature не содержит XYZ.")
                           .arg(map),
                       world);
            } else if (cat.fromWowhead && cat.npcs.size() > 10 && kinds * 4 < cat.npcs.size()) {
                addFix(r, bot,
                       QStringLiteral("Заселённость ниже ретейла: у вас %1 типов NPC, Wowhead %2")
                           .arg(kinds)
                           .arg(cat.npcs.size()),
                       retail, QString(), world);
            }
            const QString wcol = wanderCol(db, world);
            if (hasCol(db, world, QStringLiteral("creature"), QStringLiteral("MovementType")) && !wcol.isEmpty()) {
                addFix(r, bot,
                       QStringLiteral("Остановить широкое бродяжничество на всей «%1»").arg(cat.zoneName),
                       retail,
                       QStringLiteral("UPDATE %1 SET MovementType=0, `%2`=0 WHERE map=%3 AND MovementType=1 AND `%2`>40;")
                           .arg(cr, wcol)
                           .arg(map),
                       world);
            }
        }
        if (cat.dungeonMap > 0) {
            bool inst = hasTable(db, world, QStringLiteral("instance_template"))
                        && rowExists(db, qf(world, QStringLiteral("instance_template")), QStringLiteral("map"), cat.dungeonMap);
            r.report += QStringLiteral("  Инстанс map=%1 в instance_template: %2\n")
                            .arg(cat.dungeonMap)
                            .arg(inst ? QStringLiteral("да") : QStringLiteral("нет"));
            if (!inst && hasTable(db, world, QStringLiteral("instance_template"))) {
                addFix(r, bot,
                       QStringLiteral("Нет входа/инстанса map=%1 (ретейл: подземелье зоны «%2»)")
                           .arg(cat.dungeonMap)
                           .arg(cat.zoneName),
                       retail,
                       QStringLiteral("INSERT INTO %1 (map, parent, script) VALUES (%2, %3, '');")
                           .arg(qf(world, QStringLiteral("instance_template")))
                           .arg(cat.dungeonMap)
                           .arg(map),
                       world);
            }
            if (hasTable(db, world, QStringLiteral("areatrigger_teleport"))) {
                const int at = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE target_map=%2")
                                                    .arg(qf(world, QStringLiteral("areatrigger_teleport")))
                                                    .arg(cat.dungeonMap));
                if (at == 0) {
                    addFix(r, bot,
                           QStringLiteral("Нет areatrigger_teleport на map=%1 — вход в подземелье/рейд не телепортирует")
                               .arg(cat.dungeonMap),
                           retail, QString(), world);
                }
            }
        }
        if (hasTable(db, world, QStringLiteral("areatrigger_teleport")) && map > 0) {
            const int fromHere = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE target_map=%2")
                                                    .arg(qf(world, QStringLiteral("areatrigger_teleport")))
                                                    .arg(map));
            r.report += QStringLiteral("  areatrigger на эту карту (target_map=%1): %2\n").arg(map).arg(fromHere);
        }
    }

    // 2 Бестиарий: все NPC зоны
    {
        const QString bot = QString::fromUtf8(QaBotService::kBestiary);
        const QString retail = QStringLiteral(
            "Ретейл: TDB (world.creature) = XYZ в ярдах. Wowhead рисует те же точки для TomTom: "
            "/way %.1f %.1f — это проценты карты 0..100, не ярды. "
            "Перевод: worldX = locTop - (y/100)*(locTop-locBottom), "
            "worldY = locLeft - (x/100)*(locLeft-locRight) (UiMapAssignment.Region). "
            "Если TDB уже есть — его XYZ главнее. Нет TDB, есть TomTom — ставим по Wowhead. %1")
                                 .arg(wh);
        QaMapBounds bounds = QaRetailSource::boundsFor(cat.wowheadZone, map);
        if (hasTable(db, hotfix, QStringLiteral("uimapassignment"))
            || hasTable(db, hotfix, QStringLiteral("UiMapAssignment"))) {
            const QString ut = hasTable(db, hotfix, QStringLiteral("uimapassignment"))
                                   ? QStringLiteral("uimapassignment")
                                   : QStringLiteral("UiMapAssignment");
            const QString r0 = hasCol(db, hotfix, ut, QStringLiteral("Region_0"))
                                   ? QStringLiteral("Region_0")
                                   : QStringLiteral("Region0");
            const QString r1 = hasCol(db, hotfix, ut, QStringLiteral("Region_1"))
                                   ? QStringLiteral("Region_1")
                                   : QStringLiteral("Region1");
            const QString r3 = hasCol(db, hotfix, ut, QStringLiteral("Region_3"))
                                   ? QStringLiteral("Region_3")
                                   : QStringLiteral("Region3");
            const QString r4 = hasCol(db, hotfix, ut, QStringLiteral("Region_4"))
                                   ? QStringLiteral("Region_4")
                                   : QStringLiteral("Region4");
            QSqlQuery bq(db);
            bq.prepare(QStringLiteral("SELECT `%1`,`%2`,`%3`,`%4` FROM %5 WHERE MapID=? LIMIT 1")
                           .arg(r0, r1, r3, r4, qf(hotfix, ut)));
            bq.addBindValue(map);
            if (bq.exec() && bq.next()) {
                bounds.ok = true;
                bounds.mapId = map;
                bounds.locBottom = bq.value(0).toDouble();
                bounds.locRight = bq.value(1).toDouble();
                bounds.locTop = bq.value(2).toDouble();
                bounds.locLeft = bq.value(3).toDouble();
            }
        }
        double avgZ = 0;
        bool haveAvgZ = false;
        if (map > 0 && hasTable(db, world, QStringLiteral("creature"))) {
            QSqlQuery zq(db);
            if (zq.exec(QStringLiteral("SELECT AVG(position_z) FROM %1 WHERE map=%2 AND ABS(position_x)>1")
                            .arg(cr)
                            .arg(map))
                && zq.next() && !zq.value(0).isNull()) {
                avgZ = zq.value(0).toDouble();
                haveAvgZ = true;
            }
        }
        int missingTpl = 0, missingSpawn = 0, shown = 0, tomtomN = 0, tdbN = 0, wagoN = 0;
        for (const auto &n : cat.npcs) {
            if (n.hasWago) ++wagoN;
            if (n.hasTomTom) ++tomtomN;
            const bool tpl = hasTable(db, world, QStringLiteral("creature_template"))
                             && rowExists(db, qf(world, QStringLiteral("creature_template")), QStringLiteral("entry"), n.id);
            bool spawn = false;
            qint64 tdbGuid = 0;
            double tdbX = 0, tdbY = 0, tdbZ = 0;
            if (map > 0 && hasTable(db, world, QStringLiteral("creature"))) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral(
                    "SELECT guid, position_x, position_y, position_z FROM %1 WHERE id=? AND map=? LIMIT 1")
                              .arg(cr));
                q.addBindValue(n.id);
                q.addBindValue(map);
                if (q.exec() && q.next()) {
                    spawn = true;
                    ++tdbN;
                    tdbGuid = q.value(0).toLongLong();
                    tdbX = q.value(1).toDouble();
                    tdbY = q.value(2).toDouble();
                    tdbZ = q.value(3).toDouble();
                }
            }
            if (spawn && !n.sniffWorldPins.isEmpty() && (n.sniffMap == 0 || n.sniffMap == map)) {
                double best = 1e100;
                for (const auto &pin : n.sniffWorldPins) {
                    const double dx = pin.x - tdbX, dy = pin.y - tdbY, dz = pin.z - tdbZ;
                    best = qMin(best, std::sqrt(dx * dx + dy * dy + dz * dz));
                }
                r.report += QStringLiteral("    sniff consensus: NPC %1 nearest observation %.2f yd (N=%2)\n")
                                .arg(n.id).arg(best, 0, 'f', 2).arg(n.sniffWorldPins.size());
                if (best > 25.0) {
                    addFix(r, bot,
                           QStringLiteral("NPC %1 DB spawn guid=%2 отклонён от ближайшего sniff-наблюдения на %3 yd")
                               .arg(n.id).arg(tdbGuid).arg(best, 0, 'f', 2),
                           QStringLiteral("Sniff observation (%2 точек) имеет приоритет над догадкой из одного источника.").arg(n.sniffWorldPins.size()),
                           QString(), world);
                }
            }
            if (map > 0 && hasTable(db, world, QStringLiteral("creature")) && !n.sniffWorldPins.isEmpty()) {
                QSqlQuery allSpawns(db);
                allSpawns.prepare(QStringLiteral("SELECT guid, position_x, position_y, position_z FROM %1 WHERE id=? AND map=? LIMIT 200").arg(cr));
                allSpawns.addBindValue(n.id);
                allSpawns.addBindValue(map);
                double bestDistance = 1e100; qint64 bestGuid = 0; double bestX = 0, bestY = 0, bestZ = 0;
                int compared = 0;
                if (allSpawns.exec()) {
                    while (allSpawns.next()) {
                        ++compared;
                        const qint64 g = allSpawns.value(0).toLongLong();
                        const double sx = allSpawns.value(1).toDouble(), sy = allSpawns.value(2).toDouble(), sz = allSpawns.value(3).toDouble();
                        for (const auto &pin : n.sniffWorldPins) {
                            const double dx = sx - pin.x, dy = sy - pin.y, dz = sz - pin.z;
                            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                            if (d < bestDistance) { bestDistance = d; bestGuid = g; bestX = sx; bestY = sy; bestZ = sz; }
                        }
                    }
                }
                if (bestGuid) {
                    r.report += QStringLiteral("    multi-source position match: DB guid=%1 ↔ sniff points=%2, nearest=%3 yd at (%4, %5, %6), DB candidates=%7\n")
                                    .arg(bestGuid).arg(n.sniffWorldPins.size()).arg(bestDistance, 0, 'f', 2)
                                    .arg(bestX, 0, 'f', 2).arg(bestY, 0, 'f', 2).arg(bestZ, 0, 'f', 2).arg(compared);
                    if (bestDistance > 25.0) {
                        addFix(r, bot,
                               QStringLiteral("NPC %1: ни один из %2 DB spawn не ближе 25 yd к sniff observations; минимум %3 yd")
                                   .arg(n.id).arg(compared).arg(bestDistance, 0, 'f', 2),
                               QStringLiteral("Сравнение выполнено по всем найденным DB spawn и всем наблюдениям sniff; это сильнее, чем сравнение с одной координатой."),
                               QString(), world);
                    }
                }
            }
            if (!tpl) {
                ++missingTpl;
                if (shown < 25) {
                    ++shown;
                    addFix(r, bot,
                           QStringLiteral("Ретейл NPC %1 %2 нет в creature_template")
                               .arg(n.id)
                               .arg(n.name.isEmpty() ? QString() : n.name),
                           retail,
                           QStringLiteral("INSERT INTO %1 (entry, name, npcflag, type, faction) VALUES (%2,%3,0,7,35);")
                               .arg(qf(world, QStringLiteral("creature_template")))
                               .arg(n.id)
                               .arg(sqlLit(n.name.isEmpty() ? QStringLiteral("TODO") : n.name)),
                           world);
                }
            } else if (spawn && n.hasTomTom) {
                double wx = 0, wy = 0;
                if (QaRetailSource::tomtomToWorld(bounds, n.tomtomX, n.tomtomY, &wx, &wy)) {
                    const double dx = wx - tdbX, dy = wy - tdbY;
                    if (dx * dx + dy * dy > 2500.0 && shown < 15) {
                        ++shown;
                        addFix(r, bot,
                               QStringLiteral("NPC %1 TDB guid=%2 xyz=(%3,%4,%5) далеко от TomTom /way %6 %7 → (%8,%9)")
                                   .arg(n.id)
                                   .arg(tdbGuid)
                                   .arg(tdbX, 0, 'f', 1)
                                   .arg(tdbY, 0, 'f', 1)
                                   .arg(tdbZ, 0, 'f', 1)
                                   .arg(n.tomtomX, 0, 'f', 1)
                                   .arg(n.tomtomY, 0, 'f', 1)
                                   .arg(wx, 0, 'f', 1)
                                   .arg(wy, 0, 'f', 1),
                               retail,
                               QStringLiteral("UPDATE %1 SET position_x=%2, position_y=%3 WHERE guid=%4;")
                                   .arg(cr)
                                   .arg(wx, 0, 'f', 4)
                                   .arg(wy, 0, 'f', 4)
                                   .arg(tdbGuid),
                               world);
                    }
                }
            } else if (!spawn && map > 0) {
                ++missingSpawn;
                double wx = 0, wy = 0, wz = haveAvgZ ? avgZ : 0.0, wo = 0;
                QString how;
                bool conv = false;
                if (n.hasWorld) {
                    conv = true;
                    wx = n.worldX;
                    wy = n.worldY;
                    wz = n.worldZ;
                    wo = n.worldO;
                    how = QStringLiteral("снифф/WPP ярды");
                } else if (n.hasTomTom
                           && QaRetailSource::tomtomToWorld(bounds, n.tomtomX, n.tomtomY, &wx, &wy)) {
                    conv = true;
                    how = QStringLiteral("TomTom /way");
                }
                if (shown < 40) {
                    ++shown;
                    addFix(r, bot,
                           conv ? QStringLiteral("Нет TDB-спавна NPC %1 на map=%2. %3 → (%4, %5, %6)")
                                       .arg(n.id)
                                       .arg(map)
                                       .arg(how)
                                       .arg(wx, 0, 'f', 2)
                                       .arg(wy, 0, 'f', 2)
                                       .arg(wz, 0, 'f', 2)
                                : QStringLiteral("NPC %1 нет спавна на map=%2 (нет сниффа и TomTom)")
                                      .arg(n.id)
                                      .arg(map),
                           retail,
                           conv ? QStringLiteral(
                                      "INSERT INTO %1 (`guid`,`id`,`map`,`position_x`,`position_y`,`position_z`,`orientation`) "
                                      "SELECT g, %2, %3, %4, %5, %6, %7 FROM (SELECT IFNULL(MAX(`guid`),0)+1 AS g FROM %1) s;")
                                      .arg(cr)
                                      .arg(n.id)
                                      .arg(n.sniffMap > 0 ? n.sniffMap : map)
                                      .arg(wx, 0, 'f', 4)
                                      .arg(wy, 0, 'f', 4)
                                      .arg(wz, 0, 'f', 4)
                                      .arg(wo, 0, 'f', 4)
                                : QString(),
                           world);
                }
            }
        }
        r.report += QStringLiteral(
                        "  Retail NPC=%1 Wago-описано=%2 TomTom=%3 TDB-спавнов=%4 нет шаблона=%5 нет спавна=%6 "
                        "(Wago = client DB2 identity/display; TDB/sniff = world facts; TomTom = fallback)\n")
                        .arg(cat.npcs.size())
                        .arg(wagoN)
                        .arg(tomtomN)
                        .arg(tdbN)
                        .arg(missingTpl)
                        .arg(missingSpawn);
        if (!cat.fromWowhead && cat.npcs.isEmpty()) {
            addFix(r, bot,
                   QStringLiteral("Нет списка NPC с Wowhead — откройте %1 (кэш) и запустите снова").arg(wh),
                   retail, QString(), world);
        }
    }

    // 3 Квестолог: квесты + цепочки
    {
        const QString bot = QString::fromUtf8(QaBotService::kQuest);
        const QString retail = QStringLiteral(
            "Ретейл: цепочка Quest.db2 / Wowhead series. Trinity: quest_template_addon.PrevQuestID / NextQuestID "
            "(после какого квеста появляется следующий). Стартер обязателен.");
        const bool hasQt = hasTable(db, world, QStringLiteral("quest_template"));
        const bool hasAd = hasTable(db, world, QStringLiteral("quest_template_addon"));
        int miss = 0, shown = 0;
        for (const auto &qst : cat.quests) {
            if (!hasQt) break;
            if (!rowExists(db, qf(world, QStringLiteral("quest_template")), QStringLiteral("ID"), qst.id)
                && !rowExists(db, qf(world, QStringLiteral("quest_template")), QStringLiteral("id"), qst.id)) {
                ++miss;
                if (shown < 20) {
                    ++shown;
                    addFix(r, bot,
                           QStringLiteral("Квест ретейла %1 %2 нет в quest_template")
                               .arg(qst.id)
                               .arg(qst.name),
                           retail,
                           QStringLiteral("-- Импорт TDB/Wago Quest %1. Не выдумывать objectives.\n"
                                          "-- https://www.wowhead.com/quest=%1")
                               .arg(qst.id),
                           world);
                }
            }
        }
        r.report += QStringLiteral("  Wowhead квестов=%1 отсутствуют локально=%2\n").arg(cat.quests.size()).arg(miss);
        if (hasAd && hasQt) {
            const QString idc = hasCol(db, world, QStringLiteral("quest_template_addon"), QStringLiteral("ID"))
                                    ? QStringLiteral("ID")
                                    : QStringLiteral("id");
            QString prevC = hasCol(db, world, QStringLiteral("quest_template_addon"), QStringLiteral("PrevQuestID"))
                                ? QStringLiteral("PrevQuestID")
                                : QString();
            QString nextC = hasCol(db, world, QStringLiteral("quest_template_addon"), QStringLiteral("NextQuestID"))
                                ? QStringLiteral("NextQuestID")
                                : QString();
            if (!prevC.isEmpty() && !cat.quests.isEmpty()) {
                int chained = 0;
                QSqlQuery q(db);
                q.exec(QStringLiteral("SELECT `%1`,`%2`,`%3` FROM %4 WHERE `%2`<>0 OR `%3`<>0 LIMIT 30")
                           .arg(idc, prevC, nextC.isEmpty() ? prevC : nextC,
                                qf(world, QStringLiteral("quest_template_addon"))));
                r.report += QStringLiteral("  Примеры цепочек (PrevQuestID → квест → NextQuestID):\n");
                while (q.next() && chained < 12) {
                    r.report += QStringLiteral("    %1 → %2 → %3\n")
                                    .arg(q.value(1).toInt())
                                    .arg(q.value(0).toInt())
                                    .arg(nextC.isEmpty() ? 0 : q.value(2).toInt());
                    ++chained;
                }
                addFix(r, bot,
                       QStringLiteral("Цепочки: смотрите quest_template_addon.PrevQuestID (после какого квеста появляется этот)"),
                       retail,
                       QStringLiteral("-- Пример: квест B появляется после A:\n"
                                      "UPDATE %1 SET PrevQuestID=/*A*/ WHERE `%2`=/*B*/;")
                           .arg(qf(world, QStringLiteral("quest_template_addon")), idc),
                       world);
            }
        }
        if (hasTable(db, world, QStringLiteral("creature_queststarter")) && map > 0
            && hasTable(db, world, QStringLiteral("creature"))) {
            const int orphans = countWhere(db, QStringLiteral(
                "SELECT COUNT(*) FROM %1 s LEFT JOIN %2 c ON c.id=s.id AND c.map=%3 "
                "WHERE c.id IS NULL")
                .arg(qf(world, QStringLiteral("creature_queststarter")), cr)
                .arg(map));
            Q_UNUSED(orphans);
        }
    }

    // 4 Интендант: объекты, способности
    {
        const QString bot = QString::fromUtf8(QaBotService::kQuartermaster);
        const QString retail = QStringLiteral(
            "Ретейл: объекты зоны — GameObjects.db2 / Wowhead objects. "
            "Способности классов — SkillLineAbility + Spell, на сервере playercreateinfo_spell / skill_line_ability.");
        int missGo = 0, shown = 0;
        const bool hasGot = hasTable(db, world, QStringLiteral("gameobject_template"));
        for (const auto &o : cat.objects) {
            if (!hasGot) break;
            const QString col = hasCol(db, world, QStringLiteral("gameobject_template"), QStringLiteral("entry"))
                                    ? QStringLiteral("entry")
                                    : QStringLiteral("ID");
            if (!rowExists(db, qf(world, QStringLiteral("gameobject_template")), col, o.id)) {
                ++missGo;
                if (shown < 15) {
                    ++shown;
                    addFix(r, bot,
                           QStringLiteral("Объект ретейла %1 нет в gameobject_template").arg(o.id),
                           retail,
                           QStringLiteral("-- https://www.wowhead.com/object=%1 — импорт TDB/Wago GameObjects").arg(o.id),
                           world);
                }
            } else if (map > 0 && hasTable(db, world, QStringLiteral("gameobject"))) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE id=? AND map=? LIMIT 1").arg(go));
                q.addBindValue(o.id);
                q.addBindValue(map);
                if (!(q.exec() && q.next()) && shown < 20) {
                    ++shown;
                    addFix(r, bot,
                           QStringLiteral("Объект %1 не заспавнен на map=%2").arg(o.id).arg(map),
                           retail, QString(), world);
                }
            }
        }
        int goN = -1;
        if (map > 0 && hasTable(db, world, QStringLiteral("gameobject")))
            goN = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE map=%2").arg(go).arg(map));
        r.report += QStringLiteral("  Объекты: Wowhead=%1 нет шаблона=%2 спавнов на карте=%3\n")
                        .arg(cat.objects.size())
                        .arg(missGo)
                        .arg(goN);

        const QString low = task.toLower();
        const bool wantClass = cat.scope.includePlayerClasses && (low.contains(QStringLiteral("способн")) || low.contains(QStringLiteral("класс"))
                               || low.contains(QLatin1String("spell")) || low.contains(QStringLiteral("талант")));
        if (wantClass) {
            const bool pcs = hasTable(db, world, QStringLiteral("playercreateinfo_spell"));
            const bool sla = hasTable(db, world, QStringLiteral("skill_line_ability"))
                             || hasTable(db, hotfix, QStringLiteral("skilllineability"))
                             || hasTable(db, hotfix, QStringLiteral("skill_line_ability"));
            r.report += QStringLiteral("  Способности: playercreateinfo_spell=%1 skill_line_ability=%2 wowhead spells=%3\n")
                            .arg(pcs ? QStringLiteral("есть") : QStringLiteral("нет"))
                            .arg(sla ? QStringLiteral("есть") : QStringLiteral("нет"))
                            .arg(cat.spells.size());
            int evShown = 0;
            for (const auto &sp : cat.spells) {
                const QString ev = communityEvidenceFor(cat, sp.id);
                if (ev == QLatin1String("нет multi-source evidence")) continue;
                if (evShown++ >= 20) break;
                r.report += QStringLiteral("    Spell %1: %2\n").arg(sp.id).arg(ev);
            }
            if (!pcs) {
                addFix(r, bot,
                       QStringLiteral("Нет playercreateinfo_spell — стартовые способности классов пусты"),
                       retail, QString(), world);
            } else {
                const int n = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1")
                                                   .arg(qf(world, QStringLiteral("playercreateinfo_spell"))));
                if (n == 0)
                    addFix(r, bot, QStringLiteral("playercreateinfo_spell пуст"), retail, QString(), world);
            }
            addFix(r, bot,
                   QStringLiteral("Способности классов на ретейле: импорт Wago SkillLineAbility + SpellName в hotfixes"),
                   retail,
                   QStringLiteral("-- Вкладка Wago/DB2: SkillLineAbility, SpellName, Spell. "
                                  "Wowhead: https://www.wowhead.com/spells/class-abilities"),
                   hotfix);
        }
    }

    // 5 Режиссёр: движение NPC зоны, входы
    {
        const QString bot = QString::fromUtf8(QaBotService::kDirector);
        const QString retail = QStringLiteral(
            "Ретейл: мобы зоны либо стоят, либо идут по spline. Вход в данж — триггер + instance_template + скрипт инстанса.");
        if (map > 0 && hasTable(db, world, QStringLiteral("creature"))
            && hasCol(db, world, QStringLiteral("creature"), QStringLiteral("MovementType"))) {
            const int idle = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE map=%2 AND MovementType=0").arg(cr).arg(map));
            const int rnd = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE map=%2 AND MovementType=1").arg(cr).arg(map));
            const int wp = countWhere(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE map=%2 AND MovementType=2").arg(cr).arg(map));
            r.report += QStringLiteral("  Движение на map=%1: стоят=%2 бродят=%3 путь=%4\n").arg(map).arg(idle).arg(rnd).arg(wp);
            if (rnd > idle && idle + rnd > 10) {
                addFix(r, bot,
                       QStringLiteral("На «%1» бродящих больше, чем стоящих (%2 vs %3) — не как на ретейле")
                           .arg(cat.zoneName)
                           .arg(rnd)
                           .arg(idle),
                       retail,
                       QStringLiteral("UPDATE %1 SET MovementType=0 WHERE map=%2 AND MovementType=1;")
                           .arg(cr)
                           .arg(map),
                       world);
            }
            const QString wcol = wanderCol(db, world);
            const bool hasWp = hasTable(db, world, QStringLiteral("waypoint_data"));
            const bool hasAddon = hasTable(db, world, QStringLiteral("creature_addon"));
            QaMapBounds bb = QaRetailSource::boundsFor(cat.wowheadZone, map);
            auto isPatrolPins = [](const QVector<QaTomTomPin> &pins) {
                if (pins.size() < 3) return false;
                int close = 0;
                for (int i = 1; i < pins.size(); ++i) {
                    const double dx = pins[i].x - pins[i - 1].x;
                    const double dy = pins[i].y - pins[i - 1].y;
                    const double d = std::sqrt(dx * dx + dy * dy);
                    if (d > 0.25 && d < 8.0) ++close;
                }
                return close >= 2 && close >= pins.size() - 2;
            };
            int shownMove = 0;
            for (const auto &n : cat.npcs) {
                if (shownMove >= 25) break;
                QSqlQuery cq(db);
                cq.prepare(QStringLiteral("SELECT guid, MovementType%1 FROM %2 WHERE id=? AND map=? LIMIT 3")
                               .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`").arg(wcol), cr));
                cq.addBindValue(n.id);
                cq.addBindValue(map);
                if (!cq.exec()) continue;
                while (cq.next() && shownMove < 25) {
                    const qint64 guid = cq.value(0).toLongLong();
                    const int mt = cq.value(1).toInt();
                    int pathId = 0;
                    if (hasAddon && hasCol(db, world, QStringLiteral("creature_addon"), QStringLiteral("path_id"))) {
                        QSqlQuery aq(db);
                        aq.prepare(QStringLiteral("SELECT path_id FROM %1 WHERE guid=?")
                                       .arg(qf(world, QStringLiteral("creature_addon"))));
                        aq.addBindValue(guid);
                        if (aq.exec() && aq.next()) pathId = aq.value(0).toInt();
                    }
                    int wpN = 0;
                    if (pathId > 0 && hasWp) {
                        QSqlQuery wq(db);
                        wq.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE id=?")
                                       .arg(qf(world, QStringLiteral("waypoint_data"))));
                        wq.addBindValue(pathId);
                        if (wq.exec() && wq.next()) wpN = wq.value(0).toInt();
                    }
                    const bool tdbPatrol = (mt == 2) || (pathId > 0 && wpN > 0);
                    const bool sniffPatrol = (n.sniffMove == 2) || (n.sniffWaypoints.size() >= 2);
                    const bool wowPatrol = isPatrolPins(n.pins);
                    QString verdict;
                    if (tdbPatrol)
                        verdict = QStringLiteral("патруль (TDB MovementType=2 / waypoint_data %1 точек)").arg(wpN);
                    else if (sniffPatrol)
                        verdict = QStringLiteral("патруль (снифф WPP MovementType=%1 точек пути=%2)")
                                      .arg(n.sniffMove)
                                      .arg(n.sniffWaypoints.size());
                    else if (wowPatrol)
                        verdict = QStringLiteral("патруль (Wowhead/TomTom %1 точек цепочкой)").arg(n.pins.size());
                    else if (mt == 1)
                        verdict = QStringLiteral("бродит случайно (MovementType=1) — на ретейле патруль так не делают");
                    else
                        verdict = QStringLiteral("стоит (одна точка / MovementType=0)");
                    r.report += QStringLiteral("    NPC %1 guid=%2: %3\n").arg(n.id).arg(guid).arg(verdict);

                    if ((sniffPatrol || wowPatrol) && !tdbPatrol) {
                        ++shownMove;
                        QString sql = QStringLiteral("UPDATE %1 SET MovementType=2%2 WHERE guid=%3;\n")
                                          .arg(cr)
                                          .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`=0").arg(wcol))
                                          .arg(guid);
                        if (hasAddon)
                            sql += QStringLiteral(
                                       "INSERT INTO %1 (guid, path_id) VALUES (%2, %2) "
                                       "ON DUPLICATE KEY UPDATE path_id=%2;\n")
                                       .arg(qf(world, QStringLiteral("creature_addon")))
                                       .arg(guid);
                        if (hasWp) {
                            sql += QStringLiteral("DELETE FROM %1 WHERE id=%2;\n")
                                       .arg(qf(world, QStringLiteral("waypoint_data")))
                                       .arg(guid);
                            int pt = 1;
                            if (!n.sniffWaypoints.isEmpty()) {
                                for (const QaWorldPin &pin : n.sniffWaypoints) {
                                    sql += QStringLiteral(
                                               "INSERT INTO %1 (id, point, position_x, position_y, position_z, orientation, delay) "
                                               "VALUES (%2, %3, %4, %5, %6, %7, 0);\n")
                                               .arg(qf(world, QStringLiteral("waypoint_data")))
                                               .arg(guid)
                                               .arg(pt++)
                                               .arg(pin.x, 0, 'f', 4)
                                               .arg(pin.y, 0, 'f', 4)
                                               .arg(pin.z, 0, 'f', 4)
                                               .arg(pin.o, 0, 'f', 4);
                                }
                            } else {
                                for (const QaTomTomPin &pin : n.pins) {
                                    double wx = 0, wy = 0;
                                    if (!QaRetailSource::tomtomToWorld(bb, pin.x, pin.y, &wx, &wy)) continue;
                                    sql += QStringLiteral(
                                               "INSERT INTO %1 (id, point, position_x, position_y, position_z, orientation, delay) "
                                               "VALUES (%2, %3, %4, %5, 0, 0, 0);\n")
                                               .arg(qf(world, QStringLiteral("waypoint_data")))
                                               .arg(guid)
                                               .arg(pt++)
                                               .arg(wx, 0, 'f', 4)
                                               .arg(wy, 0, 'f', 4);
                                }
                            }
                        }
                        addFix(r, bot,
                               QStringLiteral("NPC %1 guid=%2 должен патрулировать (%3), в TDB не путь")
                                   .arg(n.id)
                                   .arg(guid)
                                   .arg(sniffPatrol ? QStringLiteral("снифф") : QStringLiteral("TomTom")),
                               retail, sql, world);
                    } else if (!wowPatrol && !tdbPatrol && mt == 1) {
                        ++shownMove;
                        addFix(r, bot,
                               QStringLiteral("NPC %1 guid=%2 не патруль: одна точка / нет пути — убрать бродяжничество")
                                   .arg(n.id)
                                   .arg(guid),
                               retail,
                               QStringLiteral("UPDATE %1 SET MovementType=0%2 WHERE guid=%3;")
                                   .arg(cr)
                                   .arg(wcol.isEmpty() ? QString() : QStringLiteral(", `%1`=0").arg(wcol))
                                   .arg(guid),
                               world);
                    } else if (tdbPatrol && mt != 2) {
                        ++shownMove;
                        addFix(r, bot,
                               QStringLiteral("NPC %1 guid=%2 есть waypoint_data, но MovementType≠2 — путь не идёт")
                                   .arg(n.id)
                                   .arg(guid),
                               retail,
                               QStringLiteral("UPDATE %1 SET MovementType=2 WHERE guid=%2;")
                                   .arg(cr)
                                   .arg(guid),
                               world);
                    }
                }
            }
        }
        if (hasTable(db, world, QStringLiteral("access_requirement")) && cat.dungeonMap > 0) {
            if (!rowExists(db, qf(world, QStringLiteral("access_requirement")), QStringLiteral("mapId"), cat.dungeonMap)
                && !rowExists(db, qf(world, QStringLiteral("access_requirement")), QStringLiteral("mapID"), cat.dungeonMap)) {
                addFix(r, bot,
                       QStringLiteral("Нет access_requirement для подземелья map=%1 — уровень/ключ входа").arg(cat.dungeonMap),
                       retail, QString(), world);
            }
        }
    }
}

QaRunResult QaBotService::run(DatabaseService &db, const QString &hotfixSchema,
                              const QString &task, const QString &serverLogs,
                              const QaRetailCatalog &catalog) {
    QaRunResult r;
    if (!db.isOpen()) {
        r.report = QStringLiteral("Сначала подключитесь к MySQL (world). Боты правят world + hotfixes, клиент не нужен.");
        return r;
    }
    const QSqlDatabase sql = db.database();
    const QString world = worldSchema(sql);
    QString hotfix = hotfixSchema;
    if (!identOk(hotfix)) hotfix = QStringLiteral("hotfixes");
    const QString t = task.trimmed();
    QaRetailCatalog scoped = catalog;
    if (scoped.scope.enabled && scoped.mapId <= 0 && scoped.scope.mapId > 0) scoped.mapId = scoped.scope.mapId;
    if (scoped.scope.enabled && scoped.mapId <= 0 && scoped.scope.zoneId > 0 && hasTable(sql, world, QStringLiteral("creature")) && hasCol(sql, world, QStringLiteral("creature"), QStringLiteral("zoneId"))) {
        QSqlQuery mq(sql);
        mq.prepare(QStringLiteral("SELECT map FROM %1 WHERE zoneId=? GROUP BY map ORDER BY COUNT(*) DESC LIMIT 1").arg(qf(world, QStringLiteral("creature"))));
        mq.addBindValue(scoped.scope.zoneId);
        if (mq.exec() && mq.next()) scoped.mapId = mq.value(0).toInt();
    }
    if (scoped.scope.enabled && scoped.scope.name.isEmpty()) scoped.scope.name = scoped.zoneName;
    if (scoped.scope.enabled && (scoped.scope.kind == QLatin1String("dungeon") || scoped.scope.kind == QLatin1String("raid"))
        && scoped.dungeonMap <= 0 && scoped.mapId > 0)
        scoped.dungeonMap = scoped.mapId;
    if (scoped.scope.enabled && scoped.mapId > 0 && !scoped.communityPoints.isEmpty()) {
        QVector<QaCommunityPoint> filtered;
        filtered.reserve(scoped.communityPoints.size());
        for (const auto &point : scoped.communityPoints)
            if (point.mapId == 0 || point.mapId == scoped.mapId || point.mapId == scoped.dungeonMap) filtered.push_back(point);
        scoped.communityPoints = filtered;
    }
    QList<int> ids = extractIds(t);
    if (scoped.mapId) ids.removeAll(scoped.mapId);
    if (scoped.wowheadZone) ids.removeAll(scoped.wowheadZone);
    if (scoped.dungeonMap) ids.removeAll(scoped.dungeonMap);
    const ParsedPos pos = parsePos(t);
    r.report += QStringLiteral(
                    "5 ботов. Источники: WPP/sniff > TDB > community consensus (MDT/DBM/BigWigs/LittleWigs/ATT) > Wago; внешние wowdata/wow.export/WCL учитываются как дополнительные reference-провайдеры после подключения.\n"
                    "Задача: %1\nworld=`%2` hotfixes=`%3`\n")
                    .arg(t.isEmpty() ? QStringLiteral("(аудит)") : t, world, hotfix);
    if (scoped.scope.enabled) {
        r.report += QStringLiteral("Область проверки: [%1] %2 map=%3 zone=%4 dungeon=%5; классы/таланты=%6\n\n")
                        .arg(scoped.scope.kind, scoped.scope.name.isEmpty() ? scoped.zoneName : scoped.scope.name)
                        .arg(scoped.mapId).arg(scoped.scope.zoneId).arg(scoped.dungeonMap)
                        .arg(scoped.scope.includePlayerClasses ? QStringLiteral("включены") : QStringLiteral("отдельный контур"));
    } else {
        r.report += QLatin1Char('\n');
    }
    if (!catalog.communityReferenceSummary.isEmpty()) {
        r.report += QStringLiteral("Community Reference: %1\n").arg(catalog.communityReferenceSummary);
    }

    if (scoped.scope.includeNpcObjects) {
        zoneAllBots(r, sql, world, hotfix, t, scoped);
        botGeodesist(r, sql, world, t, ids, pos);
        botBestiary(r, sql, world, hotfix, ids);
    }
    if (scoped.scope.includeQuests) botQuest(r, sql, world, ids, scoped);
    if (scoped.scope.includeNpcObjects) botQuarter(r, sql, world, hotfix, ids, scoped);
    if (scoped.scope.includeMovementAndScripts) botDirector(r, sql, world, t, ids, scoped);

    if (!serverLogs.trimmed().isEmpty()) {
        const QString low = serverLogs.toLower();
        if (low.contains(QLatin1String("dberror")) || low.contains(QLatin1String("unknown column"))) {
            addFix(r, QStringLiteral("Режиссёр"),
                   QStringLiteral("В логах ядра есть DBErrors / unknown column — схема не совпала с билдом"),
                   QStringLiteral("Ретейл-клиент и ядро должны быть одной сборки; hotfixes.VerifiedBuild = gamebuild."),
                   QString(), world);
        }
        r.report += QStringLiteral("—— Логи ——\n%1\n").arg(serverLogs.right(1200));
    }

    r.report += QStringLiteral("\nИтого предложений: %1\n").arg(r.fixes.size());
    return r;
}
