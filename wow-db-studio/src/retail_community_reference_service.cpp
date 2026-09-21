#include "retail_community_reference_service.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>

RetailCommunityReferenceService::RetailCommunityReferenceService(QObject *parent) : QObject(parent) {
    m_repos = {
        {QStringLiteral("MDT"), QStringLiteral("Nnoggie"), QStringLiteral("MythicDungeonTools"), QStringLiteral("master"), QStringLiteral("Midnight/")},
        {QStringLiteral("DBM"), QStringLiteral("DeadlyBossMods"), QStringLiteral("DBM-Dungeons"), QStringLiteral("master"), QStringLiteral("Midnight/")},
        {QStringLiteral("LittleWigs"), QStringLiteral("BigWigsMods"), QStringLiteral("LittleWigs"), QStringLiteral("master"), QStringLiteral("Midnight/")},
        {QStringLiteral("BigWigs"), QStringLiteral("BigWigsMods"), QStringLiteral("BigWigs"), QStringLiteral("master"), QStringLiteral("Midnight/")},
        {QStringLiteral("ATT"), QStringLiteral("ATTWoWAddon"), QStringLiteral("AllTheThings"), QStringLiteral("master"), QStringLiteral("db/")},
        {QStringLiteral("KeystoneGuru"), QStringLiteral("RaiderIO"), QStringLiteral("keystone.guru"), QStringLiteral("master"), QStringLiteral("database/seeders/dungeondata/midnight/")}
    };
}

QString RetailCommunityReferenceService::defaultBuild() { return QStringLiteral("12.1.0.69497"); }
QString RetailCommunityReferenceService::cacheRoot(const QString &build) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("retail-reference/%1/community").arg(build));
}
QString RetailCommunityReferenceService::cachePath(const QString &build) const { return cacheRoot(build.trimmed().isEmpty() ? defaultBuild() : build.trimmed()); }
bool RetailCommunityReferenceService::hasCachedBuild(const QString &build) const {
    const QaCommunityReference r = load(build);
    return !r.build.isEmpty() && !r.syncedSources.isEmpty();
}

QString RetailCommunityReferenceService::help() {
    return QStringLiteral(
        "Community Retail Reference — build-specific multi-source evidence\n\n"
        "Sources:\n"
        "1) Mythic Dungeon Tools (MDT) — dungeon NPC IDs, display IDs, map clones/points and spell lists.\n"
        "   https://github.com/Nnoggie/MythicDungeonTools\n"
        "2) DBM-Dungeons — boss/trash encounter spell/event evidence.\n"
        "   https://github.com/DeadlyBossMods/DBM-Dungeons\n"
        "3) LittleWigs — 5-man encounter aura/spell warnings.\n"
        "   https://github.com/BigWigsMods/LittleWigs\n"
        "4) BigWigs — additional encounter modules where present.\n"
        "   https://github.com/BigWigsMods/BigWigs\n"
        "5) AllTheThings — harvested object/quest/reference data.\n"
        "   https://github.com/ATTWoWAddon/AllTheThings\n"
        "6) Keystone.guru — dungeon enemy data/positions where public JSON seeders are available.\n"
        "   https://github.com/RaiderIO/keystone.guru\n"
        "7) wowdata — direct Blizzard CDN DB2/spell/encounter/creature JSON without a WoW client.\n"
        "   https://github.com/Follen/wowdata\n"
        "8) wow.export — public CDN streaming, DB2/maps/models/scripts; can work without a local client.\n"
        "   https://github.com/Kruithne/wow.export\n"
        "9) Warcraft Logs — combat event evidence (casts, auras, damage, summons); API credentials are required for automated queries.\n"
        "   https://www.warcraftlogs.com/api/docs\n"
        "10) WoWDB / Warcraft Wiki / Icy Veins / Wowhead — human-readable cross-checks and encounter guides.\n"
        "   https://thewowdb.com/ | https://warcraft.wiki.gg/ | https://www.icy-veins.com/wow/ | https://www.wowhead.com/\n\n"
        "The built-in synchronizer currently downloads public GitHub text/JSON sources above. wowdata/wow.export/WCL are registered as external reference sources; their exact-build output can be imported/connected separately.\n"
        "For spells, every independent source adds evidence; repeated agreement raises the score.\n"
        "For positions, coordinateSpace is preserved: MDT/KeystoneGuru coordinates are NOT silently treated as world X/Y/Z.\n"
        "World-space coordinates from sniff/TDB can be compared separately by the QA geodesist.\n"
        "The goal is evidence fusion, not pretending one community source is absolute truth.");
}

