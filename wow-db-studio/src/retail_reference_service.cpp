#include "retail_reference_service.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTextStream>

RetailReferenceService::RetailReferenceService(QObject *parent) : QObject(parent) {
    // Keep canonical names stable inside our cache, while trying Wago's actual DB2 names
    // and known aliases. Missing Wago exports are handled per-table and then sent to the
    // external wowdata CDN client (when installed), rather than aborting the whole sync.
    m_jobs = {
        {QStringLiteral("Creature"), QStringLiteral("Creature.csv"), {}, false},
        {QStringLiteral("CreatureDisplayInfo"), QStringLiteral("CreatureDisplayInfo.csv"), {}, false},
        {QStringLiteral("CreatureModelData"), QStringLiteral("CreatureModelData.csv"), {}, false},
        {QStringLiteral("CreatureXDisplayInfo"), QStringLiteral("CreatureXDisplayInfo.csv"), {}, false},
        {QStringLiteral("CreatureDifficulty"), QStringLiteral("CreatureDifficulty.csv"), {}, false},
        {QStringLiteral("GameObjects"), QStringLiteral("GameObject.csv"), {QStringLiteral("GameObject")}, false},
        {QStringLiteral("GameObjectDisplayInfo"), QStringLiteral("GameObjectDisplayInfo.csv"), {}, false},
        {QStringLiteral("Map"), QStringLiteral("Map.csv"), {}, false},
        {QStringLiteral("MapDifficulty"), QStringLiteral("MapDifficulty.csv"), {}, false},
        {QStringLiteral("MapChallengeMode"), QStringLiteral("MapChallengeMode.csv"), {}, false},
        {QStringLiteral("AreaTable"), QStringLiteral("AreaTable.csv"), {}, false},
        {QStringLiteral("AreaGroupMember"), QStringLiteral("AreaGroupMember.csv"), {}, false},
        {QStringLiteral("AreaPOI"), QStringLiteral("AreaPOI.csv"), {}, false},
        {QStringLiteral("DungeonEncounter"), QStringLiteral("DungeonEncounter.csv"), {}, false},
        {QStringLiteral("JournalInstance"), QStringLiteral("JournalInstance.csv"), {}, false},
        {QStringLiteral("JournalEncounter"), QStringLiteral("JournalEncounter.csv"), {}, false},
        {QStringLiteral("JournalEncounterSection"), QStringLiteral("JournalEncounterSection.csv"), {}, false},
        {QStringLiteral("JournalEncounterCreature"), QStringLiteral("JournalEncounterCreature.csv"), {}, false},
        {QStringLiteral("Spell"), QStringLiteral("Spell.csv"), {}, false},
        {QStringLiteral("SpellName"), QStringLiteral("SpellName.csv"), {}, false},
        {QStringLiteral("SpellEffect"), QStringLiteral("SpellEffect.csv"), {}, false},
        {QStringLiteral("SpellMisc"), QStringLiteral("SpellMisc.csv"), {}, false},
        {QStringLiteral("SpellAuraOptions"), QStringLiteral("SpellAuraOptions.csv"), {}, false},
        {QStringLiteral("SpellAuraRestrictions"), QStringLiteral("SpellAuraRestrictions.csv"), {}, false},
        {QStringLiteral("SpellCategories"), QStringLiteral("SpellCategories.csv"), {}, false},
        {QStringLiteral("SpellCharges"), QStringLiteral("SpellCharges.csv"), {}, false},
        {QStringLiteral("SpellClassOptions"), QStringLiteral("SpellClassOptions.csv"), {}, false},
        {QStringLiteral("SpellCooldowns"), QStringLiteral("SpellCooldowns.csv"), {}, false},
        {QStringLiteral("SpellDuration"), QStringLiteral("SpellDuration.csv"), {}, false},
        {QStringLiteral("SpellEquippedItems"), QStringLiteral("SpellEquippedItems.csv"), {}, false},
        {QStringLiteral("SpellInterrupts"), QStringLiteral("SpellInterrupts.csv"), {}, false},
        {QStringLiteral("SpellLevels"), QStringLiteral("SpellLevels.csv"), {}, false},
        {QStringLiteral("SpellPower"), QStringLiteral("SpellPower.csv"), {}, false},
        {QStringLiteral("SpellReagents"), QStringLiteral("SpellReagents.csv"), {}, false},
        {QStringLiteral("SpellRadius"), QStringLiteral("SpellRadius.csv"), {}, false},
        {QStringLiteral("SpellRange"), QStringLiteral("SpellRange.csv"), {}, false},
        {QStringLiteral("SpellShapeshift"), QStringLiteral("SpellShapeshift.csv"), {}, false},
        {QStringLiteral("SpellTargetRestrictions"), QStringLiteral("SpellTargetRestrictions.csv"), {}, false},
        {QStringLiteral("SpellVisual"), QStringLiteral("SpellVisual.csv"), {}, false},
        {QStringLiteral("SpellXSpellVisual"), QStringLiteral("SpellXSpellVisual.csv"), {}, false},
        {QStringLiteral("SkillLineAbility"), QStringLiteral("SkillLineAbility.csv"), {}, false},
        {QStringLiteral("QuestV2"), QStringLiteral("QuestV2.csv"), {}, false},
        {QStringLiteral("QuestPackageItem"), QStringLiteral("QuestPackageItem.csv"), {}, false},
        {QStringLiteral("QuestInfo"), QStringLiteral("QuestInfo.csv"), {}, false},
        {QStringLiteral("QuestFactionReward"), QStringLiteral("QuestFactionReward.csv"), {}, false},
        {QStringLiteral("QuestRewardDisplaySpell"), QStringLiteral("QuestRewardDisplaySpell.csv"), {}, false},
        {QStringLiteral("Item"), QStringLiteral("Item.csv"), {}, false},
        {QStringLiteral("ItemSparse"), QStringLiteral("ItemSparse.csv"), {}, false},
        {QStringLiteral("ItemEffect"), QStringLiteral("ItemEffect.csv"), {}, false},
        {QStringLiteral("ItemXItemEffect"), QStringLiteral("ItemXItemEffect.csv"), {}, false},
        {QStringLiteral("AreaTrigger"), QStringLiteral("AreaTrigger.csv"), {}, false},
        {QStringLiteral("AreaTriggerCreateProperties"), QStringLiteral("AreaTriggerCreateProperties.csv"), {}, false},
        {QStringLiteral("AreaTriggerShape"), QStringLiteral("AreaTriggerShape.csv"), {}, false},
        {QStringLiteral("Phase"), QStringLiteral("Phase.csv"), {}, false},
        {QStringLiteral("PhaseXPhaseGroup"), QStringLiteral("PhaseXPhaseGroup.csv"), {}, false},
        {QStringLiteral("Criteria"), QStringLiteral("Criteria.csv"), {}, false},
        {QStringLiteral("CriteriaTree"), QStringLiteral("CriteriaTree.csv"), {}, false}
    };
}

