#include "qa_sniff_source.h"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

QString QaSniffSource::sourcesHelp() {
    return QStringLiteral(
        "Откуда сниффы (ретейл-пакеты → SQL):\n"
        "1) WowPacketParser (официально для Trinity): https://github.com/TrinityCore/WowPacketParser\n"
        "   Кормите ему .pkt/.bin/.wowpacket → SQL: creature, waypoint_data, gameobject, gossip…\n"
        "2) Как снимать: Trinity wiki «Sniffing» / Using WowPacketParser "
        "(https://trinitycore.info / atlassian wiki Sniffing). Это разбор пакетов, не патч клиента.\n"
        "3) TDB Trinity уже собран из сниффов — world.creature = снифф-ярды, если таблица не пустая.\n"
        "4) AllTheThings: https://github.com/ATTWoWAddon/AllTheThings/releases — в истории есть Harvest 12.1.0.69497. Это "
        "community harvested reference data для NPC/объектов/квестов, но не raw packet sniff.\n"
        "5) Готового CDN «все сниффы 12.x» я не нашёл; для 12.x обычно используют собственные WPP-дампы/парсинги.\n"
        "6) Можно указать HTTPS raw .sql (GitHub raw.githubusercontent.com).\n"
        "Приоритет координат: наблюдаемый sniff > проверенная TDB spawn > TomTom/Wowhead проценты.");
}

static QString unquoteIdent(QString s) {
    s = s.trimmed();
    if (s.startsWith(QLatin1Char('`')) && s.endsWith(QLatin1Char('`')))
        return s.mid(1, s.size() - 2);
    return s;
}

static QStringList splitSqlList(const QString &inner) {
    QStringList out;
    QString cur;
    bool inStr = false;
    int depth = 0;
    for (int i = 0; i < inner.size(); ++i) {
        const QChar c = inner[i];
        if (c == QLatin1Char('\'') && (i == 0 || inner[i - 1] != QLatin1Char('\\')))
            inStr = !inStr;
        if (!inStr) {
            if (c == QLatin1Char('(')) ++depth;
            if (c == QLatin1Char(')')) --depth;
            if (c == QLatin1Char(',') && depth == 0) {
                out << cur.trimmed();
                cur.clear();
                continue;
            }
        }
        cur += c;
    }
    if (!cur.trimmed().isEmpty()) out << cur.trimmed();
    return out;
}

static QString stripSqlVal(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('@'))) return QString(); // @CGUID+1 — пропускаем как число отдельно
    if (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')) && v.size() >= 2)
        return v.mid(1, v.size() - 2);
    return v;
}

static QaRetailId *ensureNpc(QaRetailCatalog *cat, int id) {
    if (id <= 0) return nullptr;
    for (QaRetailId &n : cat->npcs)
        if (n.id == id) return &n;
    if (cat->npcs.size() >= 800) return nullptr;
    QaRetailId n;
    n.id = id;
    cat->npcs.push_back(n);
    return &cat->npcs[cat->npcs.size() - 1];
}

static void applyCreatureRow(QaRetailCatalog *cat, const QStringList &cols, const QStringList &vals,
                             int mapFilter, QHash<int, int> *guidToNpc) {
    QHash<QString, QString> m;
    const int n = qMin(cols.size(), vals.size());
    for (int i = 0; i < n; ++i)
        m[cols[i].toLower()] = stripSqlVal(vals[i]);
    const int id = m.value(QStringLiteral("id"), m.value(QStringLiteral("entry"))).toInt();
    const int map = m.value(QStringLiteral("map")).toInt();
    if (id <= 0) return;
    if (mapFilter > 0 && map > 0 && map != mapFilter) return;
    QaRetailId *npc = ensureNpc(cat, id);
    if (!npc) return;
    const int guid = m.value(QStringLiteral("guid")).toInt();
    if (guid > 0 && guidToNpc) (*guidToNpc)[guid] = id;
    bool okx = false, oky = false;
    const double x = m.value(QStringLiteral("position_x")).toDouble(&okx);
    const double y = m.value(QStringLiteral("position_y")).toDouble(&oky);
    const double z = m.value(QStringLiteral("position_z")).toDouble();
    const double o = m.value(QStringLiteral("orientation")).toDouble();
    if (okx && oky) {
        if (!npc->hasWorld) {
            npc->hasWorld = true;
            npc->worldX = x;
            npc->worldY = y;
            npc->worldZ = z;
            npc->worldO = o;
            npc->sniffMap = map;
        }
        QaWorldPin pin;
        pin.x = x; pin.y = y; pin.z = z; pin.o = o;
        npc->sniffWorldPins.push_back(pin);
        if (npc->sniffWorldPins.size() > 500) npc->sniffWorldPins.removeFirst();
    }
    if (m.contains(QStringLiteral("movementtype")))
        npc->sniffMove = m.value(QStringLiteral("movementtype")).toInt();
}

