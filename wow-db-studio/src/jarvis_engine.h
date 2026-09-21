#pragma once
#include <QString>
#include <QStringList>

// Result of a local (offline) analysis by Jarvis.
struct JarvisResult {
    QStringList reasoning;     // visible "thinking" steps
    QString answer;            // composed answer text
    QStringList suggestedUrls; // Wowhead / reference links to open in the browser
};

// Offline reasoning engine for the integrated assistant "Jarvis".
// Deterministic, rule-based + retrieval: no network, no external model.
// It tokenizes text, understands words (RU/EN stemming), classifies intent,
// ranks the local knowledge base, and composes an answer with a visible
// reasoning trace. Web access is delegated to the user's browser.
class JarvisEngine final {
public:
    JarvisEngine();
    JarvisResult analyze(const QString &task, const QString &script,
                         const QString &error, const QString &core,
                         const QString &knowledge, const QString &project,
                         const QString &schema) const;
    JarvisResult analyzeServerLogs(const QString &worldLog, const QString &realmLog,
                                   const QString &fileDump, const QString &core) const;

    static QStringList tokenize(const QString &text);
    static QString stem(const QString &word);

private:
    struct Intent { QString label; QStringList keywords; QString wowheadPrefix; };
    Intent detectIntent(const QString &text, const QString &core) const;
    QString triageSql(const QString &text) const;
    QString triageCompile(const QString &text) const;
    QString triageServer(const QString &text) const;
    QString analyzeSmartAi(const QString &task, const QString &script, const QString &error, const QString &core) const;
    QString coreProfile(const QString &core) const;
    QString smartAiReference(const QString &core) const;
    QString smartAiRecipes(const QString &task, const QString &core) const;
    QString wdbxHowto() const;
    QString tableGuide(const QString &text) const;
    QStringList extractLogHits(const QString &text) const;
    QStringList extractIds(const QString &text) const;
};
