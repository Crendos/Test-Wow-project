#pragma once
#include <QString>
#include <QVector>

struct KnowledgeEntry { QString title; QString text; QString tags; };

// Local, inspectable learning memory. It is retrieval memory, not opaque weight training.
class KnowledgeBase final {
public:
    KnowledgeBase();
    bool add(const KnowledgeEntry &entry, QString *error = nullptr);
    QString search(const QString &query, int maxEntries = 5, int maxChars = 7000) const;
    int count() const { return m_entries.size(); }
private:
    QString m_path;
    QVector<KnowledgeEntry> m_entries;
    void load();
    bool save(QString *error) const;
    void addSeedKnowledge();
};
