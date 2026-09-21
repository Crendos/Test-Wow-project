#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

struct CoreDetection {
    QString profile;      // combo value, e.g. "TrinityCore 3.3.5a"
    QString branch;       // "3.3.5" / "master"
    QString patch;        // human-readable patch / TDB / build
    QString clientBuild;  // патч wago.tools, например "7.3.5.26365"
    int gameBuild = 0;    // сырой realmlist.gamebuild (26365)
    QString coreName;     // TrinityCore / CypherCore / AzerothCore / …
    QStringList evidence;
    int confidence = 0;

    QString summary() const;
};

class CoreDetectService final {
public:
    static CoreDetection fromDatabase(const QSqlDatabase &db);
    static CoreDetection fromFolder(const QString &path);
    static CoreDetection merge(CoreDetection a, const CoreDetection &b);
};
