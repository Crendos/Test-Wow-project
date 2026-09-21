#pragma once
#include <QString>
#include <QVector>
#include "qa_retail_source.h"
class DatabaseService;

struct QaFix {
    QString bot;       // имя бота
    QString problem;   // что сломано
    QString retail;    // как на ретейле
    QString sql;       // пусто = править руками (нет данных)
    QString schema;    // world / hotfixes
};

struct QaRunResult {
    QString report;
    QVector<QaFix> fixes;
};

class QaBotService {
public:
    static const char *kGeodesist;   // координаты / карта / бродяжничество
    static const char *kBestiary;    // NPC / модели / имена
    static const char *kQuest;       // квесты
    static const char *kQuartermaster; // предметы / лут / вендор
    static const char *kDirector;    // SmartAI / маршруты

    static QaRunResult run(DatabaseService &db, const QString &hotfixSchema,
                           const QString &task, const QString &serverLogs,
                           const QaRetailCatalog &catalog = {});
};