QNetworkRequest RetailCommunityReferenceService::request(const QUrl &url, const QByteArray &accept) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "WoWDBStudio/0.3 RetailCommunityReference");
    req.setRawHeader("Accept", accept);
    req.setRawHeader("Accept-Encoding", "identity");
    req.setTransferTimeout(60000);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);
    return req;
}

QString RetailCommunityReferenceService::safeFileName(const QString &path) {
    QString s = path;
    s.replace(QLatin1Char('/'), QLatin1Char('_'));
    s.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return s;
}

void RetailCommunityReferenceService::syncAll(const QString &build) {
    m_build = build.trimmed().isEmpty() ? defaultBuild() : build.trimmed();
    QDir root(cacheRoot(m_build));
    root.removeRecursively();
    QDir().mkpath(cacheRoot(m_build));
    m_files.clear();
    m_repoIndex = 0;
    m_fileIndex = 0;
    emit progress(QStringLiteral("Community Reference: MDT + DBM + LittleWigs + BigWigs + ATT + KeystoneGuru для %1…").arg(m_build));
    if (m_repos.isEmpty()) { finalizeSync(); return; }
    fetchRepoTree(m_repos.first());
}

void RetailCommunityReferenceService::fetchRepoTree(const Repo &repo) {
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/git/trees/%3?recursive=1")
                       .arg(repo.owner, repo.name, repo.branch));
    emit progress(QStringLiteral("Community: индекс %1/%2 — %3/%4…")
                      .arg(m_repoIndex + 1).arg(m_repos.size()).arg(repo.owner, repo.name));
    auto *reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo] {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        const QString errText = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError || http >= 400) {
            emit failed(QStringLiteral("GitHub %1/%2: %3 (HTTP %4). Источник пропущен.")
                            .arg(repo.owner, repo.name, errText).arg(http));
        } else {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
            const QJsonArray tree = doc.object().value(QStringLiteral("tree")).toArray();
            if (pe.error != QJsonParseError::NoError || tree.isEmpty()) {
                emit failed(QStringLiteral("GitHub %1/%2: некорректный tree API ответ (%3).")
                                .arg(repo.owner, repo.name, pe.errorString()));
            } else {
                const QStringList paths = filesForSource(repo, tree);
                for (const QString &path : paths) {
                    m_files.push_back({repo.key, repo.owner, repo.name, repo.branch, path,
                        QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/%4")
                            .arg(repo.owner, repo.name, repo.branch, path)});
                }
                emit progress(QStringLiteral("Community %1: отобрано файлов: %2").arg(repo.key).arg(paths.size()));
            }
        }
        ++m_repoIndex;
        if (m_repoIndex < m_repos.size()) fetchRepoTree(m_repos.at(m_repoIndex));
        else fetchNextFile();
    });
}

QStringList RetailCommunityReferenceService::filesForSource(const Repo &repo, const QJsonArray &tree) {
    QStringList out;
    for (const auto &v : tree) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("blob")) continue;
        const QString p = o.value(QStringLiteral("path")).toString();
        if (!p.startsWith(repo.prefix)) continue;
        const QString low = p.toLower();
        bool keep = false;
        if (repo.key == QLatin1String("MDT")) {
            keep = low.endsWith(QLatin1String(".lua"));
        } else if (repo.key == QLatin1String("DBM") || repo.key == QLatin1String("LittleWigs")) {
            keep = low.endsWith(QLatin1String(".lua")) &&
                   (low.contains("murderrow") || low.contains("denofnalorakk") || low.contains("blindingvale") ||
                    low.contains("voidscararena") || low.contains("altaroffangs") || low.contains("kingsrest") ||
                    low.contains("templeofsethraliss") || low.contains("rubylifepools") || low.contains("trash"));
        } else if (repo.key == QLatin1String("BigWigs")) {
            keep = low.endsWith(QLatin1String(".lua"));
        } else if (repo.key == QLatin1String("ATT")) {
            keep = low.endsWith(QLatin1String(".lua")) &&
                   (low.contains("objectdb") || low.contains("dynamicobjectdb") || low.contains("quest") || low.contains("dungeon"));
        } else if (repo.key == QLatin1String("KeystoneGuru")) {
            keep = low.endsWith(QLatin1String(".json")) &&
                   (low.endsWith(QLatin1String("/enemies.json")) || low.endsWith(QLatin1String("/enemy_packs.json")));
        }
        if (keep) out << p;
    }
    std::sort(out.begin(), out.end());
    const int maxFiles = repo.key == QLatin1String("MDT") ? 30 :
                         repo.key == QLatin1String("DBM") ? 80 :
                         repo.key == QLatin1String("LittleWigs") ? 80 :
                         repo.key == QLatin1String("BigWigs") ? 50 :
                         repo.key == QLatin1String("ATT") ? 60 : 30;
    if (out.size() > maxFiles) out = out.mid(0, maxFiles);
    return out;
}

