#pragma once
#include "qa_retail_source.h"
#include <QString>

// Сниффы: SQL, который пишет WowPacketParser (WPP), плюс выгрузки TDB.
// Публичного «API сниффов» нет — боты читают локальную папку / HTTPS raw SQL.
class QaSniffSource {
public:
    static QString sourcesHelp();
    static void mergeFromFolder(const QString &dir, QaRetailCatalog *cat);
    static void mergeFromText(const QString &sql, const QString &origin, QaRetailCatalog *cat);
};
