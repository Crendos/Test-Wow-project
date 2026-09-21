#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QVector>

// Fetches DB2 data from wago.tools for a specific build (patch).
// Only public, permitted read-only endpoints are used:
//   - https://wago.tools/db2            -> list of builds (props.versions) and tables
//   - https://wago.tools/db2?build=...  -> tables available for that patch
//   - https://wago.tools/db2/{table}/csv?build={build}&locale=...  -> CSV export
// SQL import is performed by the caller (MainWindow) with explicit column mapping.
class WagoService final : public QObject {
    Q_OBJECT
public:
    explicit WagoService(QObject *parent = nullptr);

    void fetchBuilds();
    void fetchTables(const QString &build);
    void fetchTable(const QString &build, const QString &table, int maxRows = 5000,
                    const QString &locale = QStringLiteral("enUS"));

    void setKnownTables(const QStringList &tables) { m_tables = tables; }
    void setKnownBuilds(const QStringList &builds) { m_builds = builds; }
    bool loadDiskCache();
    void saveDiskCache(const QString &build) const;
    QString cachedBuild() const { return m_cachedBuild; }

    const QStringList &columns() const { return m_columns; }
    const QVector<QStringList> &rows() const { return m_rows; }
    int totalRowCount() const { return m_totalRows; }
    const QStringList &knownTables() const { return m_tables; }
    const QStringList &knownBuilds() const { return m_builds; }

    static QString normalizeKey(const QString &s);
    static QString resolveTableName(const QString &typed, const QStringList &known);
    static QString resolveBuild(const QString &typed, const QStringList &known);
    static bool parsePageUrl(const QString &text, QString *table, QString *build, QString *locale);

signals:
    void buildsLoaded(const QStringList &builds);
    void tablesLoaded(const QStringList &tables);
    void tableLoaded(bool ok, const QString &message);
    void failed(const QString &message);

private:
    QNetworkAccessManager m_network;
    QStringList m_columns;
    int m_totalRows = 0;
    QStringList m_tables;
    QStringList m_builds;
    QString m_cachedBuild;
    QVector<QStringList> m_rows;
};
