#include "wago_service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

WagoService::WagoService(QObject *parent) : QObject(parent) {}

bool WagoService::loadDiskCache() {
    QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
    m_cachedBuild = s.value(QStringLiteral("wago/cacheBuild")).toString();
    const QStringList tables = s.value(QStringLiteral("wago/cacheTables")).toStringList();
    const QStringList builds = s.value(QStringLiteral("wago/cacheBuilds")).toStringList();
    if (!tables.isEmpty()) m_tables = tables;
    if (!builds.isEmpty()) m_builds = builds;
    return !m_tables.isEmpty();
}

void WagoService::saveDiskCache(const QString &build) const {
    QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
    if (!build.isEmpty()) s.setValue(QStringLiteral("wago/cacheBuild"), build);
    s.setValue(QStringLiteral("wago/cacheTables"), m_tables);
    s.setValue(QStringLiteral("wago/cacheBuilds"), m_builds);
}

QString WagoService::normalizeKey(const QString &s) {
    QString out = s.toLower();
    out.remove('_');
    out.remove('-');
    out.remove(' ');
    if (out.endsWith(QLatin1String("lang"))) out.chop(4);
    if (out.endsWith(QLatin1String("enus"))) out.chop(4);
    return out;
}

static QString unescapeHtml(const QString &s) {
    QString out = s;
    out.replace("&quot;", "\"");
    out.replace("&#39;", "'");
    out.replace("&#x27;", "'");
    out.replace("&amp;", "&");
    out.replace("&lt;", "<");
    out.replace("&gt;", ">");
    out.replace("&#x2F;", "/");
    return out;
}

static QJsonObject inertiaProps(const QString &html) {
    QString raw;
    QRegularExpression dq("data-page=\"([^\"]*)\"");
    const auto m = dq.match(html);
    if (m.hasMatch()) raw = m.captured(1);
    else {
        QRegularExpression sq("data-page='([^']*)'");
        const auto m2 = sq.match(html);
        if (!m2.hasMatch()) return {};
        raw = m2.captured(1);
    }
    return QJsonDocument::fromJson(unescapeHtml(raw).toUtf8()).object().value("props").toObject();
}

static bool looksLikeTableName(const QString &s) {
    if (s.size() < 2 || s.size() > 80) return false;
    if (s.contains('.') || s.contains('/') || s.contains('\\')) return false;
    static const QRegularExpression re("^[A-Za-z][A-Za-z0-9_]*$");
    return re.match(s).hasMatch();
}

static void appendTable(QStringList &out, const QString &n) {
    const QString t = n.trimmed();
    if (looksLikeTableName(t) && !out.contains(t, Qt::CaseInsensitive))
        out << t;
}

static QStringList jsonToTables(const QJsonValue &v) {
    QStringList out;
    if (v.isArray()) {
        for (const auto &x : v.toArray()) {
            if (x.isString()) appendTable(out, x.toString());
            else if (x.isObject()) {
                const auto o = x.toObject();
                QString n = o.value("name").toString();
                if (n.isEmpty()) n = o.value("table").toString();
                if (n.isEmpty()) n = o.value("id").toString();
                appendTable(out, n);
            }
        }
    } else if (v.isObject()) {
        const auto o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            // PHP sparse arrays become JSON objects: {"0":"Achievement","1":"Achievement_Category",...}
            if (it.value().isString()) appendTable(out, it.value().toString());
            else if (it.value().isObject()) {
                const auto inner = it.value().toObject();
                QString n = inner.value("name").toString();
                if (n.isEmpty()) n = inner.value("table").toString();
                if (n.isEmpty()) n = inner.value("id").toString();
                appendTable(out, n);
            }
            appendTable(out, it.key());
        }
    } else if (v.isString()) {
        appendTable(out, v.toString());
    }
    return out;
}

