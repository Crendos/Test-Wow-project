#include "network_guard.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

NetworkGuard::NetworkGuard(QObject *parent) : QObject(parent) {}
void NetworkGuard::check() {
    // GET, не HEAD: Cloudflare на wago.tools часто закрывает HEAD — Qt пишет
    // «QIODevice::read (QNetworkReplyHttpImpl): device not open».
    QNetworkRequest req(QUrl(QStringLiteral("https://wago.tools/db2")));
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    req.setRawHeader("Accept-Encoding", "identity");
    req.setTransferTimeout(7000);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_online = reply->error() == QNetworkReply::NoError
                   || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() > 0;
        if (reply->isOpen() || reply->bytesAvailable() > 0)
            reply->readAll();
        emit changed(m_online, m_online ? tr("Сеть доступна") : tr("Нет сети: %1").arg(reply->errorString()));
        reply->deleteLater();
    });
}