void RetailCommunityReferenceService::fetchNextFile() {
    if (m_fileIndex >= m_files.size()) { finalizeSync(); return; }
    const FileJob job = m_files.at(m_fileIndex++);
    emit progress(QStringLiteral("Community: %1/%2 — %3/%4").arg(m_fileIndex).arg(m_files.size()).arg(job.repo, job.path));
    auto *reply = m_network.get(request(QUrl(job.url), "text/plain,application/json,*/*"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto err = reply->error();
        const QByteArray body = reply->readAll();
        const QString errText = reply->errorString();
        reply->deleteLater();
        if (err == QNetworkReply::NoError && http < 400 && !body.isEmpty()) {
            const QString dir = QDir(cacheRoot(m_build)).filePath(job.source);
            QDir().mkpath(dir);
            saveText(QDir(dir).filePath(safeFileName(job.path)), body);
        } else {
            emit failed(QStringLiteral("Community %1: %2 (HTTP %3) — пропуск %4")
                            .arg(job.source, errText).arg(http).arg(job.path));
        }
        fetchNextFile();
    });
}

void RetailCommunityReferenceService::saveText(const QString &path, const QByteArray &data) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(data);
}

QVector<QStringList> RetailCommunityReferenceService::readFilesInSource(const QString &root) {
    QVector<QStringList> result;
    QDir d(root);
    if (!d.exists()) return result;
    const QStringList files = d.entryList({QStringLiteral("*.lua"), QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &fn : files) {
        QFile f(d.filePath(fn));
        if (!f.open(QIODevice::ReadOnly)) continue;
        result.push_back({fn, QString::fromUtf8(f.readAll())});
    }
    return result;
}

void RetailCommunityReferenceService::parseMdt(const QString &text, const QString &fileName, QaCommunityReference *out) {
    if (!out) return;
    QRegularExpression mapRe(QStringLiteral("mapID\\s*=\\s*(\\d+)"));
    const int mapId = mapRe.match(text).captured(1).toInt();
    const QRegularExpression entryRe(QStringLiteral("\\[(\\d+)\\]\\s*=\\s*\\{\\s*\\[\\\"name\\\"\\]\\s*=\\s*\\\"([^\\\"]*)\\\""));
    const QRegularExpression idRe(QStringLiteral("\\[\\\"id\\\"\\]\\s*=\\s*(\\d+)"));
    const QRegularExpression spellRe(QStringLiteral("\\[(\\d{4,8})\\]\\s*=\\s*\\{"));
    const QRegularExpression cloneRe(QStringLiteral("\\[\\d+\\]\\s*=\\s*\\{[^{}]*\\[\\\"x\\\"\\]\\s*=\\s*(-?[0-9]+(?:\\.[0-9]+)?)[^{}]*\\[\\\"y\\\"\\]\\s*=\\s*(-?[0-9]+(?:\\.[0-9]+)?)[^{}]*?(?:\\[\\\"g\\\"\\]\\s*=\\s*(\\d+))?"));
    QSet<int> entries;
    auto it = entryRe.globalMatch(text);
    QVector<QRegularExpressionMatch> entryMatches;
    while (it.hasNext()) entryMatches.push_back(it.next());
    for (int mi = 0; mi < entryMatches.size(); ++mi) {
        const auto m = entryMatches.at(mi);
        const int startPos = m.capturedStart();
        const int nextPos = (mi + 1 < entryMatches.size()) ? entryMatches.at(mi + 1).capturedStart() : text.size();
        const QString block = text.mid(startPos, nextPos - startPos);
        if (!block.contains(QLatin1String("[\"clones\"]"))) continue;
        const auto im = idRe.match(block);
        if (!im.hasMatch()) continue;
        const int npc = im.captured(1).toInt();
        if (npc <= 0) continue;
        entries.insert(npc);
        const int s0 = block.indexOf(QLatin1String("[\"spells\"]"));
        const int s1 = block.indexOf(QLatin1String("[\"clones\"]"), s0 < 0 ? 0 : s0);
        if (s0 >= 0 && s1 > s0) {
            auto sit = spellRe.globalMatch(block.mid(s0, s1 - s0));
            while (sit.hasNext()) {
                const auto sm = sit.next();
                QStringList flags;
                const int p = sm.capturedStart();
                const QString tiny = block.mid(s0 + p, qMin(300, block.size() - s0 - p));
                if (tiny.contains(QLatin1String("interruptible"))) flags << QStringLiteral("interruptible");
                if (tiny.contains(QLatin1String("magic"))) flags << QStringLiteral("magic");
                if (tiny.contains(QLatin1String("enrage"))) flags << QStringLiteral("enrage");
                addSpell(out, sm.captured(1).toInt(), QStringLiteral("MDT"), flags, fileName);
            }
        }
        auto cit = cloneRe.globalMatch(block);
        while (cit.hasNext()) {
            const auto cm = cit.next();
            QaCommunityPoint point;
            point.sourceDungeonId = mapId;
            point.entityId = npc;
            point.mapId = mapId;
            point.x = cm.captured(1).toDouble();
            point.y = cm.captured(2).toDouble();
            point.coordinateSpace = QStringLiteral("mdt");
            point.source = QStringLiteral("MDT");
            point.weight = 0.82;
            point.label = QStringLiteral("%1:%2").arg(fileName, m.captured(2));
            out->points.push_back(point);
            ++out->mdtNpcPoints;
        }
    }
    out->mdtNpcEntries += entries.size();
}

void RetailCommunityReferenceService::parseSpellIds(const QString &text, const QString &source, const QString &context, QaCommunityReference *out) {
    if (!out) return;
    QSet<int> seen;
    // Keep these patterns deliberately simple: Qt 6.11/PCRE2 can reject some
    // heavily escaped alternations as invalid depending on the exact source text.
    // MDT spell tables use either [123456] = { ... } or a quoted alert/color value.
    const QVector<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("(?:spellId|spellID|spellid|spell)\\s*=\\s*(\\d{4,8})")),
        QRegularExpression(QStringLiteral("RegisterEvent\\([^\\n]*?(\\d{4,8})")),
        QRegularExpression(QStringLiteral("\\[(\\d{4,8})\\]\\s*=\\s*\\{")),
    };
    for (const auto &re : patterns) {
        if (!re.isValid()) continue;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const int id = it.next().captured(1).toInt();
            if (id <= 0 || seen.contains(id)) continue;
            seen.insert(id);
            addSpell(out, id, source, {}, context);
        }
    }
}

