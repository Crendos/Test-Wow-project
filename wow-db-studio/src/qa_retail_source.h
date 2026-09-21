#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QVector>
#include <QStringList>
#include <QHash>

struct QaTomTomPin {
    double x = 0; // 0..100, как /way
    double y = 0;
};

struct QaWorldPin {
    double x = 0, y = 0, z = 0, o = 0;
};

struct QaRetailId {
    int id = 0;
    QString name;
    double tomtomX = 0;
    double tomtomY = 0;
    bool hasTomTom = false;
    QVector<QaTomTomPin> pins; // несколько точек = патруль или несколько спавнов
    // Снифф / TDB-ярды (важнее TomTom)
    bool hasWorld = false;
    double worldX = 0, worldY = 0, worldZ = 0, worldO = 0;
    int sniffMap = 0;
    int sniffMove = -1; // MovementType из сниффа, -1 = нет
    QVector<QaWorldPin> sniffWaypoints;
    QVector<QaWorldPin> sniffWorldPins; // все наблюдавшиеся точки этого Entry, а не только последняя
    // Online reference data (Wago DB2), exact build used for QA comparison.
    bool hasWago = false;
    QString wagoName;
    qint64 wagoDisplayId = 0;
    QString wagoEvidence;
};

// Границы зоны в ярдах (как WorldMapArea / UiMapAssignment.Region).
struct QaMapBounds {
    int mapId = 0;
    double locTop = 0;     // север, world X max
    double locBottom = 0;  // юг, world X min
    double locLeft = 0;    // запад, world Y max
    double locRight = 0;   // восток, world Y min
    bool ok = false;
};



struct QaAuditScope {
    QString kind = QStringLiteral("auto"); // auto / location / zone / dungeon / raid
    QString name;
    int mapId = 0;
    int zoneId = 0;
    bool enabled = false;
    bool includeNpcObjects = true;
    bool includeQuests = true;
    bool includeNpcAbilities = true;
    bool includeMovementAndScripts = true;
    bool includePlayerClasses = false; // classes/talents stay in the separate QA flow
};

struct QaCommunityPoint {
    int sourceDungeonId = 0;
    int entityId = 0;
    int mapId = 0;
    double x = 0;
    double y = 0;
    double z = 0;
    bool hasZ = false;
    QString coordinateSpace; // world / mdt / tomtom_pct / keystone_guru / unknown
    QString source;           // MDT, WPP, Wowhead, ...
    double weight = 0.0;
    QString label;
    bool hasConsensus = false;
    double consensusX = 0.0;
    double consensusY = 0.0;
    double consensusScore = 0.0;
    QStringList consensusSources;
};

struct QaCommunitySpellEvidence {
    int spellId = 0;
    QStringList sources;
    QStringList flags;
    QStringList contexts; // исходные dungeon/raid module filenames
    double score = 0.0;
};

struct QaCommunityReference {
    QString build;
    QStringList syncedSources;
    int mdtNpcPoints = 0;
    int mdtNpcEntries = 0;
    int mdtSpellIds = 0;
    int dbmSpellIds = 0;
    int littleWigsSpellIds = 0;
    int bigWigsSpellIds = 0;
    int attSpellIds = 0;
    int attObjectFiles = 0;
    int keystoneNpcPoints = 0;
    int commonSpellIds = 0;
    int commonNpcEntries = 0;
    QVector<QaCommunityPoint> points;
    QHash<int, QaCommunitySpellEvidence> spells;
    QString summary;
};

struct QaRetailCatalog {
    QaAuditScope scope;
    QString source;          // wowhead URL / cache / gazetteer
    QString zoneName;
    int wowheadZone = 0;
    int mapId = 0;           // world.creature.map
    int dungeonMap = 0;      // связанный инстанс (напр. Darkmaul)
    QString note;
    QVector<QaRetailId> npcs;
    QVector<QaRetailId> quests;
    QVector<QaRetailId> objects;
    QVector<QaRetailId> spells;
    QVector<QaRetailId> instances;
    bool fromWowhead = false;
    QString communityReferenceSummary;
    QHash<int, QString> communitySpellEvidence;
    QVector<QaCommunityPoint> communityPoints;
    QHash<int, double> communitySpellScores;
    int communityPointConsensusCount = 0;
};

class QaRetailSource final : public QObject {
    Q_OBJECT
public:
    explicit QaRetailSource(QObject *parent = nullptr);
    static QaRetailCatalog resolveTask(const QString &task);
    static QaMapBounds boundsFor(int wowheadZone, int mapId);
    static bool tomtomToWorld(const QaMapBounds &b, double tx, double ty, double *wx, double *wy);
    void fetchForTask(const QString &task);
signals:
    void ready(const QaRetailCatalog &catalog);
private:
    void httpGet(const QUrl &url, const QaRetailCatalog &seed);
    static void parseHtml(const QString &html, QaRetailCatalog *cat);
    QNetworkAccessManager m_network;
};