static void applyWaypointRow(QaRetailCatalog *cat, const QStringList &cols, const QStringList &vals,
                             const QHash<int, int> &pathToNpc) {
    QHash<QString, QString> m;
    const int n = qMin(cols.size(), vals.size());
    for (int i = 0; i < n; ++i)
        m[cols[i].toLower()] = stripSqlVal(vals[i]);
    const int pathId = m.value(QStringLiteral("id")).toInt();
    const int npcId = pathToNpc.value(pathId, 0);
    QaRetailId *npc = npcId ? ensureNpc(cat, npcId) : nullptr;
    // без связи path→npc кладём в waypoints первого npc с guid==id не можем; skip
    if (!npc) return;
    bool okx = false, oky = false;
    const double x = m.value(QStringLiteral("position_x")).toDouble(&okx);
    const double y = m.value(QStringLiteral("position_y")).toDouble(&oky);
    if (!okx || !oky) return;
    npc->sniffWaypoints.push_back({x, y, m.value(QStringLiteral("position_z")).toDouble(),
                                   m.value(QStringLiteral("orientation")).toDouble()});
    if (npc->sniffMove < 0) npc->sniffMove = 2;
}

static void parseInserts(const QString &sql, QaRetailCatalog *cat) {
    const int mapFilter = cat->mapId;
    const auto re = QRegularExpression(
        QStringLiteral("INSERT\\s+INTO\\s+`?([\\w]+)`?\\s*\\(([^)]+)\\)\\s*VALUES\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(sql);
    QHash<int, int> guidToNpc;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString table = m.captured(1).toLower();
        QStringList cols;
        for (const QString &c : m.captured(2).split(QLatin1Char(',')))
            cols << unquoteIdent(c);
        int pos = m.capturedEnd();
        while (pos < sql.size() && sql[pos].isSpace()) ++pos;
        while (pos < sql.size() && sql[pos] != QLatin1Char(';')) {
            if (sql[pos] != QLatin1Char('(')) {
                ++pos;
                continue;
            }
            int depth = 0, start = pos;
            bool inStr = false;
            for (; pos < sql.size(); ++pos) {
                const QChar c = sql[pos];
                if (c == QLatin1Char('\'') && (pos == 0 || sql[pos - 1] != QLatin1Char('\\')))
                    inStr = !inStr;
                if (inStr) continue;
                if (c == QLatin1Char('(')) ++depth;
                else if (c == QLatin1Char(')')) {
                    --depth;
                    if (depth == 0) {
                        const QString inner = sql.mid(start + 1, pos - start - 1);
                        const QStringList vals = splitSqlList(inner);
                        if (table == QLatin1String("creature")) {
                            applyCreatureRow(cat, cols, vals, mapFilter, &guidToNpc);
                        } else if (table == QLatin1String("gameobject")) {
                            QHash<QString, QString> hm;
                            const int nv = qMin(cols.size(), vals.size());
                            for (int i = 0; i < nv; ++i)
                                hm[cols[i].toLower()] = stripSqlVal(vals[i]);
                            const int id = hm.value(QStringLiteral("id")).toInt();
                            if (id > 0) {
                                bool found = false;
                                for (const auto &o : cat->objects)
                                    if (o.id == id) { found = true; break; }
                                if (!found && cat->objects.size() < 500) {
                                    QaRetailId obj;
                                    obj.id = id;
                                    obj.name = QStringLiteral("sniff");
                                    cat->objects.push_back(obj);
                                }
                            }
                        } else if (table == QLatin1String("waypoint_data")
                                   || table == QLatin1String("waypoints")) {
                            applyWaypointRow(cat, cols, vals, guidToNpc);
                        }
                        ++pos;
                        break;
                    }
                }
            }
        }
    }
}

void QaSniffSource::mergeFromText(const QString &sql, const QString &origin, QaRetailCatalog *cat) {
    if (!cat || sql.size() < 20) return;
    const int beforeNpc = cat->npcs.size();
    int beforeWorld = 0;
    for (const auto &n : cat->npcs)
        if (n.hasWorld) ++beforeWorld;
    parseInserts(sql, cat);
    int afterWorld = 0;
    for (const auto &n : cat->npcs)
        if (n.hasWorld) ++afterWorld;
    cat->note += QStringLiteral("\nСнифф %1: NPC всего %2, с ярдами +%3")
                     .arg(origin)
                     .arg(cat->npcs.size() - beforeNpc + beforeNpc)
                     .arg(afterWorld - beforeWorld);
    Q_UNUSED(beforeNpc);
}

void QaSniffSource::mergeFromFolder(const QString &dir, QaRetailCatalog *cat) {
    if (!cat || dir.trimmed().isEmpty()) return;
    QDir d(dir);
    if (!d.exists()) {
        cat->note += QStringLiteral("\nПапка сниффов не найдена: %1").arg(dir);
        return;
    }
    const QStringList files = d.entryList({QStringLiteral("*.sql"), QStringLiteral("*.SQL")},
                                          QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        cat->note += QStringLiteral(
                         "\nВ папке нет .sql (нужен вывод WowPacketParser). %1\n%2")
                         .arg(dir, sourcesHelp());
        return;
    }
    int used = 0;
    for (const QString &fn : files) {
        if (used >= 40) break;
        QFile f(d.filePath(fn));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray raw = f.read(8 * 1024 * 1024); // 8 МБ на файл
        mergeFromText(QString::fromUtf8(raw), fn, cat);
        ++used;
    }
    cat->note += QStringLiteral("\nСнифф-файлов прочитано: %1 из %2").arg(used).arg(files.size());
}