void RetailCommunityReferenceService::parseBigWigs(const QString &text, const QString &context, QaCommunityReference *out) {
    if (!out) return;
    QSet<int> seen;
    const QVector<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("registerMessage\\([^\\n]*?(\\d{4,8})"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("alert\\([^\\n]*?(\\d{4,8})"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("CD\\([^\\n]*?(\\d{4,8})"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\"spellId\"?\\s*=\\s*(\\d{4,8})"), QRegularExpression::CaseInsensitiveOption)
    };
    for (const auto &re : patterns) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const int id = it.next().captured(1).toInt();
            if (id <= 0 || seen.contains(id)) continue;
            seen.insert(id);
            addSpell(out, id, QStringLiteral("BigWigs"), {}, context);
        }
    }
}

void RetailCommunityReferenceService::parseAtt(const QString &text, const QString &context, QaCommunityReference *out) {
    if (!out) return;
    QRegularExpression spell(QStringLiteral("(?:spell|s|spellID|itemSpell)\\s*[:=]\\s*(\\d{4,8})"), QRegularExpression::CaseInsensitiveOption);
    auto it = spell.globalMatch(text);
    while (it.hasNext()) addSpell(out, it.next().captured(1).toInt(), QStringLiteral("ATT"), {}, context);
}

void RetailCommunityReferenceService::walkKeystoneJson(const QJsonValue &value, QaCommunityReference *out) {
    if (!out) return;
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        int npc = o.value(QStringLiteral("npc_id")).toInt();
        if (!npc) npc = o.value(QStringLiteral("npcId")).toInt();
        if (!npc) npc = o.value(QStringLiteral("npcID")).toInt();
        const QJsonValue xv = o.value(QStringLiteral("x"));
        const QJsonValue yv = o.value(QStringLiteral("y"));
        const bool okX = xv.isDouble();
        const bool okY = yv.isDouble();
        const double x = xv.toDouble();
        const double y = yv.toDouble();
        if (npc > 0 && okX && okY) {
            QaCommunityPoint p;
            p.entityId = npc;
            p.x = x; p.y = y;
            p.coordinateSpace = QStringLiteral("keystone_guru");
            p.source = QStringLiteral("KeystoneGuru");
            p.weight = 0.78;
            out->points.push_back(p);
            ++out->keystoneNpcPoints;
        }
        const QStringList keys = o.keys();
        for (const QString &k : keys) walkKeystoneJson(o.value(k), out);
    } else if (value.isArray()) {
        for (const auto &v : value.toArray()) walkKeystoneJson(v, out);
    }
}

