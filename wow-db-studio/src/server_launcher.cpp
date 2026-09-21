#include "server_launcher.h"
#include <QAbstractSocket>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

ServerLauncher::ServerLauncher(QObject *parent) : QObject(parent) {
    m_tailTimer.setInterval(400);
    connect(&m_tailTimer, &QTimer::timeout, this, &ServerLauncher::pollLogTails);
    hook(m_world, true);
    hook(m_realm, false);
    connect(&m_ra, &QTcpSocket::readyRead, this, [this] {
        const QString chunk = QString::fromUtf8(m_ra.readAll());
        emit logRa(chunk);
        const QString low = chunk.toLower();
        if (!m_raAuthed && (low.contains("username") || low.contains("user:"))) {
            m_ra.write((m_raUser + "\n").toUtf8());
        } else if (!m_raAuthed && (low.contains("password") || low.contains("pass:"))) {
            m_ra.write((m_raPassword + "\n").toUtf8());
            m_raAuthed = true;
        }
    });
    connect(&m_ra, &QTcpSocket::connected, this, [this] { emit logRa("RA: соединение установлено."); });
    connect(&m_ra, &QTcpSocket::disconnected, this, [this] { m_raAuthed = false; emit logRa("RA: отключено."); });
    connect(&m_ra, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit logRa("RA: " + m_ra.errorString());
    });
}

ServerLauncher::~ServerLauncher() {
    m_world.kill();
    m_realm.kill();
}

void ServerLauncher::setWorldPath(const QString &path) { m_worldPath = path; }
void ServerLauncher::setRealmPath(const QString &path) { m_realmPath = path; }
void ServerLauncher::setOwnConsole(bool own) { m_ownConsole = own; }
bool ServerLauncher::worldRunning() const { return m_world.state() != QProcess::NotRunning; }
bool ServerLauncher::realmRunning() const { return m_realm.state() != QProcess::NotRunning; }

void ServerLauncher::emitLog(bool world, const QString &text) {
    if (text.isEmpty()) return;
    if (world) emit logWorld(text); else emit logRealm(text);
}

void ServerLauncher::hook(QProcess &proc, bool world) {
    proc.setProcessChannelMode(QProcess::MergedChannels);
    auto decode = [](const QByteArray &raw) {
        QString u = QString::fromUtf8(raw);
        if (u.contains(QChar::ReplacementCharacter))
            u = QString::fromLocal8Bit(raw);
        u.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        u.remove(QLatin1Char('\r'));
        return u;
    };
    connect(&proc, &QProcess::readyRead, this, [this, &proc, world, decode] {
        emitLog(world, decode(proc.readAll()));
    });
    connect(&proc, &QProcess::started, this, [this, world] {
        const qint64 pid = world ? m_world.processId() : m_realm.processId();
        emitLog(world, QString::fromUtf8(
            "Studio: процесс запущен, PID %1. Ниже — stdout и хвост Server.log / Errors.log (на Windows ядро часто не пишет в pipe).")
                            .arg(pid));
        startLogTail(world);
        if (world) emit worldState(true); else emit realmState(true);
    });
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, world](int code, QProcess::ExitStatus) {
        pollLogTails();
        emitLog(world, QString::fromUtf8("Studio: процесс завершён, код %1.").arg(code));
        QTimer::singleShot(1500, this, [this, world] { pollLogTails(); stopLogTail(world); });
        if (world) emit worldState(false); else emit realmState(false);
    });
    connect(&proc, &QProcess::errorOccurred, this, [this, world](QProcess::ProcessError err) {
        if (err == QProcess::Timedout) return;
        const QString msg = world ? m_world.errorString() : m_realm.errorString();
        emitLog(world, QString::fromUtf8("Studio: ошибка запуска: %1").arg(msg));
    });
}

QStringList ServerLauncher::logCandidates(const QString &exePath, bool world) const {
    const QString dir = QFileInfo(exePath).absolutePath();
    QStringList names;
    if (world) {
        names << QStringLiteral("Server.log") << QStringLiteral("worldserver.log")
              << QStringLiteral("World.log") << QStringLiteral("Errors.log")
              << QStringLiteral("DBErrors.log") << QStringLiteral("SQLDriver.log");
    } else {
        names << QStringLiteral("bnetserver.log") << QStringLiteral("authserver.log")
              << QStringLiteral("Auth.log") << QStringLiteral("Server.log")
              << QStringLiteral("Errors.log");
    }
    QStringList out;
    const QStringList folders = {
        dir, dir + QStringLiteral("/logs"), dir + QStringLiteral("/Logs"),
        dir + QStringLiteral("/log"), dir + QStringLiteral("/Log")
    };
    for (const auto &folder : folders) {
        for (const auto &n : names)
            out << folder + QLatin1Char('/') + n;
    }
    return out;
}

void ServerLauncher::startLogTail(bool world) {
    const QString exe = world ? m_worldPath : m_realmPath;
    QStringList &watch = world ? m_worldLogWatch : m_realmLogWatch;
    QSet<QString> &owned = world ? m_worldLogFiles : m_realmLogFiles;
    watch = logCandidates(exe, world);
    owned.clear();
    for (const auto &p : watch) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (world) {
            if (m_realmLogFiles.contains(abs)) continue;
        } else {
            if (m_worldLogFiles.contains(abs)) continue;
        }
        QFileInfo fi(p);
        if (!fi.exists() || !fi.isFile()) continue;
        owned.insert(abs);
        qint64 sz = fi.size();
        qint64 start = sz > 16384 ? sz - 16384 : 0;
        m_tailOff[abs] = start;
        emitLog(world, QString::fromUtf8("Studio: читаю журнал %1").arg(QDir::toNativeSeparators(abs)));
    }
    if (owned.isEmpty()) {
        emitLog(world, QString::fromUtf8(
            "Studio: Server.log ещё нет рядом с exe (%1). Как только ядро создаст logs/Server.log — строки появятся здесь.")
                            .arg(QDir::toNativeSeparators(QFileInfo(exe).absolutePath())));
    }
    if (!m_tailTimer.isActive())
        m_tailTimer.start();
    pollLogTails();
}

