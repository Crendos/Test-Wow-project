#pragma once
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

enum CoreFam : quint32 {
    CoreNone   = 0,
    CoreTC335  = 1u << 0,
    CoreTC434  = 1u << 1,
    CoreTC548  = 1u << 2,
    CoreTC735  = 1u << 3,
    CoreNord   = 1u << 4,
    CoreTC10   = 1u << 5,
    CoreTC12   = 1u << 6,
    CoreCypher = 1u << 7,
    CoreCustom = 1u << 8,
    CoreTC     = CoreTC335 | CoreTC434 | CoreTC548 | CoreTC735 | CoreTC10 | CoreTC12,
    CoreAuth   = CoreTC335 | CoreTC434 | CoreTC548 | CoreCypher,
    CoreBnet   = CoreTC735 | CoreNord | CoreTC10 | CoreTC12,
    CoreLegion = CoreTC735 | CoreNord,
    CoreShared = CoreTC | CoreNord | CoreCypher | CoreCustom,
    CoreAll    = 0xFFFFFFFFu
};

struct ConsoleCommand {
    QString group;
    QString title;
    QString command;
    QString help;
    bool world = true; // false = realm / bnet / auth
    quint32 cores = 0; // 0 = определить по группе/команде
};

class ServerLauncher final : public QObject {
    Q_OBJECT
public:
    explicit ServerLauncher(QObject *parent = nullptr);
    ~ServerLauncher() override;

    void setWorldPath(const QString &path);
    void setRealmPath(const QString &path);
    void setOwnConsole(bool own);

    bool worldRunning() const;
    bool realmRunning() const;

    void startWorld();
    void startRealm();
    void stopWorld();
    void stopRealm();
    void restartWorld();
    void restartRealm();

    void sendWorld(const QString &line);
    void sendRealm(const QString &line);

    void configureRa(const QString &host, quint16 port, const QString &user, const QString &password);
    void sendRa(const QString &line);

    static QVector<ConsoleCommand> catalog();
    static QVector<ConsoleCommand> catalogFor(const QString &profile);
    static quint32 coreMaskFromProfile(const QString &profile);
    static quint32 coresFor(const ConsoleCommand &cmd);
    // Пишет безопасные ключи в worldserver.conf рядом с exe. Копия .bak-studio. Ядро не патчится.
    static bool reduceMemoryLoad(const QString &worldExePath, QString *report);

signals:
    void logWorld(const QString &line);
    void logRealm(const QString &line);
    void logRa(const QString &line);
    void worldState(bool running);
    void realmState(bool running);

private:
    QProcess m_world;
    QProcess m_realm;
    QTcpSocket m_ra;
    QString m_worldPath, m_realmPath, m_raUser, m_raPassword;
    bool m_ownConsole = false;
    bool m_raAuthed = false;

    void startProcess(QProcess &proc, const QString &path, bool world);
    void stopProcess(QProcess &proc, bool world);
    void hook(QProcess &proc, bool world);
    void startLogTail(bool world);
    void stopLogTail(bool world);
    void pollLogTails();
    QStringList logCandidates(const QString &exePath, bool world) const;
    void emitLog(bool world, const QString &text);

    QTimer m_tailTimer;
    QHash<QString, qint64> m_tailOff;
    QSet<QString> m_worldLogFiles;
    QSet<QString> m_realmLogFiles;
    QStringList m_worldLogWatch;
    QStringList m_realmLogWatch;
};