void RetailCommunityReferenceService::parseKeystoneJson(const QString &text, const QString &, QaCommunityReference *out) {
    if (!out) return;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error == QJsonParseError::NoError) walkKeystoneJson(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), out);
}

void RetailCommunityReferenceService::addSpell(QaCommunityReference *out, int spellId, const QString &source, const QStringList &flags, const QString &context) {
    if (!out || spellId <= 0) return;
    auto &e = out->spells[spellId];
    e.spellId = spellId;
    if (!e.sources.contains(source)) e.sources << source;
    for (const QString &f : flags) if (!e.flags.contains(f)) e.flags << f;
    if (!context.isEmpty() && !e.contexts.contains(context)) e.contexts << context;
}

void RetailCommunityReferenceService::finalizeScores(QaCommunityReference *out) {
    if (!out) return;
    for (auto it = out->spells.begin(); it != out->spells.end(); ++it) {
        const QStringList sources = it.value().sources;
        double score = 0.0;
        for (const QString &src : sources) {
            if (src == QLatin1String("MDT")) score += 0.82;
            else if (src == QLatin1String("DBM")) score += 0.82;
            else if (src == QLatin1String("LittleWigs")) score += 0.82;
            else if (src == QLatin1String("BigWigs")) score += 0.82;
            else if (src == QLatin1String("ATT")) score += 0.65;
            else if (src == QLatin1String("KeystoneGuru")) score += 0.72;
            else if (src == QLatin1String("Wago")) score += 0.95;
        }
        if (sources.size() >= 2) score += 0.08;
        if (sources.size() >= 3) score += 0.06;
        if (sources.size() >= 4) score += 0.04;
        it.value().score = qMin(1.0, score);
    }
    // Point consensus: compare community coordinates only within the same coordinate system and map.
    // This avoids falsely comparing MDT normalized points with arbitrary world yards.
    int pointConsensus = 0;
    for (int i = 0; i < out->points.size(); ++i) {
        auto &p = out->points[i];
        if (p.entityId <= 0 || p.coordinateSpace.isEmpty()) continue;
        double sumX = 0.0, sumY = 0.0, sumW = 0.0;
        QStringList sources;
        int support = 0;
        for (int j = 0; j < out->points.size(); ++j) {
            const auto &q = out->points.at(j);
            if (i == j || q.entityId != p.entityId || q.mapId != p.mapId || q.coordinateSpace != p.coordinateSpace) continue;
            const double dx = p.x - q.x, dy = p.y - q.y;
            const double d = std::sqrt(dx * dx + dy * dy);
            const double radius = (p.coordinateSpace == QLatin1String("mdt") || p.coordinateSpace == QLatin1String("keystone_guru")) ? 4.0 : 10.0;
            if (d > radius) continue;
            const double w = qMax(0.05, q.weight);
            sumX += q.x * w; sumY += q.y * w; sumW += w;
            if (!q.source.isEmpty() && !sources.contains(q.source)) sources << q.source;
            ++support;
        }
        if (support > 0 && sumW > 0.0) {
            p.hasConsensus = true;
            p.consensusX = sumX / sumW;
            p.consensusY = sumY / sumW;
            p.consensusSources = sources;
            p.consensusScore = qMin(1.0, (sources.size() * 0.25) + (support * 0.10) + 0.20);
            if (sources.size() >= 2) ++pointConsensus;
        }
    }
    out->commonNpcEntries = pointConsensus;

    int common = 0;
    for (auto it = out->spells.constBegin(); it != out->spells.constEnd(); ++it)
        if (it.value().sources.size() >= 2) ++common;
    out->commonSpellIds = common;
}