void ServerLauncher::stopLogTail(bool world) {
    if (world) { m_worldLogFiles.clear(); m_worldLogWatch.clear(); }
    else { m_realmLogFiles.clear(); m_realmLogWatch.clear(); }
    if (m_worldLogWatch.isEmpty() && m_realmLogWatch.isEmpty()
        && m_world.state() == QProcess::NotRunning && m_realm.state() == QProcess::NotRunning)
        m_tailTimer.stop();
}

void ServerLauncher::pollLogTails() {
    auto pump = [this](const QStringList &watch, QSet<QString> *owned, bool world) {
        for (const auto &p : watch) {
            QFileInfo fi(p);
            if (!fi.exists() || !fi.isFile()) continue;
            const QString abs = fi.absoluteFilePath();
            if (!owned->contains(abs)) {
                if (!world && m_worldLogFiles.contains(abs)) continue;
                if (world && m_realmLogFiles.contains(abs)) continue;
                owned->insert(abs);
                m_tailOff[abs] = fi.size() > 8192 ? fi.size() - 8192 : 0;
                emitLog(world, QString::fromUtf8("Studio: появился журнал %1").arg(QDir::toNativeSeparators(abs)));
            }
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const qint64 sz = f.size();
            qint64 off = m_tailOff.value(abs, 0);
            if (sz < off) off = 0;
            if (sz == off) continue;
            if (!f.seek(off)) continue;
            const QByteArray raw = f.readAll();
            m_tailOff[abs] = f.pos();
            QString u = QString::fromUtf8(raw);
            if (u.contains(QChar::ReplacementCharacter))
                u = QString::fromLocal8Bit(raw);
            u.replace(QLatin1String("\r\n"), QLatin1String("\n"));
            u.remove(QLatin1Char('\r'));
            emitLog(world, u);
        }
    };
    pump(m_worldLogWatch, &m_worldLogFiles, true);
    pump(m_realmLogWatch, &m_realmLogFiles, false);
}

void ServerLauncher::startProcess(QProcess &proc, const QString &path, bool world) {
    if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) {
        emitLog(world, QString::fromUtf8("Укажите существующий .exe (worldserver / bnetserver / authserver)."));
        return;
    }
    if (proc.state() != QProcess::NotRunning) {
        emitLog(world, QString::fromUtf8("Уже запущено из Studio."));
        return;
    }
    const QString dir = QFileInfo(path).absolutePath();
    emitLog(world, QString::fromUtf8("Studio: запуск %1 (рабочая папка %2)")
                      .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(dir)));
    startLogTail(world);
    if (m_ownConsole) {
        const bool ok = QProcess::startDetached(path, {}, dir);
        emitLog(world, ok
            ? QString::fromUtf8("Запущено в отдельном окне. stdout в Studio нет — читаю Server.log. Команды Studio в это окно не попадут (нужен RA).")
            : QString::fromUtf8("Не удалось запустить отдельное окно."));
        if (ok) {
            if (world) emit worldState(true); else emit realmState(true);
        }
        return;
    }
    proc.setProgram(path);
    proc.setWorkingDirectory(dir);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
}

void ServerLauncher::stopProcess(QProcess &proc, bool world) {
    if (proc.state() == QProcess::NotRunning) return;
    if (world) proc.write("server shutdown 0\n");
    if (!proc.waitForFinished(6000)) {
        proc.terminate();
        if (!proc.waitForFinished(3000)) proc.kill();
    }
}

void ServerLauncher::startWorld() { startProcess(m_world, m_worldPath, true); }
void ServerLauncher::startRealm() { startProcess(m_realm, m_realmPath, false); }
void ServerLauncher::stopWorld() { stopProcess(m_world, true); }
void ServerLauncher::stopRealm() { stopProcess(m_realm, false); }
void ServerLauncher::restartWorld() {
    stopWorld();
    QTimer::singleShot(800, this, [this] { startWorld(); });
}
void ServerLauncher::restartRealm() {
    stopRealm();
    QTimer::singleShot(800, this, [this] { startRealm(); });
}

void ServerLauncher::sendWorld(const QString &line) {
    const QString cmd = line.trimmed();
    if (cmd.isEmpty()) return;
    if (m_world.state() != QProcess::Running) {
        emit logWorld("Ядро не запущено из Studio — отправьте команду через RA или запустите worldserver здесь.");
        return;
    }
    m_world.write((cmd + "\n").toUtf8());
    emit logWorld("> " + cmd);
}

void ServerLauncher::sendRealm(const QString &line) {
    const QString cmd = line.trimmed();
    if (cmd.isEmpty()) return;
    if (m_realm.state() != QProcess::Running) {
        emit logRealm("Реалм не запущен из Studio.");
        return;
    }
    m_realm.write((cmd + "\n").toUtf8());
    emit logRealm("> " + cmd);
}

