#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QJsonArray>
#include <QUrl>
#include "qa_retail_source.h"

class RetailCommunityReferenceService final : public QObject {
    Q_OBJECT
public:
    explicit RetailCommunityReferenceService(QObject *parent = nullptr);

    static QString defaultBuild();
    static QString cacheRoot(const QString &build = defaultBuild());
    static QString help();

    void syncAll(const QString &build = defaultBuild());
    bool hasCachedBuild(const QString &build = defaultBuild()) const;
    QString cachePath(const QString &build = defaultBuild()) const;
    QaCommunityReference load(const QString &build = defaultBuild()) const;
    void enrichCatalog(QaRetailCatalog *catalog, const QString &build = defaultBuild()) const;

signals:
    void progress(const QString &message);
    void finished(bool ok, const QString &message, const QString &build);
    void failed(const QString &message);

private:
    struct Repo {
        QString key;
        QString owner;
        QString name;
        QString branch;
        QString prefix;
    };
    struct FileJob {
        QString source;
        QString owner;
        QString repo;
        QString branch;
        QString path;
        QString url;
    };

    QNetworkAccessManager m_network;
    QString m_build;
    QVector<Repo> m_repos;
    QVector<FileJob> m_files;
    int m_repoIndex = 0;
    int m_fileIndex = 0;

    void fetchRepoTree(const Repo &repo);
    void fetchNextFile();
    void finalizeSync();

    static QNetworkRequest request(const QUrl &url, const QByteArray &accept = "application/json,text/plain,*/*");
    static QString safeFileName(const QString &path);
    static QStringList filesForSource(const Repo &repo, const QJsonArray &tree);
    static void saveText(const QString &path, const QByteArray &data);
    static QVector<QStringList> readFilesInSource(const QString &root);
    static void parseMdt(const QString &text, const QString &fileName, QaCommunityReference *out);
    static void parseSpellIds(const QString &text, const QString &source, const QString &context, QaCommunityReference *out);
    static void parseBigWigs(const QString &text, const QString &context, QaCommunityReference *out);
    static void parseAtt(const QString &text, const QString &context, QaCommunityReference *out);
    static void parseKeystoneJson(const QString &text, const QString &fileName, QaCommunityReference *out);
    static void walkKeystoneJson(const QJsonValue &value, QaCommunityReference *out);
    static void addSpell(QaCommunityReference *out, int spellId, const QString &source, const QStringList &flags = {}, const QString &context = {});
    static void finalizeScores(QaCommunityReference *out);
    static QString summary(const QaCommunityReference &r);
};