QString RetailCommunityReferenceService::summary(const QaCommunityReference &r) {
    return QStringLiteral(
        "Build %1: sources=%2; MDT NPC entries=%3 points=%4; KeystoneGuru points=%5; point consensuses=%6; spells MDT=%7 DBM=%8 LittleWigs=%9 BigWigs=%10 ATT=%11; spell IDs supported by 2+ sources=%12.")
        .arg(r.build).arg(r.syncedSources.join(", ")).arg(r.mdtNpcEntries).arg(r.mdtNpcPoints).arg(r.keystoneNpcPoints).arg(r.commonNpcEntries)
        .arg(r.mdtSpellIds).arg(r.dbmSpellIds).arg(r.littleWigsSpellIds).arg(r.bigWigsSpellIds).arg(r.attSpellIds).arg(r.commonSpellIds);
}

QaCommunityReference RetailCommunityReferenceService::load(const QString &build) const {
    QaCommunityReference r;
    const QString root = cachePath(build);
    QFile f(QDir(root).filePath(QStringLiteral("community_index.json")));
    if (!f.open(QIODevice::ReadOnly)) return r;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return r;
    const QJsonObject o = doc.object();
    r.build = o.value(QStringLiteral("build")).toString();
    for (const auto &v : o.value(QStringLiteral("sources")).toArray()) r.syncedSources << v.toString();
    r.summary = o.value(QStringLiteral("summary")).toString();
    r.mdtNpcEntries = o.value(QStringLiteral("mdtNpcEntries")).toInt();
    r.mdtNpcPoints = o.value(QStringLiteral("mdtNpcPoints")).toInt();
    r.keystoneNpcPoints = o.value(QStringLiteral("keystoneNpcPoints")).toInt();
    for (const auto &v : o.value(QStringLiteral("points")).toArray()) {
        const QJsonObject p = v.toObject();
        QaCommunityPoint cp;
        cp.sourceDungeonId = p.value("sourceDungeonId").toInt(); cp.entityId = p.value("entityId").toInt(); cp.mapId = p.value("mapId").toInt();
        cp.x = p.value("x").toDouble(); cp.y = p.value("y").toDouble(); cp.z = p.value("z").toDouble(); cp.hasZ = p.value("hasZ").toBool();
        cp.coordinateSpace = p.value("coordinateSpace").toString(); cp.source = p.value("source").toString(); cp.weight = p.value("weight").toDouble(); cp.label = p.value("label").toString();
        cp.hasConsensus = p.value("hasConsensus").toBool(); cp.consensusX = p.value("consensusX").toDouble(); cp.consensusY = p.value("consensusY").toDouble();
        cp.consensusScore = p.value("consensusScore").toDouble(); for (const auto &x : p.value("consensusSources").toArray()) cp.consensusSources << x.toString();
        r.points.push_back(cp);
    }
    for (const auto &v : o.value(QStringLiteral("spells")).toArray()) {
        const QJsonObject e = v.toObject();
        QaCommunitySpellEvidence se; se.spellId = e.value("id").toInt();
        for (const auto &x : e.value("sources").toArray()) se.sources << x.toString();
        for (const auto &x : e.value("flags").toArray()) se.flags << x.toString();
        for (const auto &x : e.value("contexts").toArray()) se.contexts << x.toString();
        se.score = e.value("score").toDouble();
        r.spells.insert(se.spellId, se);
    }
    finalizeScores(&r);
    return r;
}