void ServerLauncher::configureRa(const QString &host, quint16 port, const QString &user, const QString &password) {
    m_raUser = user;
    m_raPassword = password;
    m_raAuthed = false;
    if (m_ra.state() != QAbstractSocket::UnconnectedState) m_ra.disconnectFromHost();
    emit logRa(QString("RA: подключение к %1:%2 …").arg(host).arg(port));
    m_ra.connectToHost(host, port);
}

void ServerLauncher::sendRa(const QString &line) {
    const QString cmd = line.trimmed();
    if (cmd.isEmpty()) return;
    if (m_ra.state() != QAbstractSocket::ConnectedState) {
        emit logRa("RA не подключено. Включите Remote Access в worldserver.conf (Ra.Enable = 1).");
        return;
    }
    m_ra.write((cmd + "\n").toUtf8());
    emit logRa("> " + cmd);
}


bool ServerLauncher::reduceMemoryLoad(const QString &worldExePath, QString *report) {
    auto fail = [&](const QString &m) { if (report) *report = m; return false; };
    if (worldExePath.trimmed().isEmpty())
        return fail(QString::fromUtf8("Укажите путь к worldserver.exe на вкладке «Сервер»."));
    const QFileInfo exe(worldExePath);
    const QString dir = exe.isDir() ? exe.absoluteFilePath() : exe.absolutePath();
    QString conf;
    const QStringList cands = {
        dir + "/worldserver.conf",
        dir + "/etc/worldserver.conf",
        dir + "/../etc/worldserver.conf"
    };
    for (const auto &c : cands) {
        if (QFileInfo::exists(c)) { conf = QFileInfo(c).absoluteFilePath(); break; }
    }
    if (conf.isEmpty())
        return fail(QString::fromUtf8("worldserver.conf не найден рядом с exe (%1). Скопируйте .conf.dist в .conf.").arg(QDir::toNativeSeparators(dir)));

    QFile f(conf);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QString::fromUtf8("Не открыть %1: %2").arg(QDir::toNativeSeparators(conf), f.errorString()));
    QString body = QString::fromUtf8(f.readAll());
    f.close();

    const QString bak = conf + ".bak-studio";
    if (!QFile::exists(bak))
        QFile::copy(conf, bak);

    auto currentVal = [&](const QString &key) -> QString {
        QRegularExpression re(QStringLiteral("^\\s*%1\\s*=\\s*(.*)$").arg(QRegularExpression::escape(key)),
                              QRegularExpression::MultilineOption);
        const auto m = re.match(body);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };
    const QString oldPreload = currentVal(QStringLiteral("PreloadAllNonInstancedMapGrids"));
    const QString oldBase = currentVal(QStringLiteral("BaseMapLoadAllGrids"));
    const QString oldUnload = currentVal(QStringLiteral("GridUnload"));

    const struct Pair { const char *key; const char *val; } keys[] = {
        {"PreloadAllNonInstancedMapGrids", "0"},
        {"BaseMapLoadAllGrids", "0"},
        {"InstanceMapLoadAllGrids", "0"},
        {"BattlegroundMapLoadAllGrids", "0"},
        {"SetAllCreaturesWithWaypointMovementActive", "0"},
        {"GridUnload", "1"},
        {"GridCleanUpDelay", "60000"},
        {"DontCacheRandomMovementGeneratorPaths", "1"},
        {"DontCacheRandomMovementPaths", "1"},
        {"Visibility.Distance.Continents", "70"},
        {"Visibility.Distance.Instances", "50"},
        {"Visibility.Distance.BGArenas", "60"},
        {"ListenRange.Say", "20"},
        {"ListenRange.Yell", "80"},
        {"ListenRange.TextEmote", "20"},
        {"ChangeWeather", "0"},
        {"Metrics.Enable", "0"},
        {"SOAP.Enabled", "0"},
        {"LogDB.Enable", "0"},
        {"PacketLogFile", "\"\""},
        {"LoadAllGridsOnMaps", "\"\""},
    };

    QStringList changed, missing;
    for (const auto &k : keys) {
        QRegularExpression re(QStringLiteral("^(\\s*#?\\s*)%1\\s*=.*$")
                                  .arg(QRegularExpression::escape(QLatin1String(k.key))),
                              QRegularExpression::MultilineOption);
        const QString line = QLatin1String(k.key) + " = " + QLatin1String(k.val);
        if (re.match(body).hasMatch()) {
            body.replace(re, line);
            changed << line;
        } else {
            missing << line;
        }
    }
    if (!missing.isEmpty()) {
        body += QString::fromUtf8("\n\n###################################################################################################\n# Studio: цель ~4 ГБ RAM (добавлено %1). mmap/vmap не отключались. Ядро не патчилось.\n###################################################################################################\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        for (const auto &l : missing) body += l + "\n";
        changed += missing;
    }

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QString::fromUtf8("Не записать %1: %2").arg(QDir::toNativeSeparators(conf), f.errorString()));
    f.write(body.toUtf8());
    f.close();

    QString why;
    if (oldPreload == QLatin1String("1") || oldBase == QLatin1String("1"))
        why += QString::fromUtf8("У вас был включён preload всех гридов — это легко +3–5 ГБ. Выключено.\n");
    if (oldUnload == QLatin1String("0"))
        why += QString::fromUtf8("GridUnload был 0 (карты не выгружались). Включён: пустые гриды уходят через 60 с.\n");
    const QString native = QDir::toNativeSeparators(exe.absoluteFilePath());
    if (native.contains(QLatin1String("Debug"), Qt::CaseInsensitive)
        || native.contains(QLatin1String("RelWithDebInfo"), Qt::CaseInsensitive))
        why += QString::fromUtf8("worldserver.exe похож на Debug/RelWithDebInfo — такая сборка часто ест в 1.5–2 раза больше RAM, чем Release. Пересоберите ядро в Release.\n");

    if (report) {
        *report = QString::fromUtf8(
            "Цель: снизить worldserver с ~8 ГБ к ~4 ГБ (не ниже: Legion держит DB2/hotfixes и mmap).\n\n"
            "Файл: %1\nРезервная копия: %2\n\n"
            "%3"
            "Прописано:\n• %4\n\n"
            "Обязательно:\n"
            "1) Полный рестарт worldserver (не reload config — гриды уже в памяти).\n"
            "2) Сборка ядра Release, не Debug.\n"
            "3) Если MySQL на этой же машине — в my.ini: innodb_buffer_pool_size=1G (иначе MySQL отъедает 2–4 ГБ рядом с ядром).\n"
            "4) Не летите GM-кой по всем континентам сразу: каждый новый грид снова ест память, пока GridUnload не сработает.\n"
            "mmap/vmap не отключались — иначе зависание после входа в мир.")
                      .arg(QDir::toNativeSeparators(conf),
                           QDir::toNativeSeparators(bak),
                           why,
                           changed.join(QString::fromUtf8("\n• ")));
    }
    return true;
}

