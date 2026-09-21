#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>

struct CoreRepository { QString displayName; QString gitUrl; QString apiUrl; QString readmeUrl; QString language; };
class CoreRepositoryService final : public QObject {
    Q_OBJECT
public:
    explicit CoreRepositoryService(QObject *parent = nullptr);
    static CoreRepository trinity();
    static CoreRepository cypher();
    static CoreRepository nordrassil();
    static CoreRepository forProfile(const QString &profile);
    void fetchBranches(const CoreRepository &repo);
    void fetchPatchInfo(const CoreRepository &repo);
signals:
    void branchesLoaded(const QStringList &branches);
    void failed(const QString &message);
    void patchInfoLoaded(const QString &text);
private: QNetworkAccessManager m_network;
};