QString RetailReferenceService::defaultBuild() { return QStringLiteral("12.1.0.69497"); }

QString RetailReferenceService::cacheRoot(const QString &build) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("retail-reference/%1").arg(build));
}

QString RetailReferenceService::cachePath(const QString &build) const { return cacheRoot(build); }

bool RetailReferenceService::hasCachedBuild(const QString &build) const {
    QDir d(cacheRoot(build));
    return d.exists() && d.exists(QStringLiteral("Creature.csv"));
}

bool RetailReferenceService::resolveScopeFromCache(const QString &build, const QString &query, int *mapId, int *zoneId, QString *canonicalName) {
    if (mapId) *mapId = 0;
    if (zoneId) *zoneId = 0;
    if (canonicalName) canonicalName->clear();
    const QString want = query.trimmed();
    if (want.isEmpty()) return false;
    auto normalize = [](QString x) {
        x = x.toLower().trimmed();
        x.replace(QLatin1Char('ё'), QLatin1Char('е'));
        x.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
        return x.simplified();
    };
    const QString wn = normalize(want);
    struct Candidate { int id = 0; QString name; bool map = false; int score = -1; };
    Candidate best;
    const QString root = cacheRoot(build);
    const QStringList files = {QStringLiteral("Map.csv"), QStringLiteral("AreaTable.csv")};
    for (const QString &fn : files) {
        const QString path = QDir(root).filePath(fn);
        const auto rows = readCsvFile(path, 0);
        if (rows.size() < 2) continue;
        const QStringList h = rows.first();
        int idCol = columnIndex(h, {QStringLiteral("ID"), QStringLiteral("MapID"), QStringLiteral("AreaTableID")});
        int nameCol = columnIndex(h, {QStringLiteral("Name_lang"), QStringLiteral("Name"), QStringLiteral("Name_enUS"), QStringLiteral("UiMapName"), QStringLiteral("ZoneName")});
        if (idCol < 0 || nameCol < 0) continue;
        const bool isMap = fn.compare(QStringLiteral("Map.csv"), Qt::CaseInsensitive) == 0;
        for (int i = 1; i < rows.size(); ++i) {
            const auto &r = rows.at(i);
            if (idCol >= r.size() || nameCol >= r.size()) continue;
            const int id = scalar(r, idCol).toInt();
            const QString name = scalar(r, nameCol);
            if (id <= 0 || name.isEmpty()) continue;
            const QString nn = normalize(name);
            int score = -1;
            if (nn == wn) score = 100;
            else if (nn.contains(wn)) score = 80;
            else if (wn.contains(nn) && nn.size() >= 4) score = 70;
            if (score < 0) continue;
            if (score > best.score) best = {id, name, isMap, score};
        }
    }
    if (best.score < 0) return false;
    if (best.map) { if (mapId) *mapId = best.id; }
    else { if (zoneId) *zoneId = best.id; }
    if (canonicalName) *canonicalName = best.name;
    return true;
}