static QStringList tablesFromHtml(const QString &html) {
    QStringList out;
    QRegularExpression re(QStringLiteral("/db2/([A-Za-z][A-Za-z0-9_]*)"));
    auto it = re.globalMatch(html);
    while (it.hasNext()) appendTable(out, it.next().captured(1));
    return out;
}

static QStringList extractTables(const QJsonObject &props, const QString &html = {}) {
    const char *keys[] = {"tables", "availableTables", "db2Tables", "tableNames", "db2"};
    QStringList best;
    for (const char *k : keys) {
        const auto list = jsonToTables(props.value(QLatin1String(k)));
        if (list.size() > best.size()) best = list;
    }
    if (best.size() < 10) {
        for (auto it = props.begin(); it != props.end(); ++it) {
            const auto list = jsonToTables(it.value());
            if (list.size() > best.size()) best = list;
        }
    }
    if (best.size() < 10 && !html.isEmpty()) {
        const auto href = tablesFromHtml(html);
        if (href.size() > best.size()) best = href;
    }
    best.sort(Qt::CaseInsensitive);
    return best;
}

static QStringList extractVersions(const QJsonObject &props) {
    QStringList out;
    const auto versions = props.value("versions").toArray();
    for (const auto &v : versions) {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    if (out.isEmpty()) {
        const auto obj = props.value("versions").toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().isString()) out << it.value().toString();
            else if (looksLikeTableName(it.key()) == false && it.key().contains('.'))
                out << it.key();
        }
    }
    if (out.isEmpty()) {
        const QString current = props.value("version").toString();
        if (current.isEmpty()) {
            const QString cv = props.value("currentVersion").toString();
            if (!cv.isEmpty()) out << cv;
        } else out << current;
    }
    return out;
}

QString WagoService::resolveTableName(const QString &typed, const QStringList &known) {
    const QString t = typed.trimmed();
    if (t.isEmpty()) return t;
    for (const auto &k : known) if (k == t) return k;
    for (const auto &k : known) if (k.compare(t, Qt::CaseInsensitive) == 0) return k;
    const QString n = normalizeKey(t);
    for (const auto &k : known)
        if (normalizeKey(k) == n) return k;
    return t;
}

QString WagoService::resolveBuild(const QString &typed, const QStringList &known) {
    QString t = typed.trimmed();
    if (t.isEmpty()) return t;
    t.replace(QLatin1String("3.3.5a."), QLatin1String("3.3.5."), Qt::CaseInsensitive);
    t.replace(QLatin1String("3.3.5a"), QLatin1String("3.3.5"), Qt::CaseInsensitive);
    if (t.startsWith(QLatin1Char('v')) || t.startsWith(QLatin1Char('V')))
        t = t.mid(1);
    auto findExact = [&](const QString &s) -> QString {
        for (const auto &k : known) if (k == s) return k;
        for (const auto &k : known) if (k.compare(s, Qt::CaseInsensitive) == 0) return k;
        return {};
    };
    if (const QString hit = findExact(t); !hit.isEmpty()) return hit;

    QString prefix = t;
    if (!prefix.endsWith(QLatin1Char('.'))) prefix += QLatin1Char('.');
    for (const auto &k : known) {
        if (k.startsWith(prefix, Qt::CaseInsensitive) || k.startsWith(t + QLatin1Char(' '), Qt::CaseInsensitive))
            return k;
    }

    QString last = t;
    const int dot = t.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0) last = t.mid(dot + 1);
    bool ok = false;
    const int n = last.toInt(&ok);
    if (ok && n > 0) {
        const QString suffix = QLatin1Char('.') + QString::number(n);
        for (const auto &k : known) {
            if (k == last || k.endsWith(suffix))
                return k;
        }
    }
    return t;
}

