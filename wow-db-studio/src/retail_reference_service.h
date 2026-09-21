#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QVector>
#include <QStringList>
#include "qa_retail_source.h"

// Online, client-free retail reference sync for QA.
// Uses public Wago DB2 CSV exports; no WoW client or WPP is required.
class RetailReferenceService final : public QObject {
    Q_OBJECT
public:
    explicit RetailReferenceService(QObject *parent = nullptr);

    static QString defaultBuild();
    static QString cacheRoot(const QString &build = defaultBuild());
    static QString help();

    // Fetches a broad QA-oriented DB2 set and caches it locally; per-table fallbacks prevent one 404 from aborting the sync.
    void setLocale(const QString &locale) { m_locale = locale.trimmed().isEmpty() ? QStringLiteral("ruRU") : locale.trimmed(); }
    QString locale() const { return m_locale; }
    void syncBuild(const QString &build = defaultBuild());
    void enrichCatalog(QaRetailCatalog *catalog, const QString &build = defaultBuild()) const;
    bool hasCachedBuild(const QString &build = defaultBuild()) const;
    QString cachePath(const QString &build = defaultBuild()) const;
    static bool resolveScopeFromCache(const QString &build, const QString &query, int *mapId, int *zoneId, QString *canonicalName);

signals:
    void progress(const QString &message);
    void finished(bool ok, const QString &message, const QString &build);
    void failed(const QString &message);

private:
    struct TableJob { QString name; QString fileName; QStringList aliases; bool optional = false; };
    QNetworkAccessManager m_network;
    QVector<TableJob> m_jobs;
    int m_index = 0;
    QString m_build;
    QString m_locale = QStringLiteral("ruRU");
    qint64 m_startedMs = 0;

    void fetchNext();
    void fetchWagoCandidate(const TableJob &job, const QStringList &candidates, int candidateIndex);
    bool tryWowDataFallback(const TableJob &job);
    static QStringList fallbackCandidates(const QString &name);
    void saveTable(const QString &table, const QByteArray &data, const QString &build, const QString &fileName);
    static QStringList csvHeader(const QByteArray &data);
    static QStringList parseCsvRow(const QString &line);
    static QVector<QStringList> readCsvFile(const QString &path, int maxRows = 0);
    static int columnIndex(const QStringList &header, const QStringList &aliases);
    static QString scalar(const QStringList &row, int idx);
};