QString RetailReferenceService::help() {
    return QStringLiteral(
        "Retail Reference без клиента/WPP\n\n"
        "Основной источник: Wago DB2 CSV. Локаль задаётся отдельно (по умолчанию ruRU).\n"
        "Если Wago не отдаёт конкретную таблицу для build/locale, Studio НЕ прекращает синхронизацию:\n"
        "сначала пробуются альтернативные имена DB2, затем установленный wowdata (Blizzard CDN).\n"
        "Таблица считается отсутствующей только после провала всех доступных источников.\n\n"
        "Build по умолчанию: 12.1.0.69497\n"
        "Для фактических X/Y/Z, GUID и наблюдаемого движения по-прежнему нужен sniff/WPP либо community export.\n\n"
        "wowdata: https://github.com/Follen/wowdata\n"
        "WoWDBDefs: https://github.com/wowdev/WoWDBDefs\n"
        "AllTheThings: https://github.com/ATTWoWAddon/AllTheThings/releases\n"
        "Примечание: DB2-схема и строки локализуются независимо; не каждая таблица содержит локализованные поля."
    );
}

static QNetworkRequest refRequest(const QUrl &url) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "WoWDBStudio/0.1 RetailReference");
    req.setRawHeader("Accept", "text/csv,*/*;q=0.8");
    req.setTransferTimeout(60000);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);
    return req;
}

QStringList RetailReferenceService::fallbackCandidates(const QString &name) {
    QStringList out;
    if (name.compare(QStringLiteral("GameObject"), Qt::CaseInsensitive) == 0)
        out << QStringLiteral("GameObjects");
    else if (name.compare(QStringLiteral("GameObjects"), Qt::CaseInsensitive) == 0)
        out << QStringLiteral("GameObject");
    return out;
}

void RetailReferenceService::syncBuild(const QString &build) {
    m_build = build.trimmed().isEmpty() ? defaultBuild() : build.trimmed();
    m_index = 0;
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(cacheRoot(m_build));
    QFile::remove(QDir(cacheRoot(m_build)).filePath(QStringLiteral("reference-missing.txt")));
    emit progress(QStringLiteral("Retail Reference: синхронизация build %1, locale %2…").arg(m_build, m_locale));
    emit progress(QStringLiteral("Источник 1: Wago DB2; fallback: альтернативное имя → wowdata/Blizzard CDN. Ни одна отсутствующая таблица не прерывает остальные."));
    fetchNext();
}