quint32 ServerLauncher::coreMaskFromProfile(const QString &profile) {
    const QString t = profile.toLower();
    if (t.contains(QLatin1String("nordrassil"))) return CoreNord;
    if (t.contains(QLatin1String("cypher"))) return CoreCypher;
    if (t.contains(QLatin1String("3.3.5"))) return CoreTC335;
    if (t.contains(QLatin1String("4.3.4"))) return CoreTC434;
    if (t.contains(QLatin1String("5.4.8"))) return CoreTC548;
    if (t.contains(QLatin1String("7.3.5"))) return CoreTC735;
    if (t.contains(QLatin1String("10.x")) || t.contains(QLatin1String("10."))) return CoreTC10;
    if (t.contains(QLatin1String("12.x")) || t.contains(QLatin1String("master"))) return CoreTC12;
    if (t.contains(QString::fromUtf8("другой")) || t.contains(QLatin1String("custom"))) return CoreCustom;
    return CoreCustom;
}

quint32 ServerLauncher::coresFor(const ConsoleCommand &cmd) {
    if (cmd.cores != 0) return cmd.cores;
    const QString g = cmd.group;
    const QString c = cmd.command.toLower();
    if (c.startsWith(QLatin1String("bnetaccount"))) return CoreBnet;
    if (c.startsWith(QLatin1String("account create"))) return CoreAuth;
    if (c.startsWith(QLatin1String("account set"))) return CoreShared;
    if (g.startsWith(QLatin1String("Nordrassil"))) return CoreNord;
    if (g.startsWith(QLatin1String("CypherCore")) || g.contains(QLatin1String("(C#)"))) return CoreCypher;
    if (g.contains(QLatin1String("WotLK")) || g.contains(QLatin1String("authserver 3.3.5")))
        return CoreAuth;
    if (g.contains(QLatin1String("bnetserver")))
        return CoreBnet;
    if (g.contains(QLatin1String("hotfixes")) || c.contains(QLatin1String("hotfix")))
        return CoreLegion | CoreTC10 | CoreTC12;
    return CoreShared;
}

QVector<ConsoleCommand> ServerLauncher::catalogFor(const QString &profile) {
    const quint32 mask = coreMaskFromProfile(profile);
    QVector<ConsoleCommand> out;
    for (const auto &c : catalog()) {
        if (coresFor(c) & mask) out.append(c);
    }
    return out;
}

