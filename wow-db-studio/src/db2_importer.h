#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QSqlDatabase>
#include <QUrl>

class Db2Importer final : public QObject {
    Q_OBJECT
public:
    explicit Db2Importer(QObject *parent = nullptr);
    // Downloads a user-supplied public JSON/CSV export and imports selected fields.
    void importUrl(const QUrl &url, const QString &table, QSqlDatabase db);
signals:
    void finished(bool ok, const QString &message);
private:
    QNetworkAccessManager m_network;
    bool importJson(const QByteArray &, const QString &, QSqlDatabase, QString *);
    bool importCsv(const QByteArray &, const QString &, QSqlDatabase, QString *);
};