void RetailReferenceService::fetchNext() {
    if (m_index >= m_jobs.size()) {
        const qint64 ms = QDateTime::currentMSecsSinceEpoch() - m_startedMs;
        emit progress(QStringLiteral("Retail Reference: все задания источников обработаны за %1 ms. Отсутствующие таблицы перечислены в reference-missing.txt.").arg(ms));
        emit finished(true, QStringLiteral("Retail DB2 reference %1/%2 обработан: %3").arg(m_build, m_locale, cacheRoot(m_build)), m_build);
        return;
    }
    const TableJob job = m_jobs.at(m_index++);
    QStringList candidates;
    candidates << job.name;
    candidates << job.aliases;
    candidates << fallbackCandidates(job.name);
    QStringList unique;
    for (const QString &c : candidates)
        if (!c.trimmed().isEmpty() && !unique.contains(c, Qt::CaseInsensitive)) unique << c;
    fetchWagoCandidate(job, unique, 0);
}

void RetailReferenceService::fetchWagoCandidate(const TableJob &job, const QStringList &candidates, int candidateIndex) {
    if (candidateIndex >= candidates.size()) {
        emit progress(QStringLiteral("Wago: %1 — варианты имён исчерпаны, пробую wowdata/Blizzard CDN…").arg(job.name));
        if (!tryWowDataFallback(job)) {
            emit progress(QStringLiteral("Retail Reference: %1 — данные не получены ни из Wago, ни из wowdata; таблица помечена missing, остальные продолжаются.").arg(job.name));
            QFile f(QDir(cacheRoot(m_build)).filePath(QStringLiteral("reference-missing.txt")));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << job.name << "\t" << m_build << "\t" << m_locale << "\tWago+wowdata unavailable\n";
            }
        }
        fetchNext();
        return;
    }

    const QString candidate = candidates.at(candidateIndex);
    QUrl url(QStringLiteral("https://wago.tools/db2/%1/csv").arg(QString::fromUtf8(QUrl::toPercentEncoding(candidate))));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("build"), m_build);
    q.addQueryItem(QStringLiteral("locale"), m_locale);
    url.setQuery(q);
    emit progress(QStringLiteral("Wago: %1 [%2] …").arg(job.name, candidate));
    auto *reply = m_network.get(refRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, candidates, candidateIndex, candidate] {
        const auto err = reply->error();
        const QString errText = reply->errorString();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        const QByteArray head = data.left(512).trimmed().toLower();
        const bool looksHtml = head.startsWith("<html") || head.startsWith("<!doctype") || head.contains("not found");
        const bool bad = (err != QNetworkReply::NoError) || data.isEmpty() || looksHtml || (http >= 400);
        if (bad) {
            emit progress(QStringLiteral("Wago %1 [%2]: %3 (HTTP %4) — пробую следующий источник/alias.").arg(job.name, candidate, errText).arg(http));
            fetchWagoCandidate(job, candidates, candidateIndex + 1);
            return;
        }
        saveTable(candidate, data, m_build, job.fileName);
        QFile sf(QDir(cacheRoot(m_build)).filePath(QStringLiteral("%1.source.txt").arg(job.fileName)));
        if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&sf);
            ts << "provider=Wago\n" << "table=" << candidate << "\n" << "build=" << m_build << "\n" << "locale=" << m_locale << "\n";
        }
        emit progress(QStringLiteral("Wago: %1 получена как %2.").arg(job.name, candidate));
        fetchNext();
    });
}