bool WagoService::parsePageUrl(const QString &text, QString *table, QString *build, QString *locale) {
    const QString s = text.trimmed();
    if (!s.contains(QLatin1String("wago.tools"), Qt::CaseInsensitive)) return false;
    const QUrl u = QUrl::fromUserInput(s);
    if (!u.isValid()) return false;
    const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.at(0).compare(QLatin1String("db2"), Qt::CaseInsensitive) != 0)
        return false;
    if (table && parts.at(1).compare(QLatin1String("csv"), Qt::CaseInsensitive) != 0)
        *table = parts.at(1);
    const QUrlQuery q(u);
    if (build && q.hasQueryItem(QStringLiteral("build")))
        *build = q.queryItemValue(QStringLiteral("build"));
    if (locale && q.hasQueryItem(QStringLiteral("locale")))
        *locale = q.queryItemValue(QStringLiteral("locale"));
    return table ? !table->isEmpty() : true;
}

static QVector<QStringList> parseCsv(const QByteArray &text, int maxRows, int *totalDataRows = nullptr) {
    QString s = QString::fromUtf8(text);
    if (s.startsWith(QChar(0xFEFF))) s.remove(0, 1);
    QVector<QStringList> result;
    QStringList row;
    QString field;
    bool inQuotes = false;
    int dataRows = 0;
    auto takeRow = [&] {
        const bool emptyRow = row.size() == 1 && row[0].isEmpty();
        if (emptyRow) { row.clear(); return; }
        if (result.isEmpty()) {
            result << row;
        } else {
            ++dataRows;
            if (maxRows <= 0 || result.size() < maxRows + 1)
                result << row;
        }
        row.clear();
    };
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') { field += '"'; ++i; continue; }
                inQuotes = false; continue;
            }
            field += c;
        } else {
            if (c == '"') { inQuotes = true; continue; }
            if (c == ',') { row << field; field.clear(); continue; }
            if (c == '\r' || c == '\n') {
                row << field; field.clear();
                takeRow();
                if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
                continue;
            }
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) { row << field; takeRow(); }
    if (totalDataRows) *totalDataRows = dataRows;
    return result;
}

static QNetworkRequest wagoRequest(const QUrl &url, int timeoutMs) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,text/csv,application/json;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Encoding", "identity");
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);
    return req;
}

static QByteArray readReplyBody(QNetworkReply *r) {
    if (!r) return {};
    if (!r->isOpen() && r->bytesAvailable() <= 0) return {};
    return r->readAll();
}

void WagoService::fetchBuilds() {
    auto *r = m_network.get(wagoRequest(QUrl("https://wago.tools/db2"), 45000));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        const auto netErr = r->error();
        const QString errStr = r->errorString();
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(readReplyBody(r));
        r->deleteLater();
        if (netErr != QNetworkReply::NoError) {
            emit failed(QStringLiteral("wago.tools: %1 (HTTP %2)").arg(errStr).arg(http));
            return;
        }
        const auto props = inertiaProps(body);
        const auto versions = extractVersions(props);
        if (versions.isEmpty()) { emit failed("wago.tools: список build'ов пуст (не найден data-page)."); return; }
        m_builds = versions;
        emit buildsLoaded(versions);
        const auto tables = extractTables(props, body);
        if (!tables.isEmpty()) {
            m_tables = tables;
            const QString ver = props.value(QStringLiteral("currentVersion")).toString();
            saveDiskCache(ver);
            emit tablesLoaded(tables);
        }
    });
}