void RetailCommunityReferenceService::finalizeSync() {
    QaCommunityReference r;
    r.build = m_build;
    const QString root = cacheRoot(m_build);
    for (const Repo &repo : m_repos) {
        const auto files = readFilesInSource(QDir(root).filePath(repo.key));
        if (files.isEmpty()) continue;
        r.syncedSources << repo.key;
        for (const auto &pair : files) {
            if (repo.key == QLatin1String("MDT")) parseMdt(pair[1], pair[0], &r);
            else if (repo.key == QLatin1String("DBM")) parseSpellIds(pair[1], repo.key, pair[0], &r);
            else if (repo.key == QLatin1String("LittleWigs")) parseSpellIds(pair[1], repo.key, pair[0], &r);
            else if (repo.key == QLatin1String("BigWigs")) parseBigWigs(pair[1], pair[0], &r);
            else if (repo.key == QLatin1String("ATT")) { parseAtt(pair[1], pair[0], &r); ++r.attObjectFiles; }
            else if (repo.key == QLatin1String("KeystoneGuru")) parseKeystoneJson(pair[1], pair[0], &r);
        }
    }
    for (auto it = r.spells.constBegin(); it != r.spells.constEnd(); ++it) {
        if (it.value().sources.contains(QLatin1String("MDT"))) ++r.mdtSpellIds;
        if (it.value().sources.contains(QLatin1String("DBM"))) ++r.dbmSpellIds;
        if (it.value().sources.contains(QLatin1String("LittleWigs"))) ++r.littleWigsSpellIds;
        if (it.value().sources.contains(QLatin1String("BigWigs"))) ++r.bigWigsSpellIds;
        if (it.value().sources.contains(QLatin1String("ATT"))) ++r.attSpellIds;
    }
    finalizeScores(&r);
    r.summary = summary(r);

    QJsonObject rootObj;
    rootObj[QStringLiteral("build")] = r.build;
    QJsonArray ss; for (const auto &s : r.syncedSources) ss << s; rootObj[QStringLiteral("sources")] = ss;
    rootObj[QStringLiteral("summary")] = r.summary;
    rootObj[QStringLiteral("mdtNpcEntries")] = r.mdtNpcEntries;
    rootObj[QStringLiteral("mdtNpcPoints")] = r.mdtNpcPoints;
    rootObj[QStringLiteral("keystoneNpcPoints")] = r.keystoneNpcPoints;
    QJsonArray points;
    for (const auto &p : r.points) {
        QJsonObject o; o["sourceDungeonId"] = p.sourceDungeonId; o["entityId"] = p.entityId; o["mapId"] = p.mapId;
        o["x"] = p.x; o["y"] = p.y; o["z"] = p.z; o["hasZ"] = p.hasZ; o["coordinateSpace"] = p.coordinateSpace;
        o["source"] = p.source; o["weight"] = p.weight; o["label"] = p.label;
        o["hasConsensus"] = p.hasConsensus; o["consensusX"] = p.consensusX; o["consensusY"] = p.consensusY;
        o["consensusScore"] = p.consensusScore; QJsonArray cs; for (const auto &x : p.consensusSources) cs << x; o["consensusSources"] = cs; points << o;
    }
    rootObj[QStringLiteral("points")] = points;
    QJsonArray spells;
    for (auto it = r.spells.constBegin(); it != r.spells.constEnd(); ++it) {
        QJsonObject o; o[QStringLiteral("id")] = it.key();
        QJsonArray src; for (const auto &s : it.value().sources) src << s; o[QStringLiteral("sources")] = src;
        QJsonArray fl; for (const auto &s : it.value().flags) fl << s; o[QStringLiteral("flags")] = fl;
        QJsonArray cx; for (const auto &s : it.value().contexts) cx << s; o[QStringLiteral("contexts")] = cx;
        o[QStringLiteral("score")] = it.value().score; spells << o;
    }
    rootObj[QStringLiteral("spells")] = spells;
    if (r.syncedSources.isEmpty()) {
        QFile::remove(QDir(root).filePath(QStringLiteral("community_index.json")));
        emit failed(QStringLiteral("Community Reference: ни один источник не удалось синхронизировать для build %1.").arg(m_build));
        emit finished(false, QStringLiteral("Community Reference %1: нет успешно загруженных источников.").arg(m_build), m_build);
        return;
    }
    QFile f(QDir(root).filePath(QStringLiteral("community_index.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    emit progress(QStringLiteral("Community Reference: %1").arg(r.summary));
    emit finished(true, QStringLiteral("Community Reference %1 сохранён: %2").arg(m_build, root), m_build);
}

void RetailCommunityReferenceService::enrichCatalog(QaRetailCatalog *catalog, const QString &build) const {
    if (!catalog) return;
    const QaCommunityReference r = load(build);
    if (r.build.isEmpty()) {
        catalog->communityReferenceSummary = QStringLiteral("Community Reference: cache %1 не найден — синхронизируйте источники.").arg(build);
        return;
    }
    catalog->communityReferenceSummary = r.summary;
    catalog->communityPoints = r.points;
    catalog->communitySpellEvidence.clear();
    catalog->communitySpellScores.clear();
    for (auto it = r.spells.constBegin(); it != r.spells.constEnd(); ++it) {
        QaCommunitySpellEvidence ev = it.value();
        if (catalog->scope.enabled && (catalog->scope.kind == QLatin1String("dungeon") || catalog->scope.kind == QLatin1String("raid"))
            && !catalog->scope.name.trimmed().isEmpty() && !ev.contexts.isEmpty()) {
            auto normContext = [](QString x) {
                x = x.toLower();
                x.remove(QRegularExpression(QStringLiteral("[^a-z0-9а-я]+")));
                return x;
            };
            const QString want = normContext(catalog->scope.name);
            bool contextHit = false;
            for (const QString &ctx : ev.contexts) {
                const QString c = normContext(ctx);
                if (!want.isEmpty() && (c.contains(want) || want.contains(c))) { contextHit = true; break; }
                QString shortWant = want;
                if (shortWant.startsWith(QStringLiteral("the"))) shortWant.remove(0, 3);
                if (!shortWant.isEmpty() && c.contains(shortWant)) { contextHit = true; break; }
                const QStringList aliases = {
                    QStringLiteral("murderrow"), QStringLiteral("denofnalorakk"),
                    QStringLiteral("blindingvale"), QStringLiteral("voidscararena"), QStringLiteral("altaroffangs")
                };
                const QStringList names = {
                    QStringLiteral("murder row"), QStringLiteral("den of nalorakk"),
                    QStringLiteral("the blinding vale"), QStringLiteral("blinding vale"),
                    QStringLiteral("voidscar arena"), QStringLiteral("altar of fangs")
                };
                for (int ai = 0; ai < aliases.size(); ++ai) {
                    const QString a = normContext(names.at(ai));
                    if ((want == a || shortWant == a) && c.contains(aliases.at(ai))) { contextHit = true; break; }
                }
                if (contextHit) break;
            }
            if (!contextHit) continue;
        }
        for (const auto &sp : catalog->spells) {
            if (sp.id == ev.spellId && sp.hasWago && !ev.sources.contains(QStringLiteral("Wago"))) {
                ev.sources << QStringLiteral("Wago");
                ev.score = qMin(1.0, ev.score + 0.10);
                break;
            }
        }
        catalog->communitySpellEvidence.insert(it.key(), QStringLiteral("%1; score=%2%3")
            .arg(ev.sources.join(", ")).arg(ev.score, 0, 'f', 2)
            .arg(ev.flags.isEmpty() ? QString() : QStringLiteral("; flags=%1").arg(ev.flags.join(", "))));
        catalog->communitySpellScores.insert(it.key(), ev.score);
    }
    // Wago-only spells are still useful evidence even if community addons did not mention the ID.
    for (const auto &sp : catalog->spells) {
        if (!sp.hasWago || catalog->communitySpellEvidence.contains(sp.id)) continue;
        catalog->communitySpellEvidence.insert(sp.id, QStringLiteral("Wago; score=0.95"));
        catalog->communitySpellScores.insert(sp.id, 0.95);
    }
}