bool RetailReferenceService::tryWowDataFallback(const TableJob &job) {
#ifdef Q_OS_WIN
    const QStringList names = {QStringLiteral("wowdata.exe"), QStringLiteral("wowdata.cmd"), QStringLiteral("wowdata")};
#else
    const QStringList names = {QStringLiteral("wowdata")};
#endif
    QString executable;
    for (const QString &n : names) {
        executable = QStandardPaths::findExecutable(n);
        if (!executable.isEmpty()) break;
    }
    if (executable.isEmpty()) {
        emit progress(QStringLiteral("wowdata не найден в PATH — установите @follenfang/wowdata для CDN fallback."));
        return false;
    }

    QProcess proc;
    QStringList args;
    args << QStringLiteral("db2") << QStringLiteral("rows") << job.name
         << QStringLiteral("--source") << QStringLiteral("remote")
         << QStringLiteral("--region") << QStringLiteral("eu")
         << QStringLiteral("--product") << QStringLiteral("wow")
         << QStringLiteral("--build") << m_build
         << QStringLiteral("--locale") << m_locale;
    proc.start(executable, args);
    if (!proc.waitForStarted(5000)) {
        emit progress(QStringLiteral("wowdata не удалось запустить: %1").arg(proc.errorString()));
        return false;
    }
    proc.waitForFinished(120000);
    const QByteArray out = proc.readAllStandardOutput();
    const QByteArray err = proc.readAllStandardError();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 || out.trimmed().isEmpty()) {
        const QString detail = QString::fromUtf8(err).trimmed();
        emit progress(QStringLiteral("wowdata %1: %2").arg(job.name, detail.isEmpty() ? QStringLiteral("нет данных") : detail.left(500)));
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(out);
    const QJsonObject root = doc.object();
    const QJsonObject dataObj = root.value(QStringLiteral("data")).toObject();
    const QJsonArray rows = dataObj.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) {
        emit progress(QStringLiteral("wowdata %1 вернул JSON без rows.").arg(job.name));
        return false;
    }

    QStringList header;
    QVector<QJsonObject> objects;
    for (const QJsonValue &v : rows) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        objects << o;
        for (auto it = o.begin(); it != o.end(); ++it)
            if (!header.contains(it.key())) header << it.key();
    }
    if (objects.isEmpty() || header.isEmpty()) return false;

    auto csvEscape = [](const QString &value) {
        QString v = value;
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(v);
    };
    QByteArray csv;
    csv.append(header.join(QLatin1Char(',')).toUtf8());
    csv.append('\n');
    for (const QJsonObject &o : objects) {
        QStringList vals;
        for (const QString &h : header) {
            const QJsonValue v = o.value(h);
            QString text;
            if (v.isString()) text = v.toString();
            else if (v.isDouble()) text = QString::number(v.toDouble(), 'g', 16);
            else if (v.isBool()) text = v.toBool() ? QStringLiteral("1") : QStringLiteral("0");
            else if (v.isNull() || v.isUndefined()) text.clear();
            else text = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
            vals << csvEscape(text);
        }
        csv.append(vals.join(QLatin1Char(',')).toUtf8());
        csv.append('\n');
    }
    saveTable(job.name, csv, m_build, job.fileName);
    QFile sf(QDir(cacheRoot(m_build)).filePath(QStringLiteral("%1.source.txt").arg(job.fileName)));
    if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&sf);
        ts << "provider=wowdata\n" << "table=" << job.name << "\n" << "build=" << m_build << "\n" << "locale=" << m_locale << "\n";
    }
    emit progress(QStringLiteral("wowdata: %1 получена с Blizzard CDN.").arg(job.name));
    return true;
}

void RetailReferenceService::saveTable(const QString &, const QByteArray &data, const QString &build, const QString &fileName) {
    QFile f(QDir(cacheRoot(build)).filePath(fileName));
    if (!f.open(QIODevice::WriteOnly)) {
        emit failed(QStringLiteral("Не удалось записать reference cache: %1").arg(f.fileName()));
        return;
    }
    f.write(data);
}

QStringList RetailReferenceService::parseCsvRow(const QString &line) {
    QStringList row;
    QString field;
    bool quotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { field += QLatin1Char('"'); ++i; }
                else quotes = false;
            } else field += c;
        } else {
            if (c == QLatin1Char('"')) quotes = true;
            else if (c == QLatin1Char(',')) { row << field; field.clear(); }
            else field += c;
        }
    }
    row << field;
    return row;
}

QStringList RetailReferenceService::csvHeader(const QByteArray &data) {
    QString s = QString::fromUtf8(data.left(data.indexOf('\n') >= 0 ? data.indexOf('\n') : data.size()));
    if (s.startsWith(QChar(0xFEFF))) s.remove(0,1);
    return parseCsvRow(s.trimmed());
}

