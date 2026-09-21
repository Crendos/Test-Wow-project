#include "qa_retail_source.h"
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

static QString norm(QString s) {
    s = s.toLower().trimmed();
    s.replace(QLatin1Char('ё'), QLatin1Char('е'));
    s.replace(QRegularExpression(QStringLiteral("[^\\wа-я]+"), QRegularExpression::UseUnicodePropertiesOption),
              QStringLiteral(" "));
    return s.simplified();
}

static QaRetailCatalog gazetteer(const QString &task) {
    struct Row {
        const char *aliases;
        int zone;
        int map;
        int dungeon;
        const char *en;
    };
    // Wowhead zone-id + Trinity map. Остров изгнанников = 10424 / map 2175.
    static const Row kRows[] = {
        {"остров изгнанников остров изгнаников изгнанников изгнаников exile reach exiles reach",
         10424, 2175, 2236, "Exile's Reach"},
        {"darkmaul цитадель темного молота darkmaul citadel", 13309, 2236, 0, "Darkmaul Citadel"},
        {"элвинн elwynn", 12, 0, 0, "Elwynn Forest"},
        {"штормград stormwind", 1519, 0, 0, "Stormwind City"},
        {"дуротар durotar", 14, 1, 0, "Durotar"},
        {"оргримар orgrimmar", 1637, 1, 0, "Orgrimmar"},
        {"тирисфаль tirisfal", 85, 0, 0, "Tirisfal Glades"},
        {"телдрассиль teldrassil", 141, 1, 0, "Teldrassil"},
        {"дун морог dun morogh", 1, 0, 0, "Dun Morogh"},
        {"западный край westfall", 40, 0, 0, "Westfall"},
        {"красные горы redridge", 44, 0, 0, "Redridge Mountains"},
        {"пылевые топи wetlands", 11, 0, 0, "Wetlands"},
        {"лочная лок модан loch modan", 38, 0, 0, "Loch Modan"},
        {"выжженные земли blasted lands", 4, 0, 0, "Blasted Lands"},
        {"стратхольм stratholme", 2017, 329, 0, "Stratholme"},
        {"непройденные пещеры deadmines", 1581, 36, 0, "The Deadmines"},
        {"огненная пропасть ragefire", 2437, 389, 0, "Ragefire Chasm"},
        {"запределье outland hellfire", 3483, 530, 0, "Hellfire Peninsula"},
        {"драконий остров dragon isles", 13644, 2444, 0, "The Waking Shores"},
        {"калимдор kalimdor", 1414, 1, 0, "Kalimdor"},
        {"восточные королевства eastern kingdoms", 1415, 0, 0, "Eastern Kingdoms"},
    };
    const QString n = norm(task);
    QaRetailCatalog c;
    const auto scopeKind = QRegularExpression(QStringLiteral("scope(?:_type|Type)\\s*[:=]\\s*([a-zA-Zа-яА-Я]+)"), QRegularExpression::CaseInsensitiveOption).match(task);
    const auto scopeName = QRegularExpression(QStringLiteral("scope(?:_name|Name)\\s*[:=]\\s*([^\\n;]+)"), QRegularExpression::CaseInsensitiveOption).match(task);
    if (scopeKind.hasMatch() || scopeName.hasMatch()) {
        c.scope.enabled = true;
        c.scope.kind = scopeKind.hasMatch() ? scopeKind.captured(1).trimmed().toLower() : QStringLiteral("zone");
        if (scopeName.hasMatch()) c.scope.name = scopeName.captured(1).trimmed();
    }
    for (const Row &row : kRows) {
        const QString aliases = QString::fromUtf8(row.aliases);
        bool hit = false;
        for (const QString &a : aliases.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (a.size() >= 4 && n.contains(a)) { hit = true; break; }
        }
        if (!hit && n.contains(norm(QString::fromUtf8(row.en)))) hit = true;
        if (!hit) continue;
        c.wowheadZone = row.zone;
        c.mapId = row.map;
        c.dungeonMap = row.dungeon;
        c.zoneName = QString::fromUtf8(row.en);
        c.scope.name = c.zoneName;
        c.source = QStringLiteral("gazetteer");
        break;
    }
    const auto zm = QRegularExpression(QStringLiteral("zone\\s*[:=]\\s*(\\d+)"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(task);
    if (zm.hasMatch()) c.wowheadZone = zm.captured(1).toInt();
    const auto szm = QRegularExpression(QStringLiteral("scope_zone\\s*[:=]\\s*(\\d+)"),
                                        QRegularExpression::CaseInsensitiveOption)
                         .match(task);
    if (szm.hasMatch()) c.scope.zoneId = szm.captured(1).toInt();
    const auto mm = QRegularExpression(QStringLiteral("\\bmap\\s*[:=]\\s*(\\d+)"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(task);
    if (mm.hasMatch()) c.mapId = mm.captured(1).toInt();
    const auto smm = QRegularExpression(QStringLiteral("scope_map\\s*[:=]\\s*(\\d+)"),
                                        QRegularExpression::CaseInsensitiveOption)
                         .match(task);
    if (smm.hasMatch()) c.scope.mapId = smm.captured(1).toInt();
    const QString explicitPlayer = n;
    if (explicitPlayer.contains(QStringLiteral("класс")) || explicitPlayer.contains(QStringLiteral("талант"))
        || explicitPlayer.contains(QLatin1String("class ability")) || explicitPlayer.contains(QLatin1String("player spell")))
        c.scope.includePlayerClasses = true;
    if (c.scope.enabled) {
        if (c.scope.mapId > 0) c.mapId = c.scope.mapId;
        if (c.scope.name.isEmpty()) c.scope.name = c.zoneName;
    }
    return c;
}

static QaRetailId *addId(QVector<QaRetailId> *v, int id, const QString &name) {
    if (id <= 0) return nullptr;
    for (QaRetailId &x : *v) {
        if (x.id != id) continue;
        if (x.name.isEmpty() && !name.isEmpty()) x.name = name;
        return &x;
    }
    if (v->size() >= 500) return nullptr;
    QaRetailId n;
    n.id = id;
    n.name = name;
    v->push_back(n);
    return &(*v)[v->size() - 1];
}

static void setTomTom(QVector<QaRetailId> *v, int id, double x, double y) {
    if (x < 0 || y < 0 || x > 100 || y > 100) return;
    QaRetailId *p = addId(v, id, QString());
    if (!p) return;
    for (const QaTomTomPin &pin : p->pins) {
        const double dx = pin.x - x, dy = pin.y - y;
        if (dx * dx + dy * dy < 0.04) return; // дубль
    }
    if (p->pins.size() >= 40) return;
    p->pins.push_back({x, y});
    if (!p->hasTomTom) {
        p->tomtomX = x;
        p->tomtomY = y;
        p->hasTomTom = true;
    }
}

QaMapBounds QaRetailSource::boundsFor(int wowheadZone, int mapId) {
    QaMapBounds b;
    // UiMapAssignment 12.1.0.69497, UiMapID 1409, MapID 2175 (Exile's Reach).
    if (wowheadZone == 10424 || mapId == 2175) {
        b.ok = true;
        b.mapId = 2175;
        b.locBottom = -820.833984375;
        b.locTop = 1433.3299560547;
        b.locRight = -3900.0;
        b.locLeft = -518.75;
        return b;
    }
    return b;
}

bool QaRetailSource::tomtomToWorld(const QaMapBounds &b, double tx, double ty, double *wx, double *wy) {
    if (!b.ok || !wx || !wy) return false;
    *wx = b.locTop - (ty / 100.0) * (b.locTop - b.locBottom);
    *wy = b.locLeft - (tx / 100.0) * (b.locLeft - b.locRight);
    return true;
}

void QaRetailSource::parseHtml(const QString &html, QaRetailCatalog *cat) {
    auto take = [&](const char *kind, QVector<QaRetailId> *into) {
        const auto re = QRegularExpression(QStringLiteral("/%1=(\\d+)(?:/([\\w\\-]+)?)?")
                                               .arg(QLatin1String(kind)));
        auto it = re.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            addId(into, m.captured(1).toInt(), m.captured(2));
        }
    };
    take("npc", &cat->npcs);
    take("quest", &cat->quests);
    take("object", &cat->objects);
    take("spell", &cat->spells);
    take("zone", &cat->instances);

    // WH.Gatherer.addData(3, 1, { "156280": { ... name_enus:"..." })
    const auto gre = QRegularExpression(
        QStringLiteral("addData\\(\\s*(\\d+)\\s*,\\s*\\d+\\s*,\\s*\\{([^}]{0,800000})\\}"),
        QRegularExpression::DotMatchesEverythingOption);
    auto git = gre.globalMatch(html);
    while (git.hasNext()) {
        const auto m = git.next();
        const int type = m.captured(1).toInt();
        QVector<QaRetailId> *into = nullptr;
        if (type == 3) into = &cat->npcs;
        else if (type == 5) into = &cat->quests;
        else if (type == 2) into = &cat->objects;
        else if (type == 6) into = &cat->spells;
        else if (type == 7) into = &cat->instances;
        if (!into) continue;
        auto iit = QRegularExpression(QStringLiteral("\"(\\d+)\"\\s*:")).globalMatch(m.captured(2));
        while (iit.hasNext()) addId(into, iit.next().captured(1).toInt(), QString());
    }

    const auto js = QRegularExpression::DotMatchesEverythingOption;
    auto pin = [&](QVector<QaRetailId> *into, const QRegularExpression &re, int idG, int xG, int yG) {
        auto it = re.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            setTomTom(into, m.captured(idG).toInt(), m.captured(xG).toDouble(), m.captured(yG).toDouble());
        }
    };
    pin(&cat->npcs,
        QRegularExpression(QStringLiteral("id\\s*[:=]\\s*\"?(\\d+)\"?[\\s\\S]{0,400}?coords\\s*[:=]\\s*\\[\\s*\\[\\s*([0-9.]+)\\s*,\\s*([0-9.]+)"), js),
        1, 2, 3);
    pin(&cat->npcs,
        QRegularExpression(QStringLiteral("coords\\s*[:=]\\s*\\[\\s*\\[\\s*([0-9.]+)\\s*,\\s*([0-9.]+)[\\s\\S]{0,400}?id\\s*[:=]\\s*\"?(\\d+)"), js),
        3, 1, 2);
    pin(&cat->npcs,
        QRegularExpression(QStringLiteral("\\[\\s*([0-9.]+)\\s*,\\s*([0-9.]+)[^\\]]{0,120}npc=?\\s*[:=]?\\s*(\\d+)"), js),
        3, 1, 2);
    pin(&cat->npcs,
        QRegularExpression(QStringLiteral("/npc=(\\d+)[\\s\\S]{0,200}?/way\\s+([0-9.]+)\\s+([0-9.]+)"), js),
        1, 2, 3);
    {
        const auto blk = QRegularExpression(
            QStringLiteral("id\\s*[:=]\\s*\"?(\\d+)\"?[\\s\\S]{0,160}?coords\\s*[:=]\\s*\\[([^\\]]{0,8000}\\])"), js);
        auto it = blk.globalMatch(html);
        const auto pairRe = QRegularExpression(QStringLiteral("\\[\\s*([0-9.]+)\\s*,\\s*([0-9.]+)"));
        while (it.hasNext()) {
            const auto m = it.next();
            const int id = m.captured(1).toInt();
            auto pit = pairRe.globalMatch(m.captured(2));
            while (pit.hasNext()) {
                const auto p = pit.next();
                setTomTom(&cat->npcs, id, p.captured(1).toDouble(), p.captured(2).toDouble());
            }
        }
    }
}

QaRetailSource::QaRetailSource(QObject *parent) : QObject(parent) {}

QaRetailCatalog QaRetailSource::resolveTask(const QString &task) {
    return gazetteer(task);
}

void QaRetailSource::fetchForTask(const QString &task) {
    QaRetailCatalog seed = gazetteer(task);
    if (seed.wowheadZone <= 0) {
        seed.note = QStringLiteral("Зона не распознана — сверка с Wowhead по зоне пропущена. "
                                   "Напишите название (напр. «Остров изгнанников») или zone=10424 map=2175.");
        emit ready(seed);
        return;
    }
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/qa-wowhead");
    QDir().mkpath(cacheDir);
    const QString cachePath = cacheDir + QStringLiteral("/zone-%1.html").arg(seed.wowheadZone);
    QFile cf(cachePath);
    if (cf.exists() && cf.size() > 4000 && cf.open(QIODevice::ReadOnly)) {
        const QString html = QString::fromUtf8(cf.readAll());
        cf.close();
        parseHtml(html, &seed);
        seed.fromWowhead = !seed.npcs.isEmpty() || !seed.quests.isEmpty();
        seed.source = QStringLiteral("wowhead-cache %1").arg(cachePath);
        int tt = 0;
        for (const auto &n : seed.npcs)
            if (n.hasTomTom) ++tt;
        seed.note = QStringLiteral("Кэш Wowhead. NPC=%1 (TomTom точек=%2) квесты=%3 объекты=%4")
                        .arg(seed.npcs.size())
                        .arg(tt)
                        .arg(seed.quests.size())
                        .arg(seed.objects.size());
        emit ready(seed);
        return;
    }
    const QUrl url(QStringLiteral("https://www.wowhead.com/zone=%1").arg(seed.wowheadZone));
    seed.source = url.toString();
    httpGet(url, seed);
}

void QaRetailSource::httpGet(const QUrl &url, const QaRetailCatalog &seed) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "ru,en-US;q=0.9,en;q=0.8");
    req.setRawHeader("Accept-Encoding", "identity");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setTransferTimeout(25000);
    auto *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seed, url] {
        QaRetailCatalog cat = seed;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        const QString errText = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError || http >= 400 || body.size() < 2000) {
            cat.note = QStringLiteral(
                           "Wowhead недоступен (HTTP %1, %2 байт, %3). "
                           "Зона «%4» map=%5 zone=%6 — сверка по world/hotfixes. "
                           "Когда страница откроется, кэш сохранится: %7")
                           .arg(http)
                           .arg(body.size())
                           .arg(errText)
                           .arg(cat.zoneName)
                           .arg(cat.mapId)
                           .arg(cat.wowheadZone)
                           .arg(url.toString());
            emit ready(cat);
            return;
        }
        const QString html = QString::fromUtf8(body);
        parseHtml(html, &cat);
        cat.fromWowhead = !cat.npcs.isEmpty() || !cat.quests.isEmpty() || !cat.objects.isEmpty();
        const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 + QStringLiteral("/qa-wowhead");
        QDir().mkpath(cacheDir);
        QFile cf(cacheDir + QStringLiteral("/zone-%1.html").arg(cat.wowheadZone));
        if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cf.write(body);
            cf.close();
        }
        int tt = 0;
        for (const auto &n : cat.npcs)
            if (n.hasTomTom) ++tt;
        cat.note = QStringLiteral("Wowhead %1 — NPC %2 (TomTom=%3), квесты %4, объекты %5")
                       .arg(url.toString())
                       .arg(cat.npcs.size())
                       .arg(tt)
                       .arg(cat.quests.size())
                       .arg(cat.objects.size());
        emit ready(cat);
    });
}
