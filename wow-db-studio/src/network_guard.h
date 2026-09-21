#pragma once
#include <QObject>
#include <QNetworkAccessManager>

class NetworkGuard final : public QObject {
    Q_OBJECT
public:
    explicit NetworkGuard(QObject *parent = nullptr);
    void check();
    bool online() const { return m_online; }
signals:
    void changed(bool online, const QString &details);
private:
    QNetworkAccessManager m_network;
    bool m_online = false;
};