QVector<QStringList> RetailReferenceService::readCsvFile(const QString &path, int maxRows) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray raw = f.readAll();
    QString text = QString::fromUtf8(raw);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0,1);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    QVector<QStringList> rows;
    const int limit = maxRows > 0 ? qMin(maxRows + 1, lines.size()) : lines.size();
    for (int i = 0; i < limit; ++i) rows.push_back(parseCsvRow(lines.at(i)));
    return rows;
}

int RetailReferenceService::columnIndex(const QStringList &header, const QStringList &aliases) {
    for (int i = 0; i < header.size(); ++i) {
        QString h = header.at(i).trimmed().toLower();
        h.remove('_'); h.remove('-'); h.remove('.');
        for (QString a : aliases) {
            a = a.toLower(); a.remove('_'); a.remove('-'); a.remove('.');
            if (h == a) return i;
        }
    }
    return -1;
}

QString RetailReferenceService::scalar(const QStringList &row, int idx) {
    return (idx >= 0 && idx < row.size()) ? row.at(idx).trimmed() : QString();
}

void RetailReferenceService::enrichCatalog(QaRetailCatalog *catalog, const QString &build) const {
    if (!catalog) return;
    const QString dir = cacheRoot(build.trimmed().isEmpty() ? defaultBuild() : build.trimmed());
    const QString creaturePath = QDir(dir).filePath(QStringLiteral("Creature.csv"));
    const auto rows = readCsvFile(creaturePath, 0);
    if (rows.size() < 2) {
        catalog->note += QStringLiteral("\nWago Reference: cache не найден — сначала выполните синхронизацию build %1.").arg(build);
        return;
    }
    const auto header = rows.first();
    const int idCol = columnIndex(header, {QStringLiteral("ID"), QStringLiteral("Id")});
    const int nameCol = columnIndex(header, {QStringLiteral("Name"), QStringLiteral("Name_lang"), QStringLiteral("Name_lang_enUS")});
    const int displayCol = columnIndex(header, {QStringLiteral("CreatureDisplayID"), QStringLiteral("DisplayID")});
    int updated = 0;
    QHash<int, QaRetailId*> byId;
    for (auto &n : catalog->npcs) byId.insert(n.id, &n);
    for (int i = 1; i < rows.size(); ++i) {
        const auto &row = rows.at(i);
        const int id = scalar(row, idCol).toInt();
        auto it = byId.find(id);
        if (id <= 0 || it == byId.end()) continue;
        QaRetailId *npc = it.value();
        npc->hasWago = true;
        npc->wagoName = scalar(row, nameCol);
        npc->wagoDisplayId = scalar(row, displayCol).toLongLong();
        npc->wagoEvidence = QStringLiteral("Wago DB2 Creature, build %1").arg(build);
        if (npc->name.isEmpty() && !npc->wagoName.isEmpty()) npc->name = npc->wagoName;
        ++updated;
    }
    int spellMarked = 0;
    QHash<int, QString> wagoSpellEvidence;
    const QString spellPath = QDir(dir).filePath(QStringLiteral("Spell.csv"));
    const auto spellRows = readCsvFile(spellPath, 0);
    if (spellRows.size() >= 2) {
        const auto sh = spellRows.first();
        const int sid = columnIndex(sh, {QStringLiteral("ID"), QStringLiteral("SpellID")});
        for (int i = 1; i < spellRows.size(); ++i) {
            const int id = scalar(spellRows.at(i), sid).toInt();
            if (id > 0) wagoSpellEvidence.insert(id, QStringLiteral("Wago"));
        }
        for (auto &sp : catalog->spells) {
            if (!wagoSpellEvidence.contains(sp.id)) continue;
            sp.wagoEvidence = QStringLiteral("Wago DB2 Spell, build %1").arg(build);
            sp.hasWago = true;
            ++spellMarked;
        }
    }
    catalog->note += QStringLiteral("\nWago Reference %1: Creature cache найден, NPC обогащено: %2; Spell IDs подтверждено Wago: %3.")
                         .arg(build).arg(updated).arg(spellMarked);
    catalog->source += QStringLiteral(" + WagoDB2/%1").arg(build);
}
