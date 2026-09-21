#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>

struct DbProfile {
    QString host = "127.0.0.1";
    int port = 3306;
    QString database;
    QString user;
    QString password;
    QString core = "TrinityCore 3.3.5a";
};

class DatabaseService final : public QObject {
    Q_OBJECT
public:
    explicit DatabaseService(QObject *parent = nullptr);
    ~DatabaseService() override;
    bool connectTo(const DbProfile &profile, QString *error);
    void disconnect();
    bool isOpen() const;
    QStringList tables(QString *error) const;
    bool execute(const QString &sql, QString *error, qint64 *affected = nullptr);
    QSqlDatabase database() const;
    // Lists all databases visible to the connected user (SHOW DATABASES).
    QStringList databases(QString *error) const;
private:
    const QString m_connectionName = "wowdbstudio";
};