QVector<ConsoleCommand> ServerLauncher::catalog() {
    // Консоль процесса — без точки. В игре те же команды с ведущей «.».
    // Ядро = worldserver, реалм = bnetserver (Legion+) или authserver (WotLK).
    return {
        {"Ядро — сведения", "Информация о сервере", "server info",
         "Онлайн, аптайм, ревизия. Безопасно. Если ядро зависло — этой команды уже не будет в логе.", true},
        {"Ядро — сведения", "Статистика", "server stats",
         "Сессии, карты, нагрузка. Смотрите, не растут ли Map/SQL очереди после входа в мир.", true},
        {"Ядро — сведения", "MOTD", "server motd",
         "Текущее сообщение дня.", true},
        {"Ядро — сведения", "Лимит игроков", "server plimit",
         "Текущий player limit.", true},
        {"Ядро — сведения", "Debug сервера", "server debug",
         "Доп. отладка ядра (если команда есть в вашей сборке).", true},

        {"Ядро — стоп / рестарт", "Сохранить всех", "saveall",
         "Записать персонажей в characters. Всегда перед shutdown/restart.", true},
        {"Ядро — стоп / рестарт", "Выключить сразу", "server shutdown 0",
         "Остановка worldserver без таймера. Сначала saveall. Если процесс уже завис — кнопка «Стоп ядро» / Task Manager.", true},
        {"Ядро — стоп / рестарт", "Выключить через 60 с", "server shutdown 60",
         "Таймер 60 секунд, игроки видят отсчёт.", true},
        {"Ядро — стоп / рестарт", "Выключить через 300 с", "server shutdown 300",
         "5 минут на выход персонажей.", true},
        {"Ядро — стоп / рестарт", "Отмена shutdown", "server shutdown cancel",
         "Отменить запланированное выключение, если ядро ещё отвечает.", true},
        {"Ядро — стоп / рестарт", "Рестарт через 30 с", "server restart 30",
         "Рестарт worldserver. На кастомных сборках процесс может не подняться сам — тогда кнопка «Запустить ядро».", true},
        {"Ядро — стоп / рестарт", "Idle shutdown 300", "server idle shutdown 300",
         "Выключить, когда не останется игроков, через 300 с.", true},
        {"Ядро — стоп / рестарт", "Закрыть вход", "server set closed on",
         "Новые подключения не принимаются, текущие остаются.", true},
        {"Ядро — стоп / рестарт", "Открыть вход", "server set closed off",
         "Снова принимать игроков.", true},
        {"Ядро — стоп / рестарт", "Задать MOTD", "server set motd Рестарт через 5 минут.",
         "Замените текст. Показывается при входе.", true},

        {"Ядро — чат", "Анонс", "announce Сервер будет перезапущен.",
         "Сообщение всем в чат. Замените текст.", true},
        {"Ядро — чат", "Уведомление", "notify Рестарт через 2 минуты.",
         "Всплывающее уведомление клиенту.", true},
        {"Ядро — чат", "GM-анонс", "gmannounce Только для GM: проверка.",
         "Видно аккаунтам с GM.", true},
        {"Ядро — чат", "Именной анонс", "nameannounce Сообщение от консоли.",
         "Анонс с именем отправителя.", true},

        {"Ядро — аккаунты (Battle.net)", "Создать Battle.net аккаунт", "bnetaccount create email@local password",
         "Legion+: логин в клиенте — email. Замените email и пароль. Выполняется в КОНСОЛИ ЯДРА, не в bnetserver.", true},
        {"Ядро — аккаунты (Battle.net)", "Создать game-аккаунт", "bnetaccount gameaccountcreate email@local",
         "Доп. WoW-аккаунт на уже существующий Battle.net email.", true},
        {"Ядро — аккаунты (Battle.net)", "Пароль Battle.net", "bnetaccount set password email@local oldpass newpass newpass",
         "Смена пароля Battle.net. Подставьте свои значения.", true},
        {"Ядро — аккаунты (Battle.net)", "GM-уровень", "account set gmlevel 1 3 -1",
         "account set gmlevel $id_или_имя $уровень $realm. 3 = полный GM, -1 = все реалмы. На 7.3.5 часто нужен числовой id game-аккаунта.", true},
        {"Ядро — аккаунты (WotLK auth)", "account create (3.3.5)", "account create NAME PASSWORD",
         "Только ветки с authserver (3.3.5a), не Legion. На 7.3.5 используйте bnetaccount create.", true},

        {"Ядро — персонажи", "Кик", "kick Имя Причина",
         "Отключить игрока. Замените имя.", true},
        {"Ядро — персонажи", "Уровень", "character level Имя 110",
         "Установить уровень. Legion max = 110.", true},
        {"Ядро — персонажи", "Переименовать", "character rename Имя",
         "Персонаж сменит имя при следующем входе.", true},
        {"Ядро — персонажи", "Кастомизация", "character customize Имя",
         "Внешность на следующем входе.", true},
        {"Ядро — персонажи", "Смена фракции", "character changefaction Имя",
         "Фракция на следующем входе.", true},
        {"Ядро — персонажи", "Смена расы", "character changerace Имя",
         "Раса на следующем входе.", true},
        {"Ядро — персонажи", "Удалённые: список", "character deleted list",
         "Персонажи, помеченные на удаление.", true},
        {"Ядро — персонажи", "Восстановить удалённого", "character deleted restore ID",
         "ID из character deleted list.", true},
        {"Ядро — персонажи", "Дамп персонажа", "pdump write Имя file.dump",
         "Экспорт персонажа в файл (если включено в конфиге).", true},
        {"Ядро — персонажи", "Загрузить дамп", "pdump load file.dump Аккаунт",
         "Импорт персонажа на аккаунт.", true},
        {"Ядро — персонажи", "Оживить", "revive",
         "Нужна цель / выбранный игрок (через RA с контекстом или в игре).", true},
        {"Ядро — персонажи", "Сброс КД", "cooldown",
         "Сбросить кулдауны цели.", true},
        {"Ядро — персонажи", "Сброс талантов", "reset talents",
         "Сброс талантов цели.", true},
        {"Ядро — персонажи", "Сброс спеллов", "reset spells",
         "Осторожно: снимает выученные заклинания.", true},
        {"Ядро — персонажи", "Max skill", "maxskill",
         "Все скиллы цели на максимум уровня.", true},
        {"Ядро — персонажи", "Выучить спелл", "learn ID",
         "Выучить заклинание по SpellID. Сначала lookup spell.", true},
        {"Ядро — персонажи", "Забыть спелл", "unlearn ID",
         "Снять заклинание.", true},
        {"Ядро — персонажи", "Выдать предмет", "additem ID 1",
         "additem $itemId $count. ID из world/hotfixes. На 7.3.5 имя в клиенте — hotfixes.", true},
        {"Ядро — персонажи", "Выдать набор", "additemset ID",
         "Комплект по itemset id.", true},
        {"Ядро — персонажи", "Модифицировать деньги", "modify money 10000000",
         "В медных. 10000000 = 1000 золота.", true},
        {"Ядро — персонажи", "Модифицировать скорость", "modify speed 2",
         "Множитель скорости цели.", true},
        {"Ядро — персонажи", "Модифицировать HP", "modify hp 500000",
         "Текущее/макс. здоровье цели.", true},
        {"Ядро — персонажи", "Модифицировать уровень", "modify level 110",
         "Уровень выбранной цели.", true},
        {"Ядро — персонажи", "Модифицировать скейл", "modify scale 1",
         "Размер модели.", true},
        {"Ядро — персонажи", "Модифицировать фазу", "modify phase 1",
         "PhaseID цели.", true},
        {"Ядро — персонажи", "Aura", "aura ID",
         "Навесить ауру SpellID.", true},
        {"Ядро — персонажи", "Снять ауры", "unaura all",
         "unaura all или unaura SpellID.", true},
        {"Ядро — персонажи", "Каст", "cast ID",
         "Прокастить спелл от своего персонажа (в игре).", true},

        {"Ядро — поиск", "lookup player account", "lookup player account NAME",
         "Найти персонажей аккаунта.", true},
        {"Ядро — поиск", "lookup player ip", "lookup player ip 127.0.0.1",
         "Кто заходил с IP.", true},
        {"Ядро — поиск", "lookup creature", "lookup creature NAME",
         "creature_template по имени.", true},
        {"Ядро — поиск", "lookup item", "lookup item NAME",
         "Предмет по имени.", true},
        {"Ядро — поиск", "lookup spell", "lookup spell NAME",
         "SpellID по имени.", true},
        {"Ядро — поиск", "lookup quest", "lookup quest NAME",
         "Квест по имени.", true},
        {"Ядро — поиск", "lookup object", "lookup object NAME",
         "gameobject_template.", true},
        {"Ядро — поиск", "lookup tele", "lookup tele NAME",
         "Точки телепорта (.tele).", true},
        {"Ядро — поиск", "lookup map", "lookup map NAME",
         "Карта / instance map.", true},
        {"Ядро — поиск", "lookup area", "lookup area NAME",
         "Зона.", true},
        {"Ядро — поиск", "lookup skill", "lookup skill NAME",
         "SkillID.", true},
        {"Ядро — поиск", "lookup title", "lookup title NAME",
         "Титул.", true},
        {"Ядро — поиск", "lookup faction", "lookup faction NAME",
         "Фракция репутации.", true},
        {"Ядро — поиск", "lookup event", "lookup event NAME",
         "game_event.", true},

        {"Ядро — телепорт / GM", "GPS", "gps",
         "Координаты выбранного объекта/игрока.", true},
        {"Ядро — телепорт / GM", "go xyz", "go xyz X Y Z MAP",
         "Телепорт по координатам. Подставьте числа.", true},
        {"Ядро — телепорт / GM", "go creature", "go creature GUID",
         "К spawn GUID существа.", true},
        {"Ядро — телепорт / GM", "go object", "go object GUID",
         "К spawn GUID объекта.", true},
        {"Ядро — телепорт / GM", "appear", "appear Имя",
         "К игроку.", true},
        {"Ядро — телепорт / GM", "summon", "summon Имя",
         "Призвать игрока к себе.", true},
        {"Ядро — телепорт / GM", "tele", "tele NAME",
         "К точке из game_tele. Сначала lookup tele.", true},
        {"Ядро — телепорт / GM", "tele add", "tele add NAME",
         "Сохранить текущую позицию как точку телепорта.", true},
        {"Ядро — телепорт / GM", "GM вкл", "gm on",
         "Режим GM.", true},
        {"Ядро — телепорт / GM", "GM выкл", "gm off",
         "Выключить GM.", true},
        {"Ядро — телепорт / GM", "Невидимость", "gm visible off",
         "gm visible on/off.", true},
        {"Ядро — телепорт / GM", "Полёт", "gm fly on",
         "gm fly on/off.", true},
        {"Ядро — телепорт / GM", "GM-чат", "gm chat on",
         "Чат GM.", true},
        {"Ядро — телепорт / GM", "Список GM", "gm list",
         "GM-аккаунты.", true},
        {"Ядро — телепорт / GM", "GM в игре", "gm ingame",
         "Кто из GM онлайн.", true},

        {"Ядро — NPC", "npc info", "npc info",
         "Инфо выбранного существа (в игре).", true},
        {"Ядро — NPC", "npc near", "npc near",
         "Ближайшие spawn.", true},
        {"Ядро — NPC", "npc add", "npc add ENTRY",
         "Заспавнить и записать в creature. Замените ENTRY.", true},
        {"Ядро — NPC", "npc add temp", "npc add temp ENTRY",
         "Временный спавн, без записи в БД.", true},
        {"Ядро — NPC", "npc delete", "npc delete",
         "Удалить выбранный spawn из мира и БД.", true},
        {"Ядро — NPC", "npc move", "npc move",
         "Перенести выбранный spawn на вашу позицию.", true},
        {"Ядро — NPC", "npc set factionid", "npc set factionid ID",
         "Фракция шаблона/спавна.", true},
        {"Ядро — NPC", "npc set level", "npc set level 110",
         "Уровень выбранного NPC.", true},
        {"Ядро — NPC", "npc set spawntime", "npc set spawntime 300",
         "Респаун в секундах.", true},
        {"Ядро — NPC", "npc set spawndist", "npc set spawndist 5",
         "Радиус брожения.", true},
        {"Ядро — NPC", "npc set movetype", "npc set movetype idle",
         "idle / random / waypoint.", true},
        {"Ядро — NPC", "npc follow", "npc follow",
         "NPC следует за вами.", true},
        {"Ядро — NPC", "npc follow stop", "npc follow stop",
         "Остановить следование.", true},
        {"Ядро — NPC", "npc say", "npc say Текст",
         "Выбранный NPC говорит.", true},

        {"Ядро — объекты", "gobject info", "gobject info",
         "Инфо выбранного GO.", true},
        {"Ядро — объекты", "gobject near", "gobject near",
         "Ближайшие объекты.", true},
        {"Ядро — объекты", "gobject add", "gobject add ENTRY",
         "Поставить GO и записать в БД.", true},
        {"Ядро — объекты", "gobject add temp", "gobject add temp ENTRY",
         "Временный GO.", true},
        {"Ядро — объекты", "gobject delete", "gobject delete GUID",
         "Удалить GO.", true},
        {"Ядро — объекты", "gobject move", "gobject move GUID",
         "Перенести GO.", true},
        {"Ядро — объекты", "gobject turn", "gobject turn GUID",
         "Повернуть GO.", true},
        {"Ядро — объекты", "gobject activate", "gobject activate GUID",
         "Активировать (дверь/рычаг).", true},

        {"Ядро — баны / муты", "ban account", "ban account NAME 1d Причина",
         "ban account $имя $срок $причина. Срок: 1h 1d 1m 1y / -1 навсегда.", true},
        {"Ядро — баны / муты", "ban character", "ban character Имя 1d Причина",
         "Бан персонажа.", true},
        {"Ядро — баны / муты", "ban ip", "ban ip 1.2.3.4 1d Причина",
         "Бан IP.", true},
        {"Ядро — баны / муты", "unban account", "unban account NAME",
         "Снять бан аккаунта.", true},
        {"Ядро — баны / муты", "unban character", "unban character Имя",
         "Снять бан персонажа.", true},
        {"Ядро — баны / муты", "unban ip", "unban ip 1.2.3.4",
         "Снять бан IP.", true},
        {"Ядро — баны / муты", "baninfo account", "baninfo account NAME",
         "Информация о бане.", true},
        {"Ядро — баны / муты", "banlist account", "banlist account",
         "Список банов аккаунтов.", true},
        {"Ядро — баны / муты", "mute", "mute Имя 30 Причина",
         "Мут чата на 30 минут.", true},
        {"Ядро — баны / муты", "unmute", "unmute Имя",
         "Снять мут.", true},

        {"Ядро — тикеты", "ticket list", "ticket list",
         "Открытые тикеты.", true},
        {"Ядро — тикеты", "ticket onlinelist", "ticket onlinelist",
         "Тикеты онлайн-игроков.", true},
        {"Ядро — тикеты", "ticket viewid", "ticket viewid ID",
         "Просмотр тикета.", true},
        {"Ядро — тикеты", "ticket close", "ticket close ID",
         "Закрыть тикет.", true},
        {"Ядро — тикеты", "ticket comment", "ticket comment ID текст",
         "Комментарий GM.", true},
        {"Ядро — тикеты", "ticket assign", "ticket assign ID NAME",
         "Назначить GM.", true},
        {"Ядро — тикеты", "ticket delete", "ticket delete ID",
         "Удалить тикет.", true},

        {"Ядро — гильдии / почта", "guild info", "guild info",
         "Инфо гильдии цели.", true},
        {"Ядро — гильдии / почта", "guild create", "guild create Лидер Название",
         "Создать гильдию.", true},
        {"Ядро — гильдии / почта", "guild delete", "guild delete Название",
         "Удалить гильдию.", true},
        {"Ядро — гильдии / почта", "guild invite", "guild invite Имя Название",
         "Пригласить в гильдию.", true},
        {"Ядро — гильдии / почта", "send mail", "send mail Имя Тема Текст",
         "Письмо без вложений.", true},
        {"Ядро — гильдии / почта", "send items", "send items Имя Тема Текст ID:count",
         "Письмо с предметом.", true},
        {"Ядро — гильдии / почта", "send money", "send money Имя Тема Текст 10000",
         "Письмо с деньгами (медь).", true},
        {"Ядро — гильдии / почта", "send message", "send message Имя Текст",
         "Системное сообщение игроку.", true},

        {"Ядро — инстансы / события", "instance listbinds", "instance listbinds",
         "Сохранения инстансов цели.", true},
        {"Ядро — инстансы / события", "instance unbind all", "instance unbind all",
         "Снять все сохранения. Осторожно на живом персонаже.", true},
        {"Ядро — инстансы / события", "instance stats", "instance stats",
         "Сколько инстансов загружено — полезно при зависании после входа в подземелье.", true},
        {"Ядро — инстансы / события", "instance savedata", "instance savedata",
         "Принудительно сохранить состояние инстанса.", true},
        {"Ядро — инстансы / события", "event activelist", "event activelist",
         "Активные game_event.", true},
        {"Ядро — инстансы / события", "event start", "event start ID",
         "Запустить событие.", true},
        {"Ядро — инстансы / события", "event stop", "event stop ID",
         "Остановить событие.", true},
        {"Ядро — инстансы / события", "event info", "event info ID",
         "Инфо события.", true},

        {"Ядро — перезагрузка SQL", "reload config", "reload config",
         "Перечитать worldserver.conf без рестарта (не все ключи).", true},
        {"Ядро — перезагрузка SQL", "reload creature_template", "reload creature_template",
         "NPC-шаблоны world.", true},
        {"Ядро — перезагрузка SQL", "reload creature_template_addon", "reload creature_template_addon",
         "Аддоны шаблона NPC.", true},
        {"Ядро — перезагрузка SQL", "reload creature_loot_template", "reload creature_loot_template",
         "Лут с существ.", true},
        {"Ядро — перезагрузка SQL", "reload creature_equip_template", "reload creature_equip_template",
         "Экипировка NPC.", true},
        {"Ядро — перезагрузка SQL", "reload creature_text", "reload creature_text",
         "Тексты NPC.", true},
        {"Ядро — перезагрузка SQL", "reload smart_scripts", "reload smart_scripts",
         "SmartAI.", true},
        {"Ядро — перезагрузка SQL", "reload quest_template", "reload quest_template",
         "Квесты.", true},
        {"Ядро — перезагрузка SQL", "reload item_template", "reload item_template",
         "Серверная логика предметов (world). Имя в клиенте 7.3.5 — hotfixes.", true},
        {"Ядро — перезагрузка SQL", "reload gameobject_template", "reload gameobject_template",
         "Шаблоны GO.", true},
        {"Ядро — перезагрузка SQL", "reload gameobject_loot_template", "reload gameobject_loot_template",
         "Лут объектов.", true},
        {"Ядро — перезагрузка SQL", "reload npc_vendor", "reload npc_vendor",
         "Вендоры.", true},
        {"Ядро — перезагрузка SQL", "reload npc_trainer", "reload npc_trainer",
         "Тренеры.", true},
        {"Ядро — перезагрузка SQL", "reload gossip_menu", "reload gossip_menu",
         "Gossip.", true},
        {"Ядро — перезагрузка SQL", "reload gossip_menu_option", "reload gossip_menu_option",
         "Пункты gossip.", true},
        {"Ядро — перезагрузка SQL", "reload conditions", "reload conditions",
         "conditions.", true},
        {"Ядро — перезагрузка SQL", "reload disables", "reload disables",
         "disables (квесты/спеллы/карты).", true},
        {"Ядро — перезагрузка SQL", "reload spell_script_names", "reload spell_script_names",
         "Привязка C++ спелл-скриптов.", true},
        {"Ядро — перезагрузка SQL", "reload spell_linked_spell", "reload spell_linked_spell",
         "Связанные спеллы.", true},
        {"Ядро — перезагрузка SQL", "reload waypoint_data", "reload waypoint_data",
         "Вейпоинты.", true},
        {"Ядро — перезагрузка SQL", "reload command", "reload command",
         "Таблица command (права на команды).", true},
        {"Ядро — перезагрузка SQL", "reload trinity_string", "reload trinity_string",
         "Строки ядра.", true},
        {"Ядро — перезагрузка SQL", "reload all_npc", "reload all_npc",
         "Пачка NPC-таблиц.", true},
        {"Ядро — перезагрузка SQL", "reload all_quest", "reload all_quest",
         "Пачка квестов.", true},
        {"Ядро — перезагрузка SQL", "reload all_item", "reload all_item",
         "Пачка предметов world.", true},
        {"Ядро — перезагрузка SQL", "reload all_loot", "reload all_loot",
         "Все loot-таблицы.", true},
        {"Ядро — перезагрузка SQL", "reload all_scripts", "reload all_scripts",
         "Скрипты SQL/SmartAI.", true},
        {"Ядро — перезагрузка SQL", "reload all_spell", "reload all_spell",
         "Spell-таблицы.", true},
        {"Ядро — перезагрузка SQL", "reload all", "reload all",
         "Тяжёлая перезагрузка. На живом реалме может подлагировать.", true},

        {"Ядро — hotfixes", "reload hotfix_data", "reload hotfix_data",
         "Клиентские DB2 в базе hotfixes. После Wago. Если «unknown command» — нужен полный рестарт worldserver.", true},
        {"Ядро — hotfixes", "reload hotfixes", "reload hotfixes",
         "Альтернативное имя на части сборок. Если нет — рестарт ядра.", true},

        {"Ядро — отладка зависаний", "mmap stats", "mmap stats",
         "Навигационные меши. Если mmap не загружены — мобы/вход в мир могут подвесить поток карты.", true},
        {"Ядро — отладка зависаний", "debug combat", "debug combat",
         "Отладка боя выбранной цели (в игре).", true},
        {"Ядро — отладка зависаний", "debug threat", "debug threat",
         "Таблица угрозы цели.", true},
        {"Ядро — отладка зависаний", "debug lfg", "debug lfg",
         "LFG очередь.", true},
        {"Ядро — отладка зависаний", "debug los", "debug los",
         "Проверка линии видимости.", true},
        {"Ядро — отладка зависаний", "wpgps", "wpgps",
         "Координата вейпоинта в лог (для SQL).", true},

        {"Реалм (bnetserver)", "Выход quit", "quit",
         "Остановить bnetserver. На Legion аккаунты создаются в КОНСОЛИ ЯДРА (bnetaccount), не здесь.", false},
        {"Реалм (bnetserver)", "Выход exit", "exit",
         "Синоним quit на части сборок.", false},
        {"Реалм (bnetserver)", "server info", "server info",
         "Если сборка слушает CLI. Иначе смотрите лог bnetserver: listening, realmlist, ошибки MySQL/порта 1119.", false},
        {"Реалм (authserver 3.3.5)", "quit (WotLK)", "quit",
         "Остановка authserver на 3.3.5a. Порт 3724. Аккаунты: account create в worldserver.", false},
    };
}
