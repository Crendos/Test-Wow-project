#pragma once
#include <QString>
#include <QSqlDatabase>

// Read-only snapshot of MySQL metadata. No game rows are sent unless a later feature explicitly adds that.
class SchemaContext final {
public:
    static QString collect(const QSqlDatabase &db, const QString &task, QString *error = nullptr);
};