void WagoService::fetchTables(const QString &build) {
    const QString resolved = resolveBuild(build.trimmed(), m_builds);
    QUrl url("https://wago.tools/db2");
    if (!resolved.isEmpty()) {
        QUrlQuery q; q.addQueryItem("build", resolved);
        url.setQuery(q);
    }
    auto *r = m_network.get(wagoRequest(url, 20000));
    connect(r, &QNetworkReply::finished, this, [this, r, build, resolved] {
        const auto netErr = r->error();
        const QString errStr = r->errorString();
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(readReplyBody(r));
        r->deleteLater();
        if (netErr != QNetworkReply::NoError) {
            emit failed(QStringLiteral("wago.tools: %1 (HTTP %2)").arg(errStr).arg(http));
            return;
        }
        if (body.contains(QLatin1String("<title>Not Found</title>"), Qt::CaseInsensitive)) {
            emit failed(QString::fromUtf8(
                "wago.tools: патч «%1» не найден. Нужен полный build, например 7.3.5.26365 (не просто 7.3.5).")
                            .arg(build));
            return;
        }
        const auto tables = extractTables(inertiaProps(body), body);
        if (tables.isEmpty())
            emit failed(QString::fromUtf8(
                "wago.tools: список таблиц патча %1 пуст. Введите имя как на сайте (Achievement) "
                "или с вашей базы (achievement) — регистр не важен.")
                            .arg(resolved.isEmpty() ? build : resolved));
        else {
            m_tables = tables;
            m_cachedBuild = resolved.isEmpty() ? build : resolved;
            saveDiskCache(m_cachedBuild);
            emit tablesLoaded(tables);
        }
    });
}

void WagoService::fetchTable(const QString &build, const QString &table, int maxRows, const QString &locale) {
    const QString resolvedBuild = resolveBuild(build.trimmed(), m_builds);
    QString name = table.trimmed();
    QString urlTable, urlBuild, urlLocale;
    if (parsePageUrl(name, &urlTable, &urlBuild, &urlLocale))
        name = urlTable;
    name = resolveTableName(name, m_tables);

    QUrl url("https://wago.tools/db2/" + QString::fromUtf8(QUrl::toPercentEncoding(name)) + "/csv");
    QUrlQuery q;
    q.addQueryItem("build", urlBuild.isEmpty() ? resolvedBuild : urlBuild);
    const QString loc = !urlLocale.isEmpty() ? urlLocale
                      : (locale.trimmed().isEmpty() ? QStringLiteral("enUS") : locale.trimmed());
    q.addQueryItem("locale", loc);
    url.setQuery(q);
    auto *r = m_network.get(wagoRequest(url, 60000));
    connect(r, &QNetworkReply::finished, this, [this, r, resolvedBuild, name, maxRows, loc] {
        const auto netErr = r->error();
        const QString errStr = r->errorString();
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = readReplyBody(r);
        r->deleteLater();
        if (netErr != QNetworkReply::NoError) {
            emit tableLoaded(false, QString::fromUtf8("Ошибка загрузки %1 (build %2, %3): %4 (HTTP %5)")
                                        .arg(name, resolvedBuild, loc, errStr)
                                        .arg(http));
            return;
        }
        const QByteArray head = data.left(64).trimmed().toLower();
        if (head.startsWith("<!doctype") || head.startsWith("<html") || head.startsWith("<title>not found")) {
            emit tableLoaded(false, QString::fromUtf8(
                "Таблица «%1» не найдена на wago.tools для build %2. "
                "На сайте имя в PascalCase (Achievement), в hotfixes — строчными (achievement). "
                "Нажмите «Таблицы этого патча» или вставьте URL страницы.")
                                        .arg(name, resolvedBuild));
            return;
        }
        int totalRows = 0;
        const auto parsed = parseCsv(data, maxRows, &totalRows);
        if (parsed.size() < 2) {
            m_columns.clear();
            m_rows.clear();
            m_totalRows = 0;
            emit tableLoaded(false, QString::fromUtf8("Таблица %1 пуста или CSV не распознан.").arg(name));
            return;
        }
        m_columns = parsed.first();
        m_rows = parsed;
        m_rows.removeFirst();
        m_totalRows = totalRows;
        emit tableLoaded(true, QString::fromUtf8(
            "Wago «%1» (build %2, %3): %4 колонок, %5 строк на сайте, скачано %6%7.")
                                   .arg(name, resolvedBuild, loc)
                                   .arg(m_columns.size()).arg(m_totalRows).arg(m_rows.size())
                                   .arg(maxRows <= 0 ? QString::fromUtf8(" (все)")
                                                     : QString::fromUtf8(" (лимит %1)").arg(maxRows)));
    });
}
