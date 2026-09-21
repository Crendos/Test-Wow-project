#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>

/** Online assistant via an OpenAI-compatible chat-completions endpoint.
 * API keys are kept only in memory; use a provider endpoint you are permitted to use. */
class AiService final : public QObject {
    Q_OBJECT
public:
    explicit AiService(QObject *parent = nullptr);
    void configure(QUrl endpoint, QString model, QString apiKey);
    void ask(const QString &task, const QString &script, const QString &error,
             const QString &core, const QString &wowheadContext = {}, const QString &localKnowledge = {}, const QString &projectContext = {}, const QString &schemaContext = {});
    void inspectWowhead(const QUrl &url);
signals:
    void answerReady(const QString &text);
    void failed(const QString &reason);
    void wowheadReady(const QString &context);
private:
    QNetworkAccessManager m_network;
    QUrl m_endpoint;
    QString m_model;
    QString m_key;
    static QString htmlToContext(const QByteArray &html, const QUrl &source);
};
