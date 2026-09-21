#include "main_window.h"
#include "client_patch_service.h"
#include "core_detect_service.h"
#include "studio_file_log.h"
#include <QDateTime>
#include <QComboBox>
#include <QCompleter>
#include <QDockWidget>
#include <QTime>
#include <QSqlError>
#include <QSqlField>
#include <QSqlRecord>
#include <QSqlDriver>
#include <QMetaType>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QSpinBox>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QPalette>
#include <QColor>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QCheckBox>
#include <QSettings>
#include <QDesktopServices>
#include <QHash>
#include <QSet>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QGridLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QTextCursor>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>
#include <QScreen>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

static QPushButton *button(const QString &text) { auto *b=new QPushButton(text); b->setMinimumHeight(30); return b; }

static void equalWidthButtons(const QList<QPushButton *> &btns) {
    int w = 0;
    for (auto *b : btns) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        w = qMax(w, b->sizeHint().width());
    }
    for (auto *b : btns) b->setFixedWidth(w);
}

static bool safeIdent(const QString &s) {
    if (s.isEmpty()) return false;
    for (QChar c : s) if (!c.isLetterOrNumber() && c != '_') return false;
    return true;
}

static QString normalizeName(QString s) {
    s = s.toLower();
    s.remove('_');
    s.remove('-');
    if (s.endsWith("lang")) s.chop(4);
    if (s.endsWith("enus")) s.chop(4);
    return s;
}

// Wago DB2 names are PascalCase (ItemSparse); TrinityCore hotfixes uses snake_case (item_sparse).
static QString wagoToHotfixName(const QString &wagoName) {
    QString out;
    for (int i = 0; i < wagoName.size(); ++i) {
        const QChar c = wagoName.at(i);
        if (i > 0 && c.isUpper() && wagoName.at(i - 1).isLetterOrNumber())
            out += QLatin1Char('_');
        out += c.toLower();
    }
    return out;
}

static QString suggestLocalTable(const QString &wagoName, const QStringList &local = {}) {
    const QString snake = wagoToHotfixName(wagoName);
    for (const auto &t : local)
        if (t.compare(snake, Qt::CaseInsensitive) == 0) return t;
    for (const auto &t : local)
        if (normalizeName(t) == normalizeName(wagoName)) return t;
    return snake;
}

static QString suggestLocalCol(const QString &wago, const QStringList &local) {
    const QString n = normalizeName(wago);
    for (const auto &c : local) if (normalizeName(c) == n) return c;
    const QStringList aliases = (n == "id") ? QStringList{"entry", "id", "guid", "ID"}
                            : (n == "name" || n == "display") ? QStringList{"name", "Name"}
                            : QStringList{};
    for (const auto &a : aliases)
        for (const auto &c : local) if (c.compare(a, Qt::CaseInsensitive) == 0) return c;
    return {};
}

// Wraps a page in a scroll area so content always stays reachable at any window size.
static QScrollArea *scrollWrap(QWidget *page) {
    auto *sa=new QScrollArea; sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(page); return sa;
}

// Label that wraps long text instead of pushing the window wider.
static QLabel *wrapLabel(const QString &text) { auto *l=new QLabel(text); l->setWordWrap(true); return l; }

static int askScrollDialog(QWidget *parent, const QString &title, const QString &headline,
                           const QString &details, const QStringList &buttons) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    QScreen *scr = (parent && parent->screen()) ? parent->screen() : QGuiApplication::primaryScreen();
    const QRect ag = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 720);
    dlg.resize(qBound(420, ag.width() * 3 / 5, 720), qBound(280, ag.height() * 2 / 5, 520));
    dlg.setMaximumSize(qMax(400, ag.width() - 48), qMax(280, ag.height() - 48));
    auto *vl = new QVBoxLayout(&dlg);
    auto *head = new QLabel(headline);
    head->setWordWrap(true);
    vl->addWidget(head);
    if (!details.trimmed().isEmpty()) {
        auto *te = new QTextEdit;
        te->setReadOnly(true);
        te->setPlainText(details);
        te->setMinimumHeight(100);
        vl->addWidget(te, 1);
    }
    auto *bb = new QDialogButtonBox;
    QList<QAbstractButton *> created;
    for (int i = 0; i < buttons.size(); ++i) {
        const auto role = (i == buttons.size() - 1) ? QDialogButtonBox::RejectRole
                         : (i == 0) ? QDialogButtonBox::AcceptRole
                                    : QDialogButtonBox::ActionRole;
        created << bb->addButton(buttons.at(i), role);
    }
    vl->addWidget(bb);
    int chosen = -1;
    for (int i = 0; i < created.size(); ++i) {
        QObject::connect(created.at(i), &QAbstractButton::clicked, &dlg, [&dlg, &chosen, i] {
            chosen = i;
            dlg.done(QDialog::Accepted);
        });
    }
    if (dlg.exec() != QDialog::Accepted)
        return -1;
    return chosen;
}

// Потребление памяти процессом (КБ): Windows — K32GetProcessMemoryInfo, Linux — VmRSS.
static qint64 processMemoryKb() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return qint64(pmc.WorkingSetSize) / 1024;
    return -1;
#else
    QFile f(QStringLiteral("/proc/self/status"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.startsWith("VmRSS:")) {
                const QList<QByteArray> parts = line.split(' ');
                for (int i = 1; i < parts.size(); ++i) { bool ok=false; const qlonglong v=parts[i].toLongLong(&ok); if (ok) return v; }
            }
        }
    }
    return -1;
#endif
}

// Ограничение роста логов: держим не больше maxBlocks строк, иначе память растёт бесконечно.
static void capLog(QTextEdit *te, int maxBlocks = 1500) {
    if (!te) return;
    auto *doc = te->document();
    if (doc->blockCount() <= maxBlocks) return;
    QTextCursor c(doc);
    int extra = doc->blockCount() - maxBlocks;
    c.movePosition(QTextCursor::Start);
    for (int i = 0; i < extra; ++i) c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
    c.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
    c.removeSelectedText();
}

// Дружелюбная классификация ошибки подключения к MySQL: неверные данные или нет соединения по адресу.
static QString friendlyDbError(const QString &raw, const DbProfile &p) {
    const QString t = raw.toLower();
    QString head;
    if (t.contains("access denied") || t.contains("1045"))
        head = QStringLiteral("Введены неверные данные доступа: проверьте логин и пароль.");
    else if (t.contains("unknown database") || t.contains("1049"))
        head = QStringLiteral("База «%1» не найдена на сервере — проверьте имя базы.").arg(p.database);
    else if (t.contains("driver not loaded") || t.contains("qmysql") || t.contains("qodbc"))
        head = QStringLiteral("Qt не загрузил драйвер MySQL (нужен qsqlmysql.dll или ODBC-драйвер) — подробности в технической детали.");
    else if (t.contains("can't connect") || t.contains("connection refused") || t.contains("10061") || t.contains("2003")
             || t.contains("timed out") || t.contains("timeout") || t.contains("unreachable") || t.contains("not respond"))
        head = QStringLiteral("Нет подключения по адресу %1:%2 — сервер MySQL не запущен, адрес/порт неверны или мешает файрвол.").arg(p.host).arg(p.port);
    else
        head = QStringLiteral("Не удалось подключиться к MySQL.");
    return head + QStringLiteral("\n\nТехническая деталь: %1").arg(raw);
}

static void makeComboSearchable(QComboBox *box) {
    if (!box) return;
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    auto *c = new QCompleter(box->model(), box);
    c->setCaseSensitivity(Qt::CaseInsensitive);
    c->setFilterMode(Qt::MatchContains);
    c->setCompletionMode(QCompleter::PopupCompletion);
    box->setCompleter(c);
}

static QString sqlIdent(const QString &s) {
    QString o;
    for (QChar c : s) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) o += c;
        else o += QLatin1Char('_');
    }
    if (o.isEmpty() || o.at(0).isDigit()) o.prepend(QLatin1Char('_'));
    return o;
}

static QString formatSqlError(const QSqlError &e, const QString &sql = {}, const QString &extra = {}) {
    QStringList parts;
    const QString code = e.nativeErrorCode();
    if (!code.isEmpty())
        parts << QStringLiteral("Код MySQL: %1").arg(code);
    const QString db = e.databaseText().trimmed();
    const QString dr = e.driverText().trimmed();
    const QString tx = e.text().trimmed();
    if (!db.isEmpty()) parts << db;
    if (!dr.isEmpty() && dr != db) parts << QStringLiteral("Драйвер: %1").arg(dr);
    if (!tx.isEmpty() && tx != db && !tx.contains(db) && tx != dr)
        parts << tx;
    switch (e.type()) {
    case QSqlError::ConnectionError: parts << QStringLiteral("Тип Qt: соединение."); break;
    case QSqlError::StatementError: parts << QStringLiteral("Тип Qt: SQL-запрос."); break;
    case QSqlError::TransactionError: parts << QStringLiteral("Тип Qt: транзакция."); break;
    case QSqlError::UnknownError: parts << QStringLiteral("Тип Qt: неизвестная ошибка."); break;
    case QSqlError::NoError:
    default:
        parts << QStringLiteral("Тип Qt: %1 (NoError=0 — драйвер часто молчит после rollback или повторного bind).").arg(int(e.type()));
        break;
    }
    if (!sql.isEmpty()) parts << QStringLiteral("SQL:\n%1").arg(sql.left(1200));
    if (!extra.trimmed().isEmpty()) parts << extra.trimmed();
    const bool noMysqlText = db.isEmpty() && dr.isEmpty() && tx.isEmpty();
    if (noMysqlText)
        parts << QStringLiteral("Текст от QMYSQL пустой. Ниже — SHOW ERRORS/WARNINGS, если MySQL их ещё держит. Иначе: повторный bind без очистки, нет прав INSERT, таблица без колонки, не тот libmysql.dll.");
    return parts.join(QLatin1Char('\n'));
}

static QString sqlLiteral(const QSqlDatabase &db, const QString &v) {
    if (db.driver()) {
        QSqlField f(QStringLiteral("v"), QMetaType(QMetaType::QString));
        f.setValue(v);
        return db.driver()->formatValue(f);
    }
    QString e = v;
    e.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    e.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    e.remove(QChar(0));
    return QLatin1Char('\'') + e + QLatin1Char('\'');
}

static bool sqlValueEqual(QString a, QString b) {
    a = a.trimmed();
    b = b.trimmed();
    if (a == b) return true;
    if (a.isEmpty() && (b == QLatin1String("0") || b == QLatin1String("NULL"))) return false;
    bool ok1 = false, ok2 = false;
    const qlonglong n1 = a.toLongLong(&ok1);
    const qlonglong n2 = b.toLongLong(&ok2);
    if (ok1 && ok2) return n1 == n2;
    const double d1 = a.toDouble(&ok1);
    const double d2 = b.toDouble(&ok2);
    if (ok1 && ok2) return qAbs(d1 - d2) < 1e-9;
    return false;
}

static QString mysqlSessionDiag(const QSqlDatabase &db) {
    if (!db.isValid() || !db.isOpen())
        return QStringLiteral("Диагностика: соединение MySQL закрыто или недействительно. driver=%1 open=%2")
            .arg(db.driverName()).arg(db.isOpen());
    QStringList out;
    out << QStringLiteral("Соединение: driver=%1 name=%2 db=%3")
               .arg(db.driverName(), db.connectionName(), db.databaseName());
    QSqlQuery q(db);
    auto dump = [&](const char *sql) {
        if (!q.exec(QLatin1String(sql))) {
            const QString e = q.lastError().text().trimmed();
            if (!e.isEmpty()) out << QStringLiteral("%1 не выполнен: %2").arg(QLatin1String(sql), e);
            return;
        }
        int n = 0;
        while (q.next() && n < 12) {
            QStringList cells;
            for (int c = 0; c < q.record().count(); ++c)
                cells << q.value(c).toString();
            out << QStringLiteral("%1: %2").arg(QLatin1String(sql), cells.join(QLatin1Char(' ')));
            ++n;
        }
        if (n == 0) out << QStringLiteral("%1: (пусто)").arg(QLatin1String(sql));
    };
    dump("SHOW ERRORS");
    dump("SHOW WARNINGS");
    return out.join(QLatin1Char('\n'));
}

// --- Scripting entity specifications (templates + optional SmartAI source type) ---
struct EntitySpec { const char *label; const char *table; const char *key; int smartSourceType; };
static const EntitySpec kEntities[] = {
    {"NPC / Существо",         "creature_template",    "entry", 0},
    {"Игровой объект",         "gameobject_template",  "entry", 1},
    {"Предмет",                "item_template",        "entry", -1},
    {"Квест",                  "quest_template",       "id",    -1},
    {"Подземелье / Рейд",      "instance_template",    "map",   -1},
};

// Preferred smart_scripts order. event_param5 есть на 6.x+/AzerothCore, на Trinity 3.3.5a его нет.
static const QStringList kScriptColsPreferred = {
    "entryorguid","source_type","id","link","event_type","event_phase_mask",
    "event_chance","event_flags","event_param1","event_param2","event_param3",
    "event_param4","event_param5","action_type","action_param1","action_param2",
    "action_param3","action_param4","action_param5","action_param6","target_type",
    "target_param1","target_param2","target_param3","target_x","target_y",
    "target_z","target_o","comment"
};

MainWindow::MainWindow() {
    QElapsedTimer startupTimer; startupTimer.start();
    setWindowTitle(QStringLiteral("WoW DB Studio")); setMinimumSize(900,600); resize(1150,760);
    auto *tabs=new QTabWidget; setCentralWidget(tabs);
    // Хвост справа от последней вкладки: заставляем виджеты сами рисовать светлый фон
    // (иначе сквозь прозрачность проступает нативный чёрный фон окна в тёмной теме Windows).
    setAutoFillBackground(true);
    tabs->setAutoFillBackground(true);
    tabs->tabBar()->setAutoFillBackground(true);
    tabs->tabBar()->setExpanding(true); // вкладки тянутся на всю ширину — хвоста нет

    // Connection
    auto *connection=new QWidget; auto *form=new QFormLayout(connection);
    host=new QLineEdit("127.0.0.1"); port=new QSpinBox; port->setRange(1,65535);port->setValue(3306);
    dbSelector=new QComboBox; dbSelector->setEditable(true); dbSelector->setCurrentText("world");
    user=new QLineEdit("root",connection);user->hide();password=new QLineEdit(connection);password->setEchoMode(QLineEdit::Password);password->hide();
    core=new QComboBox; core->addItems({"TrinityCore 3.3.5a", "TrinityCore 4.3.4", "TrinityCore 5.4.8", "TrinityCore 7.3.5", "Nordrassil Core 7.3.5", "TrinityCore 10.x", "TrinityCore 12.x (upstream master)", "TrinityCore master", "CypherCore (C#)", "Другой / кастомный"});
    coreRepository=new QLineEdit(connection);coreRepository->setReadOnly(true);coreRepository->hide();coreBranch=new QComboBox(connection);coreBranch->setEditable(true);coreBranch->hide();corePatch=new QLineEdit(connection);corePatch->setReadOnly(true);corePatch->hide();
    auto *loadBranches=button("Ветви GitHub"); auto *checkPatch=button("Версия / patch");
    connectButton=button("Подключиться");
    auto *detectNow=button("Определить ядро");
    auto *browseDetect=button("Указать папку");
    auto *connActions=new QWidget; auto *connGrid=new QGridLayout(connActions);
    connGrid->setContentsMargins(0,0,0,0); connGrid->setHorizontalSpacing(8); connGrid->setVerticalSpacing(6);
    connGrid->addWidget(connectButton, 0, 0);
    connGrid->addWidget(detectNow, 0, 1);
    connGrid->addWidget(loadBranches, 1, 0);
    connGrid->addWidget(checkPatch, 1, 1);
    connGrid->addWidget(browseDetect, 2, 0, 1, 2);
    detectFolder=new QLineEdit; detectFolder->setPlaceholderText("Необязательно: исходники ядра или папка клиента (Wow.exe). Для определения ядра хватает MySQL.");
    detectStatus=wrapLabel("После подключения программа сама читает version / realmlist и, если указана папка, README, .git и .build.info.");
    form->addRow("Адрес:",host);form->addRow("Порт:",port);form->addRow("База данных (все доступны):",dbSelector);form->addRow("Профиль ядра:",core);form->addRow("Папка ядра / клиента:",detectFolder);form->addRow("Действия:",connActions);form->addRow(detectStatus);
    auto *connNote=wrapLabel("Подключение к серверу MySQL даёт доступ ко всем базам (world, auth, characters и любым другим). Выберите активную базу в списке выше — после подключения он заполнится реальными именами (SHOW DATABASES). Пароли не записываются в файл.");
    connNote->setWordWrap(true); connNote->setMaximumWidth(560);
    form->addRow(connNote);
    tabs->addTab(scrollWrap(connection),"Подключение");

    // Database
    auto *dbPage=new QWidget; auto *dbLayout=new QHBoxLayout(dbPage); tables=new QListWidget; tables->setMaximumWidth(280);
    auto *right=new QVBoxLayout; sql=new QTextEdit("SELECT * FROM creature_template LIMIT 100;"); auto *run=button("Выполнить SQL");
    right->addWidget(wrapLabel("SQL-консоль. Изменяющие запросы выполняются только после подтверждения. Работает в активной базе (см. вкладку «Подключение»)."));right->addWidget(sql);right->addWidget(run); dbLayout->addWidget(tables);dbLayout->addLayout(right);tabs->addTab(dbPage,"База / таблицы");

    // DB2 / Wago: compare a patch table with the user's MySQL schema, then import only after mapping.
    auto *db2=new QWidget; auto *db2Layout=new QVBoxLayout(db2);
    auto *wagoBox=new QGroupBox("Wago DB2 — сопоставление с базой hotfixes (не world)");
    auto *wagoRoot=new QVBoxLayout(wagoBox);
    auto *wform=new QFormLayout;
    wagoBuild=new QComboBox; wagoBuild->setMinimumWidth(260);
    wagoHotfixDb=new QComboBox; wagoHotfixDb->setCurrentText("hotfixes");
    wagoTableName=new QComboBox; wagoTableName->setMinimumWidth(260);
    wagoLocalTable=new QComboBox; wagoLocalTable->setMinimumWidth(260);
    wagoLocale=new QComboBox; wagoLocale->setMinimumWidth(260);
    wagoLocale->addItems({"ruRU","enUS","enGB","deDE","esES","esMX","frFR","itIT","ptBR","koKR","zhCN","zhTW"});
    {
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        const QString savedLocale = s.value(QStringLiteral("wago/locale"), QStringLiteral("ruRU")).toString().trimmed();
        const int li = wagoLocale->findText(savedLocale, Qt::MatchFixedString);
        wagoLocale->setCurrentIndex(li >= 0 ? li : 0);
    }
    connect(wagoLocale, &QComboBox::currentTextChanged, this, [](const QString &value) {
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        s.setValue(QStringLiteral("wago/locale"), value.trimmed().isEmpty() ? QStringLiteral("ruRU") : value.trimmed());
    });
    makeComboSearchable(wagoBuild);
    makeComboSearchable(wagoTableName);
    makeComboSearchable(wagoLocalTable);
    makeComboSearchable(wagoLocale);
    wagoTableName->setMaxVisibleItems(25);
    wagoTableName->setMinimumHeight(32);
    wagoTableName->setPlaceholderText(QStringLiteral("Нажмите ▼ — список как на wago.tools (Achievement, Spell…)"));
    wagoHotfixDb->hide();
    wagoHotfixDb->setEnabled(false);
    wagoBuild->setPlaceholderText("12.1.0.69497");
    wagoHotfixInfo = wrapLabel(QStringLiteral("hotfixes — определяется при подключении к MySQL, сменить нельзя."));
    auto *loadWagoTables=button("Обновить список с wago.tools");
    auto *analyzeWago=button("Анализировать колонки");
    auto *createWagoTable=button("Создать таблицу в hotfixes");
    auto *importWago=button("Импортировать по сопоставлению");
    equalWidthButtons({loadWagoTables, analyzeWago, createWagoTable, importWago});
    auto *wagoBtnCol=new QWidget; auto *wagoBtnLay=new QVBoxLayout(wagoBtnCol);
    wagoBtnLay->setContentsMargins(0,0,0,0); wagoBtnLay->setSpacing(6);
    wagoBtnLay->addWidget(loadWagoTables, 0, Qt::AlignLeft);
    wagoBtnLay->addWidget(analyzeWago, 0, Qt::AlignLeft);
    wagoBtnLay->addWidget(createWagoTable, 0, Qt::AlignLeft);
    wagoBtnLay->addWidget(importWago, 0, Qt::AlignLeft);
    wform->addRow("Патч / build:", wagoBuild);
    wform->addRow("Локаль Wago:", wagoLocale);
    wform->addRow("База hotfixes:", wagoHotfixInfo);
    wform->addRow("Таблица Wago (DB2):", wagoTableName);
    wform->addRow("Таблица в hotfixes:", wagoLocalTable);
    wagoNameHint = wrapLabel(QStringLiteral("Выберите таблицу в списке «Таблица Wago (DB2)». После MySQL патч и имена таблиц подставятся сами. Анализ и импорт качают ВСЕ строки с wago.tools."));
    wform->addRow(QStringLiteral("Проверка имени:"), wagoNameHint);
    wform->addRow("Действия:", wagoBtnCol);
    wagoStatus=wrapLabel("Подключитесь к MySQL — патч из realmlist.gamebuild, база hotfixes и список таблиц wago.tools подставятся сами.");
    wform->addRow(wagoStatus);
    wagoRoot->addLayout(wform);
    wagoMapping=new QTableWidget(0,5);
    wagoMapping->setHorizontalHeaderLabels({"Колонка Wago","Пример","Ваша колонка","Тип у вас","Статус"});
    wagoMapping->horizontalHeader()->setStretchLastSection(true);
    wagoMapping->setMinimumHeight(180);
    db2Layout->addWidget(wagoBox, 1);
    db2Layout->addWidget(wrapLabel("Сопоставление колонок (измените вручную, если имена не совпали):"));
    db2Layout->addWidget(wagoMapping, 1);

    auto *jsonBox=new QGroupBox("Запасной вариант: JSON export по URL");
    auto *dform=new QFormLayout(jsonBox); db2Url=new QLineEdit("https://"); db2Table=new QLineEdit;
    auto *import=button("Скачать и импортировать JSON");
    dform->addRow("URL JSON export:",db2Url); dform->addRow("Целевая MySQL-таблица:",db2Table); dform->addRow(import);
    dform->addRow(wrapLabel("Публичный JSON-массив объектов или { rows: [...] }. Целевая таблица уже должна существовать."));
    db2Layout->addWidget(jsonBox);
    tabs->addTab(scrollWrap(db2),"DB2 / Wago");

    // Вкладка «Предметы» объединена с «Редактором контента» (создание предметов/NPC/объектов/квестов/лута там).

    // client
    auto *client=new QWidget;auto *cform=new QFormLayout(client); exeSource=new QLineEdit;exeOutput=new QLineEdit;findText=new QLineEdit;replaceText=new QLineEdit("127.0.0.1");
    auto *choose=button("Выбрать исходный файл");auto *patch=button("Создать изменённую копию");
    cform->addRow("Исходный EXE (только ваша копия):",exeSource);cform->addRow("",choose);cform->addRow("Выходная копия:",exeOutput);cform->addRow("Искать ASCII-строку:",findText);cform->addRow("Заменить на:",replaceText);cform->addRow(patch);
    cform->addRow(wrapLabel("Безопасный режим: программа не запускает и не внедряется в процессы, не обходит защиту и всегда пишет отдельную копию. Новая строка не может быть длиннее исходной. Используйте только с файлами, которые вы вправе изменять; сделайте резервную копию."));
    tabs->addTab(client,"Клиент (копия)");

    // Вкладка «Патч WoW.exe» — метод Arctium Game Launcher (сигнатуры/значения изучены).
    buildWowPatchTab(tabs);

    // ---- Scripting tab (items / NPC / dungeons / raids) ----
    auto *scriptPage=new QWidget; auto *scriptLayout=new QVBoxLayout(scriptPage);
    auto *entityRow=new QHBoxLayout;
    entityType=new QComboBox; for(const auto &e:kEntities) entityType->addItem(QString::fromUtf8(e.label));
    entityEntry=new QLineEdit; entityEntry->setPlaceholderText("Entry / ID"); entityEntry->setMaximumWidth(180);
    auto *loadEntityBtn=button("Загрузить");
    entityRow->addWidget(wrapLabel("Тип сущности:")); entityRow->addWidget(entityType);
    entityRow->addWidget(wrapLabel("Entry:")); entityRow->addWidget(entityEntry);
    entityRow->addWidget(loadEntityBtn); entityRow->addStretch();
    scriptLayout->addLayout(entityRow);
    entityStatus=wrapLabel("Выберите тип и введите entry, затем нажмите «Загрузить».");
    scriptLayout->addWidget(entityStatus);
    entityFields=new QTableWidget(0,2); entityFields->setHorizontalHeaderLabels({"Поле","Значение"}); entityFields->horizontalHeader()->setStretchLastSection(true); entityFields->setMaximumHeight(180); entityFields->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scriptLayout->addWidget(wrapLabel("Данные шаблона (template):")); scriptLayout->addWidget(entityFields);
    scriptTable=new QTableWidget(0,kScriptColsPreferred.size()); scriptTable->setHorizontalHeaderLabels(kScriptColsPreferred); scriptTable->horizontalHeader()->setStretchLastSection(true); scriptTable->setMinimumHeight(180);
    scriptLayout->addWidget(wrapLabel("SmartAI-скрипт (smart_scripts) для NPC / игровых объектов. Ячейки editable: измените и нажмите «Сохранить изменения»."));
    scriptLayout->addWidget(scriptTable,1);
    auto *scriptBtns=new QHBoxLayout;
    auto *addScript=button("Добавить строку"); auto *delScript=button("Удалить строку"); auto *saveScript=button("Сохранить изменения"); auto *aiEntity=button("Попросить Джарвиса проанализировать");
    scriptBtns->addWidget(addScript);scriptBtns->addWidget(delScript);scriptBtns->addWidget(saveScript);scriptBtns->addWidget(aiEntity);scriptBtns->addStretch();
    scriptLayout->addLayout(scriptBtns);
    scriptLayout->addWidget(wrapLabel("Подсказка: SmartAI (smart_scripts) применяется к существам и игровым объектам. Для предметов, квестов и подземелий скриптование ведётся в C++-коде ядра, SmartAI/таблицах соответствующей сущности и условиях — используйте кнопку Джарвиса для предложений. Схемы полей различаются между ветками ядра; сверяйте со снимком схемы."));
    // «Скриптинг» объединён с «Редактором контента» — секция SmartAI передаётся туда.
    buildWizardsTab(tabs, scriptPage);
    buildServerTab(tabs);
    buildConsoleTab(tabs);
    buildQaTab(tabs);

    // Jarvis: integrated personal AI assistant. Uses only the owner's own API key (never embedded).
    auto *aiPage=new QWidget; auto *aiLayout=new QVBoxLayout(aiPage);
    auto *apiForm=new QFormLayout; aiEndpoint=new QLineEdit("https://api.openai.com/v1/chat/completions"); aiModel=new QLineEdit("gpt-4.1-mini"); aiKey=new QLineEdit; aiKey->setEchoMode(QLineEdit::Password);
    rememberKey=new QCheckBox("Запомнить ключ локально (только на этом компьютере)");
    offlineMode=new QCheckBox("Офлайн-режим (без API-ключа, локальный анализ)");
    apiForm->addRow("HTTPS API endpoint:",aiEndpoint); apiForm->addRow("Модель:",aiModel); apiForm->addRow("Ваш API-ключ:",aiKey); apiForm->addRow(rememberKey); apiForm->addRow(offlineMode); aiLayout->addLayout(apiForm);
    aiLayout->addWidget(wrapLabel("Джарвис выполняет только ваши команды. В офлайн-режиме он работает локально (понимание слов, определение темы, цепочка рассуждений, база знаний) и не отправляет данные в сеть — а страницы Wowhead открывает в браузере по кнопке. В онлайн-режиме используется ваш API-ключ."));
    wowheadUrl=new QLineEdit; auto *readWowhead=button("Прочитать метаданные Wowhead");
    auto *wowRow=new QHBoxLayout; wowRow->addWidget(wowheadUrl);wowRow->addWidget(readWowhead);aiLayout->addWidget(wrapLabel("Wowhead URL (предмет, NPC, квест, зона, подземелье или рейд):"));aiLayout->addLayout(wowRow);
    auto *projectRow=new QHBoxLayout;auto *loadProject=button("Загрузить папку исходников");auto *clearProject=button("Очистить контекст");auto *projectStatus=wrapLabel("Контекст проекта не загружен.");projectRow->addWidget(loadProject);projectRow->addWidget(clearProject);projectRow->addWidget(projectStatus,1);aiLayout->addWidget(wrapLabel("Контекст проекта: выберите корневую папку C++/SQL проекта. Локально прочитаются текстовые исходники, максимум 60 файлов / 120 000 символов."));aiLayout->addLayout(projectRow);
    auto *schemaRow=new QHBoxLayout;auto *loadSchema=button("Прочитать схему тестовой MySQL");auto *clearSchema=button("Очистить схему");auto *schemaStatus=wrapLabel("Схема БД не загружена.");schemaRow->addWidget(loadSchema);schemaRow->addWidget(clearSchema);schemaRow->addWidget(schemaStatus,1);aiLayout->addWidget(wrapLabel("Контекст БД: выполняются только SHOW CREATE TABLE и запросы information_schema. Строки персонажей, аккаунтов, пароли и игровые данные не читаются."));aiLayout->addLayout(schemaRow);
    aiTask=new QTextEdit;aiTask->setPlaceholderText("Что нужно реализовать или исправить?");aiTask->setMaximumHeight(80);
    aiScript=new QTextEdit;aiScript->setPlaceholderText("Вставьте SmartAI, SQL, C++ или другой скрипт для анализа.");aiScript->setMaximumHeight(120);
    aiError=new QTextEdit;aiError->setPlaceholderText("Вставьте ошибку компиляции, SQL или лог сервера.");aiError->setMaximumHeight(90);
    wowheadContext=new QTextEdit;wowheadContext->setReadOnly(true);wowheadContext->setPlaceholderText("Контекст выбранной страницы Wowhead появится здесь.");wowheadContext->setMaximumHeight(80);
    auto *askAi=button("Спросить Джарвиса"); aiResponse=new QTextEdit;aiResponse->setReadOnly(true);aiResponse->setPlaceholderText("Ответ Джарвиса появится здесь.");
    auto *openBrowser=button("Открыть ссылки в браузере");
    aiLayout->addWidget(wrapLabel("Задача:"));aiLayout->addWidget(aiTask);aiLayout->addWidget(wrapLabel("Код / SQL:"));aiLayout->addWidget(aiScript);aiLayout->addWidget(wrapLabel("Ошибка:"));aiLayout->addWidget(aiError);aiLayout->addWidget(wowheadContext);
    auto *learnBox=new QGroupBox("Обучение: локальная, проверяемая база знаний");auto *learnForm=new QFormLayout(learnBox);knowledgeTitle=new QLineEdit;knowledgeTags=new QLineEdit;knowledgeText=new QTextEdit;knowledgeText->setPlaceholderText("Например: правило вашего проекта, проверенная структура таблицы, рабочий шаблон SmartAI или решение ошибки.");knowledgeText->setMaximumHeight(90);auto *saveKnowledge=button("Запомнить знание локально");learnForm->addRow("Заголовок:",knowledgeTitle);learnForm->addRow("Теги:",knowledgeTags);learnForm->addRow("Текст:",knowledgeText);learnForm->addRow(saveKnowledge);aiLayout->addWidget(learnBox);
    auto *askRow=new QHBoxLayout; askRow->addWidget(askAi); askRow->addWidget(openBrowser); askRow->addStretch();
    aiLayout->addLayout(askRow);aiLayout->addWidget(aiResponse,1);
    tabs->addTab(scrollWrap(aiPage),"Джарвис");

    // Global action surface: common actions stay available while the detailed tools live in tabs.
    auto *projectMenu=menuBar()->addMenu("Проект");
    auto *databaseMenu=menuBar()->addMenu("База данных");
    auto *toolsMenu=menuBar()->addMenu("Инструменты");
    auto *exitAction=projectMenu->addAction("Выход");
    auto *connectAction=databaseMenu->addAction("Перейти к подключению");
    auto *refreshAction=databaseMenu->addAction("Обновить список таблиц");
    auto *aiAction=toolsMenu->addAction("Открыть Джарвиса");
    auto *perfAction=toolsMenu->addAction(QStringLiteral("Производительность и ресурсы"));
    auto *toolbar=addToolBar("Основные действия");toolbar->setMovable(false);toolbar->addAction(connectAction);toolbar->addAction(refreshAction);toolbar->addSeparator();toolbar->addAction(aiAction);
    connect(perfAction,&QAction::triggered,this,[this]{ showPerformanceDialog(); });
    connect(exitAction,&QAction::triggered,this,&QWidget::close);
    connect(connectAction,&QAction::triggered,this,[this,tabs]{tabs->setCurrentIndex(0);});
    connect(refreshAction,&QAction::triggered,this,[this,tabs]{tabs->setCurrentIndex(1);refreshTables();});
    connect(aiAction,&QAction::triggered,this,[this,tabs]{tabs->setCurrentIndex(7);});

    log=new QTextEdit;log->setReadOnly(true);log->setMaximumHeight(115);auto *dock=new QDockWidget("Журнал",this);dock->setWidget(log);addDockWidget(Qt::BottomDockWidgetArea,dock);
    statusBar()->showMessage("Готово. Проверка сети в фоне…");
    tabs->setEnabled(true);

    connect(&m_guard,&NetworkGuard::changed,this,[this](bool online,const QString &msg){
        setOnline(online,msg);
        // Вкладки и поля ввода всегда доступны: сеть нужна только Wago / онлайн-Джарвису.
    });
    auto chooseRepository=[this]{
        const auto repo=CoreRepositoryService::forProfile(core->currentText());
        coreRepository->setText(repo.gitUrl+" ("+repo.language+")");
        corePatch->clear();
        coreBranch->clear();
        const QString t=core->currentText();
        if (t.contains("3.3.5")) coreBranch->addItem("3.3.5");
        else if (t.contains("4.3.4")) coreBranch->addItem("4.3.4");
        else if (t.contains("5.4.8")) coreBranch->addItem("5.4.8");
        else if (t.contains("Nordrassil") || t.contains("7.3.5")) coreBranch->addItem("7.3.5");
        else coreBranch->addItem("master");
    };
    connect(core,&QComboBox::currentTextChanged,this,[this,chooseRepository](const QString &){chooseRepository(); fillCommandTree();});chooseRepository();
    connect(loadBranches,&QPushButton::clicked,this,[this]{m_coreRepositories.fetchBranches(CoreRepositoryService::forProfile(core->currentText()));});
    connect(checkPatch,&QPushButton::clicked,this,[this]{corePatch->setText("Проверка GitHub README…");m_coreRepositories.fetchPatchInfo(CoreRepositoryService::forProfile(core->currentText()));});
    connect(&m_coreRepositories,&CoreRepositoryService::patchInfoLoaded,this,[this](const QString &text){corePatch->setText(text);addLog("GitHub: обновлена информация о версии ядра.");});
    connect(&m_coreRepositories,&CoreRepositoryService::branchesLoaded,this,[this](const QStringList &branches){const auto current=coreBranch->currentText();coreBranch->clear();coreBranch->addItems(branches);const int i=coreBranch->findText(current);if(i>=0)coreBranch->setCurrentIndex(i);addLog("GitHub: загружены ветви ядра.");});
    connect(&m_coreRepositories,&CoreRepositoryService::failed,this,[this](const QString &why){QMessageBox::warning(this,"Репозиторий ядра",why);});
    connect(connectButton,&QPushButton::clicked,this,[this]{
        // Логин и пароль убраны из вкладки — запрашиваем их отдельным окном.
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Подключение к базе данных"));
        auto *fl=new QFormLayout(&dlg);
        auto *u=new QLineEdit(user->text().isEmpty()?QStringLiteral("root"):user->text());
        auto *pw=new QLineEdit; pw->setEchoMode(QLineEdit::Password); pw->setText(password->text());
        fl->addRow(QStringLiteral("Пользователь:"),u);
        fl->addRow(QStringLiteral("Пароль:"),pw);
        auto *box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Подключиться"));
        box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        fl->addRow(box);
        connect(box,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
        connect(box,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()!=QDialog::Accepted) return;
        user->setText(u->text().trimmed());
        password->setText(pw->text());
        DbProfile p{host->text().trimmed(),port->value(),dbSelector->currentText().trimmed(),user->text().trimmed(),password->text(),core->currentText()};
        connectWithProfile(p);
    });
    connect(detectNow,&QPushButton::clicked,this,[this]{runCoreDetection();});
    connect(browseDetect,&QPushButton::clicked,this,[this]{chooseDetectFolder();});
    connect(detectFolder,&QLineEdit::editingFinished,this,[this]{
        QSettings s("WoWDBStudio","WoWDBStudio");
        s.setValue("detect/folder", detectFolder->text().trimmed());
    });
    connect(dbSelector,QOverload<int>::of(&QComboBox::activated),this,[this](int){ if(m_db.isOpen()){ auto p=m_profile; p.database=dbSelector->currentText().trimmed(); connectWithProfile(p);} });
    connect(run,&QPushButton::clicked,this,[this]{auto statement=sql->toPlainText().trimmed();if(statement.isEmpty())return;auto upper=statement.left(12).toUpper();if(!upper.startsWith("SELECT")&&!upper.startsWith("SHOW")&&!upper.startsWith("DESCRIBE")&&QMessageBox::warning(this,"Подтверждение","Этот SQL может изменить данные. Продолжить?",QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes)return;QString e;qint64 count;if(!m_db.execute(statement,&e,&count))QMessageBox::critical(this,"Ошибка SQL",e);else{addLog("SQL выполнен; затронуто строк: "+QString::number(count));refreshTables();}});
    connect(&m_importer,&Db2Importer::finished,this,[this](bool ok,const QString &msg){if(ok)addLog("DB2: "+msg);else QMessageBox::critical(this,"DB2 импорт",msg);});
    connect(import,&QPushButton::clicked,this,[this]{m_importer.importUrl(QUrl::fromUserInput(db2Url->text()),db2Table->text().trimmed(),m_db.database());});
    connect(loadWagoTables,&QPushButton::clicked,this,[this]{
        m_wagoTablesBuild.clear();
        QString build=wagoBuild->currentText().trimmed();
        if(build.isEmpty()) build = m_detectedWagoBuild;
        if(build.isEmpty() && m_wago.knownBuilds().isEmpty()){ fetchWagoBuilds(); return; }
        if(build.isEmpty()){QMessageBox::information(this,"Wago","Сначала выберите патч / build в списке (например 12.1.0.69497).");return;}
        const QString resolved = WagoService::resolveBuild(build, m_wago.knownBuilds());
        if (resolved != build) wagoBuild->setEditText(resolved);
        wagoStatus->setText("Загрузка списка таблиц патча "+resolved+"…");
        m_wagoTablesBuild = resolved;
        m_wago.fetchTables(resolved);
    });
    connect(analyzeWago,&QPushButton::clicked,this,[this]{analyzeWagoTable();});
    connect(createWagoTable,&QPushButton::clicked,this,[this]{
        QString err;
        if (!createHotfixTableFromWago(&err)) showWagoError(QStringLiteral("Создание таблицы"), err);
        else { loadLocalColumns(); buildMapping(); }
    });
    connect(importWago,&QPushButton::clicked,this,[this]{importWagoMapped();});
    connect(&m_wago,&WagoService::buildsLoaded,this,[this](const QStringList &builds){
        wagoBuild->clear(); wagoBuild->addItems(builds);
        const QString want = !m_detectedWagoBuild.isEmpty() ? m_detectedWagoBuild : wagoBuild->currentText();
        selectDetectedWagoBuild(want);
        wagoStatus->setText("Загружено патчей: "+QString::number(builds.size())
                            + (m_detectedWagoBuild.isEmpty() ? QString()
                               : QString::fromUtf8(". Выбран патч ядра %1 (realmlist.gamebuild).").arg(m_detectedWagoBuild)));
        addLog("Wago: загружен список патчей ("+QString::number(builds.size())+").");
        if (!m_detectedWagoBuild.isEmpty())
            addLog("Wago: патч с ядра / realmlist.gamebuild → "+m_detectedWagoBuild);
        ensureWagoTableList();
        updateWagoNameMatch();
    });
    connect(&m_wago,&WagoService::tablesLoaded,this,[this](const QStringList &names){
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        const QStringList old = s.value(QStringLiteral("wago/cacheTables")).toStringList();
        const QString oldBuild = s.value(QStringLiteral("wago/cacheBuild")).toString();
        fillWagoTablePicker(names);
        const QString build = m_wago.cachedBuild().isEmpty()
            ? (wagoBuild ? wagoBuild->currentText().trimmed() : QString())
            : m_wago.cachedBuild();
        m_wago.saveDiskCache(build);
        int added = 0, removed = 0;
        if (!old.isEmpty()) {
            QSet<QString> o, n;
            for (const auto &t : old) o.insert(t.toLower());
            for (const auto &t : names) n.insert(t.toLower());
            for (const auto &t : n) if (!o.contains(t)) ++added;
            for (const auto &t : o) if (!n.contains(t)) ++removed;
        }
        QString msg = QString::fromUtf8("Wago: сохранено %1 таблиц патча %2.")
                          .arg(names.size()).arg(build.isEmpty() ? QStringLiteral("?") : build);
        if (!old.isEmpty() && (added || removed || oldBuild != build))
            msg += QString::fromUtf8(" Сверка с прошлым запуском: +%1 / −%2.").arg(added).arg(removed);
        else if (old.isEmpty())
            msg += QString::fromUtf8(" Список сохранён на этом компьютере.");
        wagoStatus->setText(msg + QString::fromUtf8(" Выберите таблицу в списке."));
        addLog(msg);
        updateWagoNameMatch();
    });
    connect(&m_wago,&WagoService::tableLoaded,this,[this](bool ok,const QString &msg){
        if(!ok){
            m_wagoImportPending = false;
            showWagoError(QStringLiteral("Wago"), msg);
            return;
        }
        addLog("Wago: "+msg);
        if (m_wagoImportPending) {
            m_wagoImportPending = false;
            performWagoImport();
            return;
        }
        loadLocalColumns();
        if (m_localColumns.isEmpty() && !m_wago.columns().isEmpty()) {
            const QString schema = hotfixSchema();
            const QString local = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();
            const auto reply = QMessageBox::question(this, QStringLiteral("Таблицы нет"),
                QString::fromUtf8("В `%1` нет таблицы `%2`.\n\nСоздать её по колонкам Wago и сопоставить поля 1:1?")
                    .arg(schema, local.isEmpty() ? QStringLiteral("?") : local),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (reply == QMessageBox::Yes) {
                QString err;
                if (!createHotfixTableFromWago(&err)) { showWagoError(QStringLiteral("Создание таблицы"), err); return; }
                loadLocalColumns();
            }
        }
        buildMapping();
    });
    connect(&m_wago,&WagoService::failed,this,[this](const QString &why){
        m_wagoImportPending = false;
        m_wagoTablesBuild.clear();
        if (!m_wago.knownTables().isEmpty()) {
            wagoStatus->setText(QString::fromUtf8("%1 Показан сохранённый список (%2 табл.).")
                                    .arg(why).arg(m_wago.knownTables().size()));
            addLog(why);
            return;
        }
        showWagoError(QStringLiteral("Wago"), why);
    });
    connect(wagoTableName, QOverload<int>::of(&QComboBox::activated), this, [this](int){
        applyWagoTableSelection(wagoTableName->currentText());
    });
    connect(wagoTableName, &QComboBox::currentTextChanged, this, [this](const QString &name){
        if (name.trimmed().isEmpty()) return;
        applyWagoTableSelection(name);
    });
    connect(wagoBuild, QOverload<int>::of(&QComboBox::activated), this, [this](int){
        m_wagoTablesBuild.clear();
        ensureWagoTableList();
    });
    connect(wagoLocalTable, &QComboBox::currentTextChanged, this, [this](const QString &){ updateWagoNameMatch(); });
    connect(wagoHotfixDb,&QComboBox::currentTextChanged,this,[this](const QString &){ refreshWagoHotfixTables(); });
    connect(choose,&QPushButton::clicked,this,[this]{auto f=QFileDialog::getOpenFileName(this,"Выберите EXE",{},"Executable (*.exe);;All files (*)");if(!f.isEmpty()){exeSource->setText(f);exeOutput->setText(f+".localhost-copy.exe");}});
    connect(patch,&QPushButton::clicked,this,[this]{QString e;if(ClientPatchService::replaceAsciiInCopy(exeSource->text(),exeOutput->text(),findText->text(),replaceText->text(),&e))addLog("Создана копия клиента: "+exeOutput->text());else QMessageBox::critical(this,"Копирование/замена",e);});
    connect(readWowhead,&QPushButton::clicked,this,[this]{m_ai.inspectWowhead(QUrl::fromUserInput(wowheadUrl->text().trimmed()));});
    connect(loadProject,&QPushButton::clicked,this,[this,projectStatus]{const auto folder=QFileDialog::getExistingDirectory(this,"Корень исходного проекта");if(folder.isEmpty())return;const auto loaded=ProjectContext::loadFolder(folder);m_projectContext=loaded.content;projectStatus->setText(QString("Загружено: %1 файл(ов)%2").arg(loaded.files).arg(loaded.truncated?"; контекст усечён":""));addLog("Загружен контекст исходников: "+QString::number(loaded.files)+" файл(ов).");});
    connect(clearProject,&QPushButton::clicked,this,[this,projectStatus]{m_projectContext.clear();projectStatus->setText("Контекст проекта очищен.");});
    connect(loadSchema,&QPushButton::clicked,this,[this,schemaStatus]{QString e;m_schemaContext=SchemaContext::collect(m_db.database(),aiTask->toPlainText()+" "+aiScript->toPlainText()+" "+aiError->toPlainText(),&e);if(m_schemaContext.isEmpty()){QMessageBox::warning(this,"Схема MySQL",e);return;}schemaStatus->setText("Схема прочитана: "+QString::number(m_schemaContext.size())+" символов.");addLog("Получен read-only снимок схемы MySQL.");});
    connect(clearSchema,&QPushButton::clicked,this,[this,schemaStatus]{m_schemaContext.clear();schemaStatus->setText("Схема БД очищена.");});
    connect(saveKnowledge,&QPushButton::clicked,this,[this]{QString e;if(m_knowledge.add({knowledgeTitle->text().trimmed(),knowledgeText->toPlainText().trimmed(),knowledgeTags->text().trimmed()},&e)){addLog("Локальная база знаний обновлена. Всего записей: "+QString::number(m_knowledge.count()));knowledgeTitle->clear();knowledgeTags->clear();knowledgeText->clear();}else QMessageBox::warning(this,"Обучение",e);});
    connect(askAi,&QPushButton::clicked,this,[this]{
        if(offlineMode->isChecked()){ runOfflineJarvis(); return; }
        m_ai.configure(QUrl::fromUserInput(aiEndpoint->text().trimmed()),aiModel->text().trimmed(),aiKey->text());saveJarvisSettings();const auto query=aiTask->toPlainText()+" "+aiScript->toPlainText()+" "+aiError->toPlainText()+" "+wowheadContext->toPlainText();const auto memory=m_knowledge.search(query);m_ai.ask(aiTask->toPlainText(),aiScript->toPlainText(),aiError->toPlainText(),core->currentText()+" / "+coreBranch->currentText()+" / "+corePatch->text(),wowheadContext->toPlainText(),memory,m_projectContext,m_schemaContext);
    });
    connect(openBrowser,&QPushButton::clicked,this,[this]{ if(m_jarvisUrls.isEmpty()){ QMessageBox::information(this,"Джарвис","Сначала задайте вопрос в офлайн-режиме — ссылки появятся после анализа."); return; } for(const auto &u:m_jarvisUrls) QDesktopServices::openUrl(QUrl(u)); addLog("Джарвис: открыты ссылки в браузере."); });
    connect(&m_ai,&AiService::answerReady,this,[this](const QString &text){aiResponse->setPlainText(text);addLog("Джарвис: получен ответ.");});
    connect(&m_ai,&AiService::wowheadReady,this,[this](const QString &text){wowheadContext->setPlainText(text);addLog("Wowhead: метаданные страницы прочитаны.");});
    connect(&m_ai,&AiService::failed,this,[this](const QString &why){QMessageBox::warning(this,"Джарвис",why);addLog("Джарвис: "+why);});

    // Scripting tab wiring
    connect(loadEntityBtn,&QPushButton::clicked,this,[this]{loadEntity();});
    connect(addScript,&QPushButton::clicked,this,[this]{addScriptRow();});
    connect(delScript,&QPushButton::clicked,this,[this]{deleteScriptRow();});
    connect(saveScript,&QPushButton::clicked,this,[this]{saveScriptRow();});
    connect(aiEntity,&QPushButton::clicked,this,[this]{askAiForEntity();});

    loadJarvisSettings();
    loadServerSettings();
    loadWagoCache();
    // Фоновые сетевые проверки — после первой отрисовки, чтобы окно открылось быстрее.
    QTimer::singleShot(0, this, [this]{
        m_guard.check();
        if (m_wago.knownTables().isEmpty()) fetchWagoBuilds();
        else ensureWagoTableList();
    });
    m_startupMs = startupTimer.elapsed();
}

void MainWindow::connectWithProfile(const DbProfile &p) {
    QString e;
    if (m_db.connectTo(p, &e)) {
        m_profile = p;
        addLog("MySQL подключён: " + p.database + " (" + p.core + ")");
        loadDatabases(p.database);
        refreshTables();
        refreshScriptColumns();
        runCoreDetection();
    } else {
        QMessageBox::critical(this, QStringLiteral("MySQL"), friendlyDbError(e, p));
    }
}

void MainWindow::loadDatabases(const QString &selectPreferred) {
    QString e; auto list = m_db.databases(&e);
    if (!e.isEmpty()) { addLog("Список баз не получен: " + e); return; }
    const bool old = dbSelector->blockSignals(true);
    const auto current = selectPreferred.isEmpty() ? dbSelector->currentText() : selectPreferred;
    dbSelector->clear();
    dbSelector->addItems(list);
    int i = dbSelector->findText(current);
    if (i >= 0) dbSelector->setCurrentIndex(i);
    else if (!current.isEmpty()) { dbSelector->addItem(current); dbSelector->setCurrentIndex(dbSelector->count()-1); }
    dbSelector->blockSignals(old);
    addLog("Доступные базы данных: " + list.join(", "));
    selectHotfixDatabase(list);
}

void MainWindow::loadEntity() {
    if (!m_db.isOpen()) { QMessageBox::warning(this,"Скриптинг","Сначала подключитесь к базе."); return; }
    bool ok=false; const qint64 entry=entityEntry->text().trimmed().toLongLong(&ok);
    if(!ok){ QMessageBox::warning(this,"Скриптинг","Введите числовой entry."); return; }
    const int type=entityType->currentIndex();
    if(type<0 || type>=int(sizeof(kEntities)/sizeof(kEntities[0]))) return;
    const auto &spec=kEntities[type];
    entityFields->setRowCount(0); scriptTable->setRowCount(0); m_scriptKeys.clear();
    // Load template row
    QSqlQuery q(m_db.database());
    const QString sql=QString("SELECT * FROM `%1` WHERE `%2` = ? LIMIT 1").arg(spec.table,spec.key);
    q.prepare(sql); q.addBindValue(entry);
    if(!q.exec()){ entityStatus->setText("Ошибка: "+q.lastError().text()); addLog("Скриптинг: "+q.lastError().text()); return; }
    if(!q.next()){ entityStatus->setText(QString("Запись %1=%2 не найдена в %3.").arg(spec.key).arg(entry).arg(spec.table)); return; }
    const auto rec=q.record();
    for(int i=0;i<rec.count();++i){ const int r=entityFields->rowCount(); entityFields->insertRow(r); auto *n=new QTableWidgetItem(rec.fieldName(i)); auto *v=new QTableWidgetItem(rec.value(i).toString()); entityFields->setItem(r,0,n); entityFields->setItem(r,1,v); }
    entityStatus->setText(QString("Загружено: %1, %2=%3 (%4 полей).").arg(QString::fromUtf8(spec.label)).arg(spec.key).arg(entry).arg(rec.count()));
    addLog(QString("Скриптинг: загружен %1 %2=%3").arg(spec.table).arg(spec.key).arg(entry));
    if(spec.smartSourceType>=0) loadScripts(entry,spec.smartSourceType);
    else entityStatus->setText(entityStatus->text()+" SmartAI для этого типа не применим — скриптование ведётся в C++ ядра и условиях. Используйте «Попросить Джарвиса».");
}

void MainWindow::refreshScriptColumns() {
    m_scriptCols.clear();
    m_scriptFloatCols.clear();
    m_scriptCommentCol = -1;
    QSet<QString> have;
    if (m_db.isOpen()) {
        QSqlQuery q(m_db.database());
        if (q.exec(QStringLiteral("SHOW COLUMNS FROM `smart_scripts`"))) {
            while (q.next()) have.insert(q.value(0).toString().toLower());
        }
    }
    for (const auto &c : kScriptColsPreferred) {
        if (have.isEmpty() && c == QLatin1String("event_param5")) continue; // 3.3.5a fallback
        if (!have.isEmpty() && !have.contains(c.toLower())) continue;
        m_scriptCols << c;
    }
    if (m_scriptCols.isEmpty()) {
        for (const auto &c : kScriptColsPreferred)
            if (c != QLatin1String("event_param5")) m_scriptCols << c;
    }
    for (int i = 0; i < m_scriptCols.size(); ++i) {
        const QString n = m_scriptCols.at(i);
        if (n == QLatin1String("comment")) m_scriptCommentCol = i;
        if (n == QLatin1String("target_x") || n == QLatin1String("target_y")
            || n == QLatin1String("target_z") || n == QLatin1String("target_o"))
            m_scriptFloatCols.insert(i);
    }
    if (scriptTable) {
        scriptTable->setColumnCount(m_scriptCols.size());
        scriptTable->setHorizontalHeaderLabels(m_scriptCols);
    }
    if (!have.isEmpty() && !have.contains(QLatin1String("event_param5")))
        addLog("Скриптинг: в smart_scripts нет event_param5 (схема 3.3.5a/WotLK). Колонка пропущена.");
}

void MainWindow::loadScripts(qint64 entryorguid,int sourceType) {
    if (m_scriptCols.isEmpty()) refreshScriptColumns();
    scriptTable->setRowCount(0); m_scriptKeys.clear();
    const QString colList=m_scriptCols.join(", ");
    QSqlQuery q(m_db.database());
    q.prepare(QString("SELECT %1 FROM smart_scripts WHERE entryorguid = ? AND source_type = ? ORDER BY id, link").arg(colList));
    q.addBindValue(entryorguid); q.addBindValue(sourceType);
    if(!q.exec()){
        entityStatus->setText(entityStatus->text()+" Ошибка smart_scripts: "+q.lastError().text());
        addLog("Скриптинг: "+q.lastError().text()+" — колонки берутся из SHOW COLUMNS. На 3.3.5a нет event_param5.");
        refreshScriptColumns();
        return;
    }
    int row=0;
    while(q.next()){
        scriptTable->insertRow(row);
        for(int c=0;c<m_scriptCols.size();++c){
            auto *it=new QTableWidgetItem(q.value(c).toString());
            if(c==0||c==1||c==2) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            scriptTable->setItem(row,c,it);
        }
        m_scriptKeys.append({q.value(0).toLongLong(), q.value(1).toInt(), q.value(2).toInt()});
        ++row;
    }
    entityStatus->setText(entityStatus->text()+QString(" Строк SmartAI: %1. Колонок smart_scripts: %2.")
                              .arg(row).arg(m_scriptCols.size()));
}

void MainWindow::addScriptRow() {
    if(scriptTable->rowCount()==0 && m_scriptKeys.isEmpty()){ QMessageBox::information(this,"Скриптинг","Сначала загрузите NPC или игровой объект, чтобы определить entryorguid/source_type."); return; }
    const auto key=m_scriptKeys.first();
    QSqlQuery q(m_db.database());
    q.prepare("SELECT COALESCE(MAX(id),-1)+1 FROM smart_scripts WHERE entryorguid=? AND source_type=?");
    q.addBindValue(key.entryorguid); q.addBindValue(key.sourceType);
    if(!q.exec() || !q.next()){ QMessageBox::critical(this,"Скриптинг",q.lastError().text()); return; }
    const int nextId=q.value(0).toInt();
    QSqlQuery ins(m_db.database());
    if (m_scriptCols.isEmpty()) refreshScriptColumns();
    const QString cols=m_scriptCols.join(", ");
    QStringList marks; for(int i=0;i<m_scriptCols.size();++i) marks<<"?";
    ins.prepare(QString("INSERT INTO smart_scripts (%1) VALUES (%2)").arg(cols,marks.join(",")));
    for(int c=0;c<m_scriptCols.size();++c){
        if(c==0) ins.addBindValue(key.entryorguid);
        else if(c==1) ins.addBindValue(key.sourceType);
        else if(c==2) ins.addBindValue(nextId);
        else if(c==m_scriptCommentCol) ins.addBindValue(QString());
        else if(m_scriptFloatCols.contains(c)) ins.addBindValue(0.0);
        else ins.addBindValue(0);
    }
    if(!ins.exec()){ QMessageBox::critical(this,"Скриптинг","Ошибка добавления: "+ins.lastError().text()); return; }
    addLog(QString("Скриптинг: добавлена строка SmartAI id=%1 для entryorguid=%2").arg(nextId).arg(key.entryorguid));
    loadScripts(key.entryorguid,key.sourceType);
}

void MainWindow::deleteScriptRow() {
    const int row=scriptTable->currentRow();
    if(row<0 || row>=m_scriptKeys.size()){ QMessageBox::information(this,"Скриптинг","Выберите строку для удаления."); return; }
    const auto key=m_scriptKeys[row];
    if(QMessageBox::question(this,"Скриптинг",QString("Удалить строку SmartAI (entryorguid=%1, source_type=%2, id=%3)?").arg(key.entryorguid).arg(key.sourceType).arg(key.id))!=QMessageBox::Yes) return;
    QSqlQuery q(m_db.database());
    q.prepare("DELETE FROM smart_scripts WHERE entryorguid=? AND source_type=? AND id=?");
    q.addBindValue(key.entryorguid); q.addBindValue(key.sourceType); q.addBindValue(key.id);
    if(!q.exec()){ QMessageBox::critical(this,"Скриптинг",q.lastError().text()); return; }
    addLog(QString("Скриптинг: удалена строка SmartAI id=%1").arg(key.id));
    loadScripts(key.entryorguid,key.sourceType);
}

void MainWindow::saveScriptRow() {
    const int row=scriptTable->currentRow();
    if(row<0 || row>=m_scriptKeys.size()){ QMessageBox::information(this,"Скриптинг","Выберите строку для сохранения."); return; }
    const auto key=m_scriptKeys[row];
    // Build UPDATE for all non-key columns from the grid.
    if (m_scriptCols.isEmpty()) refreshScriptColumns();
    QStringList sets;
    for(int c=0;c<m_scriptCols.size();++c){ if(c==0||c==1||c==2) continue; sets<<("`"+m_scriptCols[c]+"` = ?"); }
    QSqlQuery q(m_db.database());
    q.prepare(QString("UPDATE smart_scripts SET %1 WHERE entryorguid=? AND source_type=? AND id=?").arg(sets.join(", ")));
    for(int c=0;c<m_scriptCols.size();++c){
        if(c==0||c==1||c==2) continue;
        const QString v=scriptTable->item(row,c)?scriptTable->item(row,c)->text():QString();
        if(c==m_scriptCommentCol) q.addBindValue(v);
        else if(m_scriptFloatCols.contains(c)) q.addBindValue(v.toDouble());
        else q.addBindValue(v.toLongLong());
    }
    q.addBindValue(key.entryorguid); q.addBindValue(key.sourceType); q.addBindValue(key.id);
    if(!q.exec()){ QMessageBox::critical(this,"Скриптинг","Ошибка сохранения: "+q.lastError().text()); return; }
    addLog(QString("Скриптинг: сохранена строка SmartAI id=%1").arg(key.id));
    loadScripts(key.entryorguid,key.sourceType);
}

void MainWindow::loadJarvisSettings() {
    QSettings s("WoWDBStudio","WoWDBStudio");
    aiEndpoint->setText(s.value("jarvis/endpoint", aiEndpoint->text()).toString());
    aiModel->setText(s.value("jarvis/model", aiModel->text()).toString());
    rememberKey->setChecked(s.value("jarvis/remember", false).toBool());
    offlineMode->setChecked(s.value("jarvis/offline", false).toBool());
    if (rememberKey->isChecked()) aiKey->setText(s.value("jarvis/key").toString());
    m_ai.configure(QUrl::fromUserInput(aiEndpoint->text().trimmed()), aiModel->text().trimmed(), aiKey->text());
    if (detectFolder) detectFolder->setText(s.value("detect/folder").toString());
}

void MainWindow::saveJarvisSettings() {
    QSettings s("WoWDBStudio","WoWDBStudio");
    s.setValue("jarvis/endpoint", aiEndpoint->text().trimmed());
    s.setValue("jarvis/model", aiModel->text().trimmed());
    s.setValue("jarvis/remember", rememberKey->isChecked());
    s.setValue("jarvis/offline", offlineMode->isChecked());
    // Ключ сохраняется только если владелец явно отметил это. Иначе очищаем сохранённое значение.
    if (rememberKey->isChecked()) s.setValue("jarvis/key", aiKey->text());
    else s.remove("jarvis/key");
}

void MainWindow::runOfflineJarvis() {
    const QString coreStr = core->currentText() + " / " + coreBranch->currentText() + " / " + corePatch->text();
    const QString query = aiTask->toPlainText() + " " + aiScript->toPlainText() + " " + aiError->toPlainText() + " " + wowheadContext->toPlainText();
    const auto memory = m_knowledge.search(query);
    const auto res = m_jarvis.analyze(aiTask->toPlainText(), aiScript->toPlainText(), aiError->toPlainText(), coreStr, memory, m_projectContext, m_schemaContext);
    m_jarvisUrls = res.suggestedUrls;
    QString full;
    for (const auto &step : res.reasoning) full += "▸ " + step + "\n";
    full += "\n";
    full += res.answer;

    const QString low = query.toLower();

    // --- Практическая подсказка: что сделать в Studio ---
    if (low.contains("предмет") || low.contains("item"))
        full += QStringLiteral("\n\n💡 В Studio: вкладка «Редактор контента» → тип «Предмет» → заполни поля → «Сгенерировать INSERT» (схема проверится автоматически). Для НОВОГО предмета в Legion нужен ещё патч клиентских DB2 — см. знание «Патч клиента: DB2/DBCache».");
    else if (low.contains("npc") || low.contains("нпс") || low.contains("моб") || low.contains("существ"))
        full += QStringLiteral("\n\n💡 В Studio: «Редактор контента» → «NPC / существо» → entry, имя, уровень, npcflag, AIName=SmartAI. Привязку квестов и триггеры проверь кнопкой «Проверить NPC».");
    else if (low.contains("объект") || low.contains("gameobject"))
        full += QStringLiteral("\n\n💡 В Studio: «Редактор контента» → «Объект (GameObject)» → entry, type (2=QUEST, 3=CHEST, 8=DOOR), displayId.");
    else if (low.contains("квест") || low.contains("quest") || low.contains("задан"))
        full += QStringLiteral("\n\n💡 В Studio: «Редактор контента» → «Квест». Цели — quest_objectives, текст ruRU — quest_template_locale, привязка к NPC — creature_queststarter/questender. Связи проверь кнопкой «Проверить квест».");
    else if (low.contains("лут") || low.contains("loot") || low.contains("дроп"))
        full += QStringLiteral("\n\n💡 В Studio: «Редактор контента» → «Лут существа». groupid=0 — независимый дроп, groupid>0 — «один из N». В Nordrassil shared=1 ≈ доп. проверка 0.5% (кастомный LootMgr).");

    // --- Живая диагностика подключённой базы (если есть соединение) ---
    if (m_db.isOpen()) {
        QVector<int> ids; QString num;
        for (const QChar &c : query) {
            if (c.isDigit()) { num += c; }
            else { if (!num.isEmpty()) { bool ok=false; const int v=num.toInt(&ok); if (ok && v>0 && !ids.contains(v) && ids.size()<3) ids.append(v); num.clear(); } }
        }
        if (!num.isEmpty()) { bool ok=false; const int v=num.toInt(&ok); if (ok && v>0 && !ids.contains(v) && ids.size()<3) ids.append(v); }
        const bool wantQuest = low.contains("квест") || low.contains("quest") || low.contains("задан");
        const bool wantNpc = low.contains("npc") || low.contains("нпс") || low.contains("моб") || low.contains("существ") || low.contains("creature");
        if (!ids.isEmpty() && (wantQuest || wantNpc)) {
            full += QStringLiteral("\n\n===== Живая диагностика подключённой базы =====\n");
            for (int id : ids) {
                if (wantQuest) full += ContentEditorService::diagnoseQuest(m_db, id) + "\n";
                if (wantNpc) full += ContentEditorService::diagnoseNpc(m_db, id) + "\n";
            }
            addLog(QStringLiteral("Джарвис: выполнена живая диагностика по %1 ID.").arg(ids.size()));
        }
    }

    aiResponse->setPlainText(full);
    addLog("Джарвис (офлайн): завершён локальный анализ. Тем: " + QString::number(res.reasoning.size()) + ".");
}

void MainWindow::askAiForEntity() {
    const auto type=entityType->currentText();
    const auto entry=entityEntry->text().trimmed();
    QString fields;
    for(int r=0;r<entityFields->rowCount();++r){ if(entityFields->item(r,0)&&entityFields->item(r,1)) fields+=entityFields->item(r,0)->text()+" = "+entityFields->item(r,1)->text()+"\n"; }
    QString script;
    const auto &cols = m_scriptCols.isEmpty() ? kScriptColsPreferred : m_scriptCols;
    if(scriptTable->rowCount()){
        script="SmartAI (smart_scripts) строки:\n";
        script += cols.join(" | ") + "\n";
        for(int r=0;r<scriptTable->rowCount();++r){
            QStringList rowVals;
            for(int c=0;c<cols.size();++c) rowVals<<(scriptTable->item(r,c)?scriptTable->item(r,c)->text():QString());
            script+=rowVals.join(" | ")+"\n";
        }
    }
    const QString task=QStringLiteral("Проанализируй SmartAI/скрипт сущности «%1» (entry=%2). Объясни события и действия, укажи ошибки и предложи правки.")
                           .arg(type, entry);
    if (aiTask) aiTask->setPlainText(task);
    if (aiScript) aiScript->setPlainText(fields + script);
    if (auto *tabs = qobject_cast<QTabWidget *>(centralWidget()))
        tabs->setCurrentIndex(7);
    runOfflineJarvis();
    addLog("Скриптинг: офлайн-Джарвис разобрал сущность "+type+" entry="+entry);
    if (offlineMode && offlineMode->isChecked()) return;
    m_ai.configure(QUrl::fromUserInput(aiEndpoint->text().trimmed()),aiModel->text().trimmed(),aiKey->text());
    m_ai.ask(task, fields+script, entityStatus?entityStatus->text():QString(), core->currentText()+" / "+coreBranch->currentText()+" / "+corePatch->text(), {}, {}, m_projectContext, m_schemaContext);
    addLog("Скриптинг: отправлен запрос Джарвису по сущности "+type+" entry="+entry);
}

void MainWindow::loadWagoCache() {
    if (!m_wago.loadDiskCache()) {
        fillWagoTablePicker({});
        if (wagoStatus)
            wagoStatus->setText(QStringLiteral("Списка wago.tools ещё нет. Подключитесь к MySQL — патч определится, таблицы скачаются и сохранятся."));
        return;
    }
    const QString build = m_wago.cachedBuild();
    m_detectedWagoBuild = build;
    if (wagoBuild && !build.isEmpty()) {
        const bool blocked = wagoBuild->blockSignals(true);
        if (wagoBuild->findText(build, Qt::MatchFixedString) < 0 && !m_wago.knownBuilds().isEmpty())
            wagoBuild->addItems(m_wago.knownBuilds());
        else if (wagoBuild->findText(build, Qt::MatchFixedString) < 0)
            wagoBuild->addItem(build);
        const int i = wagoBuild->findText(build, Qt::MatchFixedString);
        if (i >= 0) wagoBuild->setCurrentIndex(i);
        else wagoBuild->setEditText(build);
        wagoBuild->blockSignals(blocked);
    }
    fillWagoTablePicker(m_wago.knownTables());
    if (wagoStatus)
        wagoStatus->setText(QString::fromUtf8("Кэш wago.tools: %1 таблиц патча %2. При подключении к MySQL список сверится с сайтом.")
                                .arg(m_wago.knownTables().size())
                                .arg(build.isEmpty() ? QStringLiteral("?") : build));
    addLog(QString::fromUtf8("Wago: загружен локальный кэш (%1 таблиц, патч %2).")
               .arg(m_wago.knownTables().size())
               .arg(build.isEmpty() ? QStringLiteral("?") : build));
}

void MainWindow::updateHotfixInfoLabel() {
    if (!wagoHotfixInfo) return;
    const QString schema = hotfixSchema();
    if (schema.isEmpty())
        wagoHotfixInfo->setText(QStringLiteral("hotfixes — определится при подключении к MySQL."));
    else
        wagoHotfixInfo->setText(QString::fromUtf8("%1 — выбрана автоматически, сменить нельзя.").arg(schema));
}

void MainWindow::ensureWagoTableList() {
    QString build = wagoBuild ? wagoBuild->currentText().trimmed() : QString();
    if (build.isEmpty()) build = m_detectedWagoBuild;
    if (m_wago.knownBuilds().isEmpty()) {
        m_wago.fetchBuilds();
        return;
    }
    const QString resolved = build.isEmpty()
        ? QString()
        : WagoService::resolveBuild(build, m_wago.knownBuilds());
    if (!resolved.isEmpty()
        && m_wagoTablesBuild.compare(resolved, Qt::CaseInsensitive) == 0
        && !m_wago.knownTables().isEmpty())
        return;
    if (resolved.isEmpty() && !m_wago.knownTables().isEmpty()) return;
    m_wagoTablesBuild = resolved;
    if (wagoStatus)
        wagoStatus->setText(resolved.isEmpty()
            ? QStringLiteral("Загрузка списка таблиц с wago.tools…")
            : QStringLiteral("Загрузка списка таблиц патча %1 с wago.tools…").arg(resolved));
    m_wago.fetchTables(resolved);
}

void MainWindow::fillWagoTablePicker(const QStringList &names) {
    const QString keep = wagoTableName ? wagoTableName->currentText().trimmed() : QString();
    const QString resolved = WagoService::resolveTableName(keep, names);
    bool keepOk = false;
    for (const auto &n : names) {
        if (n.compare(resolved, Qt::CaseInsensitive) == 0) { keepOk = true; break; }
    }

    if (wagoTableName) {
        const bool blocked = wagoTableName->blockSignals(true);
        wagoTableName->clear();
        wagoTableName->addItems(names);
        if (keepOk) {
            const int i = wagoTableName->findText(resolved, Qt::MatchFixedString);
            if (i >= 0) wagoTableName->setCurrentIndex(i);
        } else {
            wagoTableName->setCurrentIndex(-1);
            wagoTableName->setEditText(QString());
        }
        wagoTableName->blockSignals(blocked);
    }

    if (wagoTableList) {
        const bool blocked = wagoTableList->blockSignals(true);
        wagoTableList->clear();
        wagoTableList->addItems(names);
        if (keepOk) {
            const auto found = wagoTableList->findItems(resolved, Qt::MatchFixedString);
            if (!found.isEmpty()) wagoTableList->setCurrentItem(found.first());
            else wagoTableList->setCurrentRow(-1);
        } else {
            wagoTableList->setCurrentRow(-1);
        }
        wagoTableList->blockSignals(blocked);
        filterWagoTableList();
    }

    if (keepOk) applyWagoTableSelection(resolved);
}

void MainWindow::filterWagoTableList() {
    if (!wagoTableList) return;
    const QString f = wagoTableFilter ? wagoTableFilter->text().trimmed() : QString();
    const QString n = WagoService::normalizeKey(f);
    for (int i = 0; i < wagoTableList->count(); ++i) {
        QListWidgetItem *it = wagoTableList->item(i);
        if (!it) continue;
        const QString t = it->text();
        const bool vis = f.isEmpty()
            || t.contains(f, Qt::CaseInsensitive)
            || (!n.isEmpty() && WagoService::normalizeKey(t).contains(n));
        it->setHidden(!vis);
    }
}

void MainWindow::applyWagoTableSelection(const QString &name) {
    QString table = name.trimmed();
    QString urlTable, urlBuild, urlLoc;
    if (WagoService::parsePageUrl(table, &urlTable, &urlBuild, &urlLoc) && !urlTable.isEmpty()) {
        table = urlTable;
        if (!urlBuild.isEmpty() && wagoBuild) {
            const int bi = wagoBuild->findText(urlBuild, Qt::MatchFixedString);
            if (bi >= 0) wagoBuild->setCurrentIndex(bi); else wagoBuild->setEditText(urlBuild);
        }
        if (!urlLoc.isEmpty() && wagoLocale) {
            const int li = wagoLocale->findText(urlLoc, Qt::MatchFixedString);
            if (li >= 0) wagoLocale->setCurrentIndex(li); else wagoLocale->setEditText(urlLoc);
        }
    }
    table = WagoService::resolveTableName(table, m_wago.knownTables());

    if (wagoTableName) {
        const bool blocked = wagoTableName->blockSignals(true);
        const int i = wagoTableName->findText(table, Qt::MatchFixedString);
        if (i >= 0) wagoTableName->setCurrentIndex(i);
        else if (!table.isEmpty()) wagoTableName->setEditText(table);
        wagoTableName->blockSignals(blocked);
    }

    if (wagoLocalTable) {
        QStringList local;
        for (int i = 0; i < wagoLocalTable->count(); ++i) local << wagoLocalTable->itemText(i);
        const QString sug = suggestLocalTable(table, local);
        if (!sug.isEmpty()) {
            const int i = wagoLocalTable->findText(sug, Qt::MatchFixedString);
            if (i >= 0) wagoLocalTable->setCurrentIndex(i);
            else wagoLocalTable->setEditText(sug);
        }
    }
    updateWagoNameMatch();
}

void MainWindow::updateWagoNameMatch() {
    if (!wagoNameHint) return;
    QString typed = wagoTableName ? wagoTableName->currentText().trimmed() : QString();
    QString urlTable, urlBuild, urlLoc;
    if (WagoService::parsePageUrl(typed, &urlTable, &urlBuild, &urlLoc) && !urlTable.isEmpty())
        typed = urlTable;

    const QStringList wagoKnown = m_wago.knownTables();
    QStringList localKnown;
    if (wagoLocalTable) {
        for (int i = 0; i < wagoLocalTable->count(); ++i) localKnown << wagoLocalTable->itemText(i);
    }
    const QString localTyped = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();

    auto closeNames = [](const QString &want, const QStringList &known) -> QStringList {
        const QString n = WagoService::normalizeKey(want);
        if (n.isEmpty()) return {};
        QStringList exact, partial;
        for (const auto &k : known) {
            const QString kn = WagoService::normalizeKey(k);
            if (kn == n) exact << k;
            else if (kn.contains(n) || n.contains(kn)) partial << k;
        }
        if (!exact.isEmpty()) return exact;
        return partial.mid(0, 5);
    };

    QString wagoLine, localLine;
    if (typed.isEmpty()) {
        wagoLine = wagoKnown.isEmpty()
            ? QStringLiteral("Wago.tools: загружается список таблиц патча (как Select Table на сайте).")
            : QStringLiteral("Wago.tools: выберите таблицу в списке (%1 шт.).").arg(wagoKnown.size());
    } else if (wagoKnown.isEmpty()) {
        wagoLine = QStringLiteral("Wago.tools: список таблиц патча ещё не загружен. Имя «%1» пока не проверено.")
                       .arg(typed);
    } else {
        const QString resolved = WagoService::resolveTableName(typed, wagoKnown);
        bool hit = false;
        for (const auto &k : wagoKnown) {
            if (k.compare(resolved, Qt::CaseInsensitive) == 0) { hit = true; break; }
        }
        if (hit) {
            wagoLine = QStringLiteral("Wago.tools: выбрано «%1» — таблица есть в этом патче. Нажмите «Анализировать колонки», чтобы сверить строки с hotfixes.").arg(resolved);
        } else {
            const QStringList alt = closeNames(typed, wagoKnown);
            wagoLine = alt.isEmpty()
                ? QStringLiteral("Wago.tools: «%1» нет в этом патче.").arg(typed)
                : QStringLiteral("Wago.tools: «%1» нет. Похоже: %2.").arg(typed, alt.join(QStringLiteral(", ")));
        }
    }

    const QString localWant = !localTyped.isEmpty() ? localTyped : suggestLocalTable(typed, localKnown);
    if (localKnown.isEmpty()) {
        localLine = QStringLiteral("База hotfixes: таблицы не прочитаны — подключитесь к MySQL.");
    } else if (localWant.isEmpty()) {
        localLine = QStringLiteral("База hotfixes: укажите имя таблицы.");
    } else {
        bool hit = false;
        const QString wantKey = WagoService::normalizeKey(localWant);
        for (const auto &k : localKnown) {
            if (k.compare(localWant, Qt::CaseInsensitive) == 0 || WagoService::normalizeKey(k) == wantKey) {
                hit = true; break;
            }
        }
        if (hit) {
            localLine = QStringLiteral("Ваша база `%1`: `%2` — таблица есть.")
                            .arg(hotfixSchema(), localWant);
        } else {
            const QStringList alt = closeNames(localWant, localKnown);
            localLine = alt.isEmpty()
                ? QStringLiteral("Ваша база `%1`: таблицы `%2` нет.").arg(hotfixSchema(), localWant)
                : QStringLiteral("Ваша база `%1`: `%2` нет. Похоже: %3.")
                      .arg(hotfixSchema(), localWant, alt.join(QStringLiteral(", ")));
        }
    }

    wagoNameHint->setText(wagoLine + QStringLiteral("\n") + localLine);
}

void MainWindow::fetchWagoBuilds() {
    wagoStatus->setText("Загрузка списка патчей с wago.tools…");
    m_wago.fetchBuilds();
}

void MainWindow::analyzeWagoTable() {
    if (!m_db.isOpen()) { QMessageBox::warning(this, "Wago", "Сначала подключитесь к MySQL — нужны таблицы базы hotfixes."); return; }
    const QString schema = hotfixSchema();
    QString build = wagoBuild->currentText().trimmed();
    QString wagoTable = wagoTableName->currentText().trimmed();
    QString locale = wagoLocale ? wagoLocale->currentText().trimmed() : QStringLiteral("ruRU");
    QString urlTable, urlBuild, urlLocale;
    if (WagoService::parsePageUrl(wagoTable, &urlTable, &urlBuild, &urlLocale)) {
        wagoTable = urlTable;
        if (!urlBuild.isEmpty()) build = urlBuild;
        if (!urlLocale.isEmpty()) locale = urlLocale;
    }
    build = WagoService::resolveBuild(build, m_wago.knownBuilds());
    wagoTable = WagoService::resolveTableName(wagoTable, m_wago.knownTables());
    if (wagoBuild) wagoBuild->setEditText(build);
    if (wagoTableName) wagoTableName->setEditText(wagoTable);
    if (wagoLocale && !locale.isEmpty()) {
        const int li = wagoLocale->findText(locale, Qt::MatchFixedString);
        if (li >= 0) wagoLocale->setCurrentIndex(li); else wagoLocale->setEditText(locale);
    }

    QStringList localNames;
    for (int i = 0; i < wagoLocalTable->count(); ++i) localNames << wagoLocalTable->itemText(i);
    QString localTable = wagoLocalTable->currentText().trimmed();
    const QString suggested = suggestLocalTable(wagoTable, localNames);
    if (localTable.isEmpty() || normalizeName(localTable) != normalizeName(wagoTable)) {
        const int li = wagoLocalTable->findText(suggested, Qt::MatchFixedString);
        if (li >= 0) { wagoLocalTable->setCurrentIndex(li); localTable = wagoLocalTable->currentText().trimmed(); }
        else if (!suggested.isEmpty()) {
            wagoLocalTable->setEditText(suggested);
            localTable = suggested;
        }
    }

    if (build.isEmpty() || wagoTable.isEmpty()) {
        QMessageBox::information(this, "Wago",
            "Укажите патч (например 7.3.5.26365) и таблицу (Achievement или achievement). Можно вставить URL страницы wago.tools.");
        return;
    }
    if (!safeIdent(schema)) { QMessageBox::information(this, "Wago", "Укажите базу hotfixes (не world)."); return; }
    if (schema.compare("world", Qt::CaseInsensitive) == 0 || schema.compare("auth", Qt::CaseInsensitive) == 0
        || schema.compare("characters", Qt::CaseInsensitive) == 0) {
        if (QMessageBox::warning(this, "Wago",
                QString("Выбрана база «%1». DB2 с wago.tools должен сопоставляться с hotfixes, не с world/auth/characters. Продолжить всё равно?").arg(schema),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }
    if (localTable.isEmpty() || !safeIdent(localTable)) { QMessageBox::information(this, "Wago", "Выберите таблицу в hotfixes (только буквы, цифры и _)."); return; }
    if (!safeIdent(wagoTable)) { QMessageBox::warning(this, "Wago", "Недопустимое имя таблицы Wago."); return; }

    if (normalizeName(wagoTable) != normalizeName(localTable)) {
        QString extra;
        if (!suggested.isEmpty() && suggested.compare(localTable, Qt::CaseInsensitive) != 0)
            extra = QString("\nПодсказка hotfixes: «%1» → «%2».").arg(wagoTable, suggested);
        const auto reply = QMessageBox::question(this, "Имена таблиц отличаются",
            QString("Wago DB2: «%1»\nhotfixes.`%2`.`%3`\n\nПродолжить анализ колонок с этой таблицей hotfixes?")
                .arg(wagoTable, schema, localTable) + extra,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) { wagoStatus->setText("Анализ отменён: имена таблиц не подтверждены."); return; }
    }

    wagoStatus->setText(QString("Скачиваю все строки %1 @ %2 (%3) и сравниваю с `%4`.`%5`…").arg(wagoTable, build, locale, schema, localTable));
    m_wago.fetchTable(build, wagoTable, 0, locale);
}

void MainWindow::loadLocalColumns() {
    m_localColumns.clear();
    m_localTypes.clear();
    const QString schema = hotfixSchema();
    const QString table = wagoLocalTable->currentText().trimmed();
    if (!m_db.isOpen() || !safeIdent(table) || !safeIdent(schema)) return;
    QSqlQuery q(m_db.database());
    q.prepare("SELECT COLUMN_NAME, DATA_TYPE FROM information_schema.columns WHERE table_schema = ? AND table_name = ? ORDER BY ORDINAL_POSITION");
    q.addBindValue(schema);
    q.addBindValue(table);
    if (!q.exec()) { addLog("Wago: не удалось прочитать колонки hotfixes: " + q.lastError().text()); return; }
    while (q.next()) {
        const QString name = q.value(0).toString();
        m_localColumns << name;
        m_localTypes.insert(name, q.value(1).toString());
    }
    if (m_localColumns.isEmpty())
        addLog("Wago: в `" + schema + "`.`" + table + "` колонок нет — это не world, нужна таблица hotfixes.");
}

void MainWindow::buildMapping() {
    const auto wagoCols = m_wago.columns();
    const auto rows = m_wago.rows();
    wagoMapping->setRowCount(0);
    int unmatched = 0, matched = 0;
    for (int i = 0; i < wagoCols.size(); ++i) {
        const int r = wagoMapping->rowCount();
        wagoMapping->insertRow(r);
        auto *nameItem = new QTableWidgetItem(wagoCols[i]);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        wagoMapping->setItem(r, 0, nameItem);
        QString sample;
        if (!rows.isEmpty() && i < rows.first().size()) sample = rows.first().at(i);
        auto *sampleItem = new QTableWidgetItem(sample.left(80));
        sampleItem->setFlags(sampleItem->flags() & ~Qt::ItemIsEditable);
        wagoMapping->setItem(r, 1, sampleItem);

        auto *cb = new QComboBox;
        cb->addItem(QString::fromUtf8("— не импортировать —"), QString());
        for (const auto &c : m_localColumns)
            cb->addItem(c + "  (" + m_localTypes.value(c) + ")", c);
        const QString sug = suggestLocalCol(wagoCols[i], m_localColumns);
        QString status = "выберите вручную";
        if (!sug.isEmpty()) {
            const int idx = cb->findData(sug);
            if (idx >= 0) { cb->setCurrentIndex(idx); status = "совпало"; ++matched; }
            else ++unmatched;
        } else ++unmatched;
        wagoMapping->setCellWidget(r, 2, cb);

        auto *typeItem = new QTableWidgetItem(sug.isEmpty() ? QString() : m_localTypes.value(sug));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        wagoMapping->setItem(r, 3, typeItem);
        auto *st = new QTableWidgetItem(status);
        st->setFlags(st->flags() & ~Qt::ItemIsEditable);
        wagoMapping->setItem(r, 4, st);

        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, r, cb](int) {
            const QString local = cb->currentData().toString();
            if (wagoMapping->item(r, 3)) wagoMapping->item(r, 3)->setText(m_localTypes.value(local));
            if (wagoMapping->item(r, 4)) wagoMapping->item(r, 4)->setText(local.isEmpty() ? QString::fromUtf8("пропуск") : QString::fromUtf8("выбрано вами"));
        });
    }
    wagoMapping->resizeColumnsToContents();
    const QString schema = hotfixSchema();
    const QString localName = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();
    qint64 localRows = -1;
    if (m_db.isOpen() && safeIdent(schema) && safeIdent(localName)) {
        QSqlQuery cq(m_db.database());
        if (cq.exec(QStringLiteral("SELECT COUNT(*) FROM `%1`.`%2`").arg(schema, localName)) && cq.next())
            localRows = cq.value(0).toLongLong();
    }
    const int wagoTotal = m_wago.totalRowCount();
    const int wagoGot = m_wago.rows().size();
    QString localPart;
    if (localRows >= 0)
        localPart = QString::fromUtf8("hotfixes.`%1`.`%2`: %3 строк.")
                        .arg(schema, localName).arg(localRows);
    else
        localPart = QString::fromUtf8("hotfixes.`%1`.`%2`: таблицы нет (0 строк).")
                        .arg(schema, localName.isEmpty() ? QStringLiteral("?") : localName);
    wagoStatus->setText(QString::fromUtf8(
        "Анализ: Wago «%1» — %2 колонок, %3 строк на сайте, скачано %4. %5 Совпало колонок: %6, требуют выбора: %7. Импорт только после кнопки.")
                            .arg(wagoTableName ? wagoTableName->currentText().trimmed() : QString())
                            .arg(wagoCols.size())
                            .arg(wagoTotal)
                            .arg(wagoGot)
                            .arg(localPart)
                            .arg(matched).arg(unmatched));
}

void MainWindow::showWagoError(const QString &title, const QString &body) {
    QString t = body.trimmed();
    if (t.isEmpty())
        t = QStringLiteral("Ошибка без текста от MySQL/Wago.\nЧасто: таблицы нет (код 1146), нет прав CREATE/INSERT, база hotfixes отсутствует.\nСмотрите журнал внизу окна.");
    if (wagoStatus) wagoStatus->setText(t.left(400).replace(QLatin1Char('\n'), QLatin1Char(' ')));
    addLog(QStringLiteral("Wago: ") + t.left(800));
    persistErrorLog(QStringLiteral("wago"), title.isEmpty() ? QStringLiteral("Wago") : title, t);
    QDialog dlg(this);
    dlg.setWindowTitle(title.isEmpty() ? QStringLiteral("Wago") : title);
    QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect ag = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 720);
    dlg.resize(qBound(420, ag.width() * 3 / 5, 680), qBound(280, ag.height() * 2 / 5, 480));
    dlg.setMaximumSize(qMax(400, ag.width() - 48), qMax(280, ag.height() - 48));
    auto *vl = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit;
    te->setReadOnly(true);
    te->setPlainText(t);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    vl->addWidget(te);
    vl->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    dlg.exec();
}

bool MainWindow::ensureHotfixSchema(QString *error) {
    const QString schema = hotfixSchema();
    if (!safeIdent(schema)) {
        if (error) *error = QStringLiteral("Имя базы hotfixes недопустимо.");
        return false;
    }
    QSqlQuery q(m_db.database());
    const QString sql = QStringLiteral("CREATE DATABASE IF NOT EXISTS `%1` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci").arg(schema);
    if (!q.exec(sql)) {
        if (error) *error = formatSqlError(q.lastError(), sql);
        return false;
    }
    return true;
}

bool MainWindow::hotfixTableExists(const QString &table) const {
    const QString schema = hotfixSchema();
    if (!m_db.isOpen() || !safeIdent(schema) || !safeIdent(table)) return false;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = ? AND table_name = ? AND table_type = 'BASE TABLE'"));
    q.addBindValue(schema);
    q.addBindValue(table);
    if (!q.exec() || !q.next()) return false;
    return q.value(0).toInt() > 0;
}

bool MainWindow::createHotfixTableFromWago(QString *error) {
    if (!m_db.isOpen()) {
        if (error) *error = QStringLiteral("Сначала подключитесь к MySQL.");
        return false;
    }
    if (m_wago.columns().isEmpty()) {
        if (error) *error = QStringLiteral("Сначала нажмите «Анализировать колонки» — нужны колонки с wago.tools.");
        return false;
    }
    if (!ensureHotfixSchema(error)) return false;
    const QString schema = hotfixSchema();
    QString table = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();
    if (table.isEmpty() || !safeIdent(table))
        table = sqlIdent(suggestLocalTable(wagoTableName ? wagoTableName->currentText() : QString()));
    if (!safeIdent(table)) {
        if (error) *error = QStringLiteral("Недопустимое имя таблицы для MySQL.");
        return false;
    }
    if (hotfixTableExists(table)) {
        if (wagoStatus)
            wagoStatus->setText(QString::fromUtf8("Таблица `%1`.`%2` уже есть — можно импортировать.").arg(schema, table));
        addLog(QString::fromUtf8("Wago: таблица `%1`.`%2` уже существует.").arg(schema, table));
        QMessageBox::information(this, QStringLiteral("Таблица есть"),
            QString::fromUtf8("`%1`.`%2` уже существует. Используйте «Импортировать по сопоставлению».").arg(schema, table));
        return true;
    }

    const auto cols = m_wago.columns();
    const auto rows = m_wago.rows();
    QStringList defs;
    QString pk;
    for (int i = 0; i < cols.size(); ++i) {
        const QString col = sqlIdent(cols.at(i));
        if (col.isEmpty()) continue;
        bool allInt = true, allFloat = true;
        int maxLen = 0;
        const int sample = qMin(rows.size(), 80);
        for (int r = 0; r < sample; ++r) {
            const QString v = (i < rows.at(r).size()) ? rows.at(r).at(i).trimmed() : QString();
            if (v.size() > maxLen) maxLen = v.size();
            if (v.isEmpty()) continue;
            bool ok = false;
            v.toLongLong(&ok);
            if (!ok) allInt = false;
            v.toDouble(&ok);
            if (!ok) allFloat = false;
        }
        QString typ = QStringLiteral("TEXT");
        if (allInt) typ = QStringLiteral("BIGINT");
        else if (allFloat) typ = QStringLiteral("DOUBLE");
        else if (maxLen <= 512) typ = QStringLiteral("VARCHAR(%1)").arg(qMax(64, qMin(512, maxLen + 32)));
        QString def = QStringLiteral("`%1` %2").arg(col, typ);
        if (col.compare(QLatin1String("ID"), Qt::CaseInsensitive) == 0
            || col.compare(QLatin1String("id")) == 0) {
            def += QStringLiteral(" NOT NULL");
            if (pk.isEmpty()) pk = col;
        }
        defs << def;
    }
    if (defs.isEmpty()) {
        if (error) *error = QStringLiteral("У Wago нет ни одной колонки с допустимым именем.");
        return false;
    }
    if (!pk.isEmpty()) defs << QStringLiteral("PRIMARY KEY (`%1`)").arg(pk);
    const QString sql = QStringLiteral(
        "CREATE TABLE `%1`.`%2` (\n  %3\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci")
                            .arg(schema, table, defs.join(QStringLiteral(",\n  ")));
    QSqlQuery q(m_db.database());
    if (!q.exec(sql)) {
        if (error) *error = formatSqlError(q.lastError(), sql);
        return false;
    }
    if (wagoLocalTable) {
        if (wagoLocalTable->findText(table, Qt::MatchFixedString) < 0)
            wagoLocalTable->addItem(table);
        wagoLocalTable->setEditText(table);
    }
    refreshWagoHotfixTables();
    addLog(QString::fromUtf8("Wago: создана таблица `%1`.`%2` (%3 колонок).").arg(schema, table).arg(defs.size() - (pk.isEmpty() ? 0 : 1)));
    if (wagoStatus)
        wagoStatus->setText(QString::fromUtf8("Создана `%1`.`%2`. Можно импортировать строки.").arg(schema, table));
    QMessageBox::information(this, QStringLiteral("Таблица создана"),
        QString::fromUtf8("Создана таблица `%1`.`%2` по колонкам Wago.\nДальше: «Импортировать по сопоставлению».").arg(schema, table));
    return true;
}

void MainWindow::importWagoMapped() {
    if (!m_db.isOpen()) { showWagoError(QStringLiteral("Wago"), QStringLiteral("Сначала подключитесь к MySQL.")); return; }
    if (m_wago.columns().isEmpty()) {
        showWagoError(QStringLiteral("Wago"), QStringLiteral("Сначала «Анализировать колонки» — нужно сопоставление полей."));
        return;
    }
    const QString wagoTable = wagoTableName ? wagoTableName->currentText().trimmed() : QString();
    const QString build = wagoBuild ? wagoBuild->currentText().trimmed() : QString();
    const QString locale = wagoLocale ? wagoLocale->currentText().trimmed() : QStringLiteral("ruRU");
    QString localTable = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();
    if (localTable.isEmpty() || !safeIdent(localTable))
        localTable = sqlIdent(suggestLocalTable(wagoTable));
    if (!hotfixTableExists(localTable)) {
        const auto reply = QMessageBox::question(this, QStringLiteral("Таблицы нет"),
            QString::fromUtf8("В `%1` нет таблицы `%2`.\nСоздать её по колонкам Wago и импортировать все строки?")
                .arg(hotfixSchema(), localTable),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes) return;
        QString err;
        if (!createHotfixTableFromWago(&err)) { showWagoError(QStringLiteral("Создание таблицы"), err); return; }
        loadLocalColumns();
        buildMapping();
    }
    const int have = m_wago.rows().size();
    const int total = m_wago.totalRowCount();
    if (total <= 0 || have < total) {
        m_wagoImportPending = true;
        if (wagoStatus)
            wagoStatus->setText(QString::fromUtf8("Скачиваю все %1 строк «%2» с wago.tools (предпросмотр был %3)…")
                                    .arg(total).arg(wagoTable).arg(have));
        addLog(QString::fromUtf8("Wago: полная загрузка %1 строк для импорта.").arg(total));
        m_wago.fetchTable(build, wagoTable, 0, locale);
        return;
    }
    performWagoImport();
}

void MainWindow::performWagoImport() {
    if (!m_db.isOpen()) { showWagoError(QStringLiteral("Wago"), QStringLiteral("Сначала подключитесь к MySQL.")); return; }
    const QString schema = hotfixSchema();
    QString localTable = wagoLocalTable ? wagoLocalTable->currentText().trimmed() : QString();
    const QString wagoTable = wagoTableName ? wagoTableName->currentText().trimmed() : QString();
    if (!safeIdent(schema)) {
        showWagoError(QStringLiteral("Wago"), QStringLiteral("База hotfixes не определена."));
        return;
    }
    if (localTable.isEmpty() || !safeIdent(localTable))
        localTable = sqlIdent(suggestLocalTable(wagoTable));
    if (m_wago.columns().isEmpty() || m_wago.rows().isEmpty()) {
        showWagoError(QStringLiteral("Wago"), QStringLiteral("Нет строк Wago. Сначала «Анализировать колонки»."));
        return;
    }
    if (wagoMapping && wagoMapping->rowCount() == 0) {
        loadLocalColumns();
        buildMapping();
    }

    const QStringList wagoCols = m_wago.columns();
    QStringList destCols;
    QList<int> srcIdx;
    QStringList skipped;
    for (int r = 0; r < (wagoMapping ? wagoMapping->rowCount() : 0); ++r) {
        const QString wagoCol = wagoMapping->item(r, 0) ? wagoMapping->item(r, 0)->text() : QString();
        auto *cb = qobject_cast<QComboBox *>(wagoMapping->cellWidget(r, 2));
        QString local = cb ? cb->currentData().toString() : QString();
        if (local.isEmpty()) {
            local = suggestLocalCol(wagoCol, m_localColumns);
            if (local.isEmpty() && m_localColumns.contains(sqlIdent(wagoCol), Qt::CaseInsensitive))
                local = sqlIdent(wagoCol);
        }
        if (local.isEmpty() || !safeIdent(local)) { skipped << wagoCol; continue; }
        if (destCols.contains(local, Qt::CaseInsensitive)) { skipped << wagoCol; continue; }
        int src = wagoCols.indexOf(wagoCol);
        if (src < 0) {
            for (int i = 0; i < wagoCols.size(); ++i)
                if (wagoCols.at(i).compare(wagoCol, Qt::CaseInsensitive) == 0) { src = i; break; }
        }
        if (src < 0) src = r;
        destCols << local;
        srcIdx << src;
    }
    if (destCols.isEmpty()) {
        showWagoError(QStringLiteral("Wago"), QStringLiteral("Не выбрано ни одной колонки для импорта."));
        return;
    }

    auto db = m_db.database();
    if (!db.isOpen()) {
        showWagoError(QStringLiteral("Wago / INSERT"), QStringLiteral("Нет открытого MySQL.\n") + mysqlSessionDiag(db));
        return;
    }

    QString pk;
    {
        QSqlQuery kq(db);
        kq.prepare(QStringLiteral(
            "SELECT column_name FROM information_schema.statistics "
            "WHERE table_schema = ? AND table_name = ? AND index_name = 'PRIMARY' "
            "ORDER BY seq_in_index"));
        kq.addBindValue(schema);
        kq.addBindValue(localTable);
        if (kq.exec()) {
            if (kq.next()) pk = kq.value(0).toString();
            if (kq.next())
                addLog(QStringLiteral("Wago: составной PRIMARY KEY — сверка дубликатов по первой колонке ") + pk);
        }
    }
    int pkDest = -1;
    if (!pk.isEmpty()) {
        for (int i = 0; i < destCols.size(); ++i)
            if (destCols.at(i).compare(pk, Qt::CaseInsensitive) == 0) { pkDest = i; break; }
    }
    if (pkDest < 0) {
        for (int i = 0; i < destCols.size(); ++i) {
            const QString n = destCols.at(i);
            if (n.compare(QLatin1String("ID"), Qt::CaseInsensitive) == 0
                || n.compare(QLatin1String("id"), Qt::CaseInsensitive) == 0) {
                pk = n; pkDest = i; break;
            }
        }
    }
    if (pkDest < 0) {
        showWagoError(QStringLiteral("Wago"),
            QStringLiteral("Нет колонки-ключа (ID / PRIMARY KEY) в сопоставлении.\n"
                           "Без неё нельзя отличить новую строку от дубликата. Отметьте ID в таблице сопоставления."));
        return;
    }
    pk = destCols.at(pkDest);

    const auto &wagoRows = m_wago.rows();
    const int total = wagoRows.size();
    if (wagoStatus)
        wagoStatus->setText(QString::fromUtf8("Сверяю %1 строк Wago с `%2`.`%3` по `%4`…")
                                .arg(total).arg(schema, localTable, pk));
    QCoreApplication::processEvents();

    QSet<QString> seenKeys;
    for (int i = 0; i < total; ++i) {
        const auto &row = wagoRows.at(i);
        const int s = srcIdx.at(pkDest);
        const QString key = (s < row.size() ? row.at(s) : QString()).trimmed();
        if (!key.isEmpty()) seenKeys.insert(key);
    }

    QHash<QString, QStringList> existing;
    const QList<QString> keyList = seenKeys.values();
    const int chunk = 400;
    for (int off = 0; off < keyList.size(); off += chunk) {
        QStringList ins;
        const int n = qMin(chunk, keyList.size() - off);
        for (int i = 0; i < n; ++i) ins << sqlLiteral(db, keyList.at(off + i));
        const QString sql = QStringLiteral("SELECT `%1`, `%2` FROM `%3`.`%4` WHERE `%1` IN (%5)")
                                .arg(pk, destCols.join(QStringLiteral("`,`")), schema, localTable, ins.join(QLatin1Char(',')));
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            showWagoError(QStringLiteral("Wago / SELECT"), formatSqlError(q.lastError(), sql, mysqlSessionDiag(db)));
            return;
        }
        while (q.next()) {
            QStringList vals;
            vals.reserve(destCols.size());
            for (int c = 0; c < destCols.size(); ++c)
                vals << q.value(c + 1).toString();
            existing.insert(q.value(0).toString().trimmed(), vals);
        }
        if ((off / chunk) % 3 == 0) QCoreApplication::processEvents();
    }

    QList<int> toInsert, toUpdate;
    QStringList conflictLines;
    int identical = 0, emptyKey = 0;
    for (int i = 0; i < total; ++i) {
        const auto &row = wagoRows.at(i);
        QStringList neu;
        neu.reserve(destCols.size());
        for (int b = 0; b < srcIdx.size(); ++b) {
            const int s = srcIdx.at(b);
            neu << (s < row.size() ? row.at(s) : QString());
        }
        const QString key = neu.at(pkDest).trimmed();
        if (key.isEmpty()) { ++emptyKey; toInsert << i; continue; }
        if (!existing.contains(key)) { toInsert << i; continue; }
        const QStringList &oldv = existing.value(key);
        QStringList diffs;
        const int cmp = qMin(oldv.size(), neu.size());
        for (int c = 0; c < cmp; ++c) {
            if (c == pkDest) continue;
            if (!sqlValueEqual(oldv.at(c), neu.at(c)))
                diffs << destCols.at(c) + QStringLiteral(": «") + oldv.at(c).left(40)
                      + QStringLiteral("» → «") + neu.at(c).left(40) + QStringLiteral("»");
        }
        if (diffs.isEmpty()) { ++identical; continue; }
        toUpdate << i;
        if (conflictLines.size() < 25)
            conflictLines << QStringLiteral("ID %1 — %2").arg(key, diffs.mid(0, 4).join(QStringLiteral("; ")));
    }

    QString summary = QString::fromUtf8(
        "Таблица Wago «%1» → `%2`.`%3`\nКлюч: `%4`\n\n"
        "Всего строк Wago: %5\nНовых (INSERT): %6\nПолностью совпали (пропуск): %7\n"
        "Тот же ID, другие значения: %8")
                          .arg(wagoTable, schema, localTable, pk)
                          .arg(total).arg(toInsert.size()).arg(identical).arg(toUpdate.size());
    if (emptyKey)
        summary += QString::fromUtf8("\nБез ID: %1 (вставлю как новые)").arg(emptyKey);
    if (!skipped.isEmpty())
        summary += QString::fromUtf8("\nКолонки Wago не импортируются: %1").arg(skipped.mid(0, 12).join(QStringLiteral(", ")));

    bool replaceConflicts = false;
    if (toUpdate.isEmpty()) {
        if (toInsert.isEmpty()) {
            askScrollDialog(this, QStringLiteral("Импорт Wago"),
                            QStringLiteral("Все строки уже есть и совпадают — писать нечего."),
                            summary, {QStringLiteral("OK")});
            return;
        }
        const int ans = askScrollDialog(this, QStringLiteral("Импорт Wago"),
            QString::fromUtf8("Конфликтов нет. Вставить %1 новых строк?").arg(toInsert.size()),
            summary, {QStringLiteral("Вставить"), QStringLiteral("Отмена")});
        if (ans != 0) return;
    } else {
        QString details = summary;
        details += QString::fromUtf8("\n\nПримеры конфликтов:\n") + conflictLines.join(QLatin1Char('\n'));
        if (toUpdate.size() > conflictLines.size())
            details += QString::fromUtf8("\n… и ещё %1").arg(toUpdate.size() - conflictLines.size());
        const int ans = askScrollDialog(this, QStringLiteral("Конфликты при импорте Wago"),
            QString::fromUtf8("Тот же ID, другие значения: %1. Заменить данные Wago или оставить мои (только INSERT новых)?")
                .arg(toUpdate.size()),
            details,
            {QStringLiteral("Заменить все конфликты"),
             QStringLiteral("Оставить мои (только новые)"),
             QStringLiteral("Отмена")});
        if (ans == 0) replaceConflicts = true;
        else if (ans == 1) replaceConflicts = false;
        else return;
        if (!replaceConflicts) toUpdate.clear();
    }

    auto sqlLit = [&](const QString &v) { return sqlLiteral(db, v); };

    QSqlQuery q(db);
    const bool inTx = q.exec(QStringLiteral("START TRANSACTION"));
    if (!inTx)
        addLog(QStringLiteral("Wago: без транзакции. ") + q.lastError().text());

    int inserted = 0, updated = 0;
    const QString colList = destCols.join(QStringLiteral("`,`"));
    const int batchSize = 25;
    for (int off = 0; off < toInsert.size(); off += batchSize) {
        const int n = qMin(batchSize, toInsert.size() - off);
        QStringList tuples;
        for (int i = 0; i < n; ++i) {
            const auto &row = wagoRows.at(toInsert.at(off + i));
            QStringList cells;
            for (int b = 0; b < srcIdx.size(); ++b) {
                const int s = srcIdx.at(b);
                cells << sqlLit(s < row.size() ? row.at(s) : QString());
            }
            tuples << QLatin1Char('(') + cells.join(QLatin1Char(',')) + QLatin1Char(')');
        }
        const QString sql = QStringLiteral("INSERT INTO `%1`.`%2` (`%3`) VALUES %4")
                                .arg(schema, localTable, colList, tuples.join(QLatin1Char(',')));
        if (!q.exec(sql)) {
            const QSqlError err = q.lastError();
            if (err.isValid() || q.numRowsAffected() < 0) {
                if (inTx) q.exec(QStringLiteral("ROLLBACK"));
                showWagoError(QStringLiteral("Wago / INSERT"),
                    formatSqlError(err, sql.left(1500), mysqlSessionDiag(db)
                        + QString::fromUtf8("\nуже INSERT: %1").arg(inserted)));
                return;
            }
        }
        const int affected = q.numRowsAffected();
        inserted += affected > 0 ? affected : n;
        if ((off / batchSize) % 4 == 0) QCoreApplication::processEvents();
    }

    if (replaceConflicts) {
        for (int idx : toUpdate) {
            const auto &row = wagoRows.at(idx);
            QStringList sets;
            QString keyLit;
            for (int b = 0; b < srcIdx.size(); ++b) {
                const int s = srcIdx.at(b);
                const QString v = s < row.size() ? row.at(s) : QString();
                if (b == pkDest) { keyLit = sqlLit(v); continue; }
                sets << QStringLiteral("`%1` = %2").arg(destCols.at(b), sqlLit(v));
            }
            if (sets.isEmpty() || keyLit.isEmpty()) continue;
            const QString sql = QStringLiteral("UPDATE `%1`.`%2` SET %3 WHERE `%4` = %5")
                                    .arg(schema, localTable, sets.join(QStringLiteral(", ")), pk, keyLit);
            if (!q.exec(sql)) {
                const QSqlError err = q.lastError();
                if (err.isValid()) {
                    if (inTx) q.exec(QStringLiteral("ROLLBACK"));
                    showWagoError(QStringLiteral("Wago / UPDATE"),
                        formatSqlError(err, sql.left(1500), mysqlSessionDiag(db)
                            + QString::fromUtf8("\nуже UPDATE: %1").arg(updated)));
                    return;
                }
            }
            ++updated;
            if (updated % 50 == 0) QCoreApplication::processEvents();
        }
    }

    if (inTx && !q.exec(QStringLiteral("COMMIT"))) {
        const QSqlError err = q.lastError();
        if (err.isValid()) {
            q.exec(QStringLiteral("ROLLBACK"));
            showWagoError(QStringLiteral("Wago / COMMIT"), formatSqlError(err, QStringLiteral("COMMIT"), mysqlSessionDiag(db)));
            return;
        }
    }

    const QString done = QString::fromUtf8(
        "Готово: INSERT %1, UPDATE %2, без изменений %3. Таблица `%4`.`%5`.")
                             .arg(inserted).arg(updated).arg(identical).arg(schema, localTable);
    addLog(QStringLiteral("Wago: ") + done);
    if (wagoStatus) wagoStatus->setText(done);
    QMessageBox::information(this, QStringLiteral("Wago"), done);
    refreshWagoHotfixTables();
}


void MainWindow::runCoreDetection() {
    CoreDetection d;
    if (m_db.isOpen()) d = CoreDetectService::fromDatabase(m_db.database());
    const QString folder = detectFolder ? detectFolder->text().trimmed() : QString();
    if (!folder.isEmpty())
        d = CoreDetectService::merge(d, CoreDetectService::fromFolder(folder));
    else if (!m_db.isOpen()) {
        QMessageBox::information(this, "Определение", "Подключитесь к MySQL или укажите папку ядра / клиента.");
        return;
    }
    applyCoreDetection(d);
}

void MainWindow::selectDetectedWagoBuild(const QString &build) {
    QString want = build.trimmed();
    if (want.isEmpty()) return;
    want.replace(QLatin1String("3.3.5a."), QLatin1String("3.3.5."));
    m_detectedWagoBuild = want;
    if (!wagoBuild) return;
    QStringList known;
    for (int i = 0; i < wagoBuild->count(); ++i) known << wagoBuild->itemText(i);
    if (known.isEmpty()) known = m_wago.knownBuilds();
    const QString resolved = WagoService::resolveBuild(want, known);
    m_detectedWagoBuild = resolved;
    const int i = wagoBuild->findText(resolved, Qt::MatchFixedString);
    if (i >= 0) wagoBuild->setCurrentIndex(i);
    else wagoBuild->setEditText(resolved);
    if (wagoStatus && !resolved.isEmpty())
        addLog(QString::fromUtf8("Wago: патч %1 (из ядра / realmlist.gamebuild).").arg(resolved));
    ensureWagoTableList();
}

void MainWindow::applyCoreDetection(const CoreDetection &d) {
    QString text = "Определено: " + d.summary();
    if (!d.evidence.isEmpty()) text += "\n" + d.evidence.mid(0, 8).join("\n");
    if (d.confidence < 20)
        text += "\nМало данных. Укажите папку исходников TrinityCore/CypherCore или папку клиента (где Wow.exe / .build.info).";
    if (detectStatus) detectStatus->setText(text);
    addLog("Автоопределение: " + d.summary());
    for (const auto &e : d.evidence) addLog("  • " + e);
    if (d.profile.isEmpty() && d.clientBuild.isEmpty()) return;

    if (!d.profile.isEmpty() && core) {
        const bool blocked = core->blockSignals(true);
        const int i = core->findText(d.profile);
        if (i >= 0) core->setCurrentIndex(i);
        else core->setCurrentText(d.profile);
        core->blockSignals(blocked);
        const auto repo = CoreRepositoryService::forProfile(d.profile);
        if (coreRepository) coreRepository->setText(repo.gitUrl + " (" + repo.language + ")");
        m_profile.core = d.profile;
    }
    if (!d.branch.isEmpty() && coreBranch) {
        if (coreBranch->findText(d.branch) < 0) coreBranch->addItem(d.branch);
        coreBranch->setCurrentText(d.branch);
    }
    QString patch = d.patch;
    if (!d.clientBuild.isEmpty() && !patch.contains(d.clientBuild))
        patch = patch.isEmpty() ? ("клиент " + d.clientBuild) : (patch + " · клиент " + d.clientBuild);
    if (!d.coreName.isEmpty() && !patch.contains(d.coreName))
        patch = d.coreName + (patch.isEmpty() ? QString() : " · " + patch);
    if (!patch.isEmpty() && corePatch) corePatch->setText(patch);
    fillCommandTree();

    if (!d.clientBuild.isEmpty() || d.gameBuild > 0)
        selectDetectedWagoBuild(d.clientBuild);
}

void MainWindow::chooseDetectFolder() {
    const auto folder = QFileDialog::getExistingDirectory(this, "Папка ядра (TrinityCore/CypherCore) или клиента WoW", detectFolder->text());
    if (folder.isEmpty()) return;
    detectFolder->setText(folder);
    QSettings s("WoWDBStudio","WoWDBStudio");
    s.setValue("detect/folder", folder);
    addLog("Папка для определения: " + folder);
    if (m_db.isOpen()) runCoreDetection();
    else applyCoreDetection(CoreDetectService::fromFolder(folder));
}

void MainWindow::buildServerTab(QTabWidget *tabs) {
    auto *page=new QWidget; auto *root=new QVBoxLayout(page);
    root->setContentsMargins(16,16,16,16); root->setSpacing(14);

    // --- Стили карточек и индикаторов (работают и без QSS-темы) ---
    const QString cardQss = QStringLiteral(
        "QGroupBox{border:1px solid #e2e8f0;border-radius:10px;margin-top:14px;"
        "padding:14px 12px 12px 12px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#2563eb;}");
    const QString dotOn  = QStringLiteral("background:#2ecc71;border-radius:7px;");
    const QString dotOff = QStringLiteral("background:#57606f;border-radius:7px;");

    // === Верхняя панель: карточки статуса Реалм / Ядро ===
    auto *statusRow=new QHBoxLayout; statusRow->setSpacing(14);

    auto *realmCard=new QGroupBox(QStringLiteral("Реалм  (bnetserver / authserver)")); realmCard->setStyleSheet(cardQss);
    serverRealmCard = realmCard;
    auto *realmCardL=new QVBoxLayout(realmCard); realmCardL->setSpacing(10);
    auto *realmHead=new QHBoxLayout; realmHead->setSpacing(8);
    auto *realmDot=new QLabel; realmDot->setFixedSize(14,14); realmDot->setStyleSheet(dotOff);
    realmStateLabel=wrapLabel(QStringLiteral("не запущено")); realmStateLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    realmHead->addWidget(realmDot); realmHead->addWidget(realmStateLabel,1);
    realmCardL->addLayout(realmHead);
    auto *realmBtns=new QHBoxLayout; realmBtns->setSpacing(8);
    auto *startRealm=button(QStringLiteral("Запустить")); auto *restartRealm=button(QStringLiteral("Рестарт")); auto *stopRealm=button(QStringLiteral("Стоп"));
    realmBtns->addWidget(startRealm); realmBtns->addWidget(restartRealm); realmBtns->addWidget(stopRealm);
    realmCardL->addLayout(realmBtns);

    auto *worldCard=new QGroupBox(QStringLiteral("Ядро  (worldserver)")); worldCard->setStyleSheet(cardQss);
    serverWorldCard = worldCard;
    auto *worldCardL=new QVBoxLayout(worldCard); worldCardL->setSpacing(10);
    auto *worldHead=new QHBoxLayout; worldHead->setSpacing(8);
    auto *worldDot=new QLabel; worldDot->setFixedSize(14,14); worldDot->setStyleSheet(dotOff);
    worldStateLabel=wrapLabel(QStringLiteral("не запущено")); worldStateLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    worldHead->addWidget(worldDot); worldHead->addWidget(worldStateLabel,1);
    realmHead->addStretch();
    worldCardL->addLayout(worldHead);
    auto *worldBtns=new QHBoxLayout; worldBtns->setSpacing(8);
    auto *startWorld=button(QStringLiteral("Запустить")); auto *restartWorld=button(QStringLiteral("Рестарт")); auto *stopWorld=button(QStringLiteral("Стоп"));
    worldBtns->addWidget(startWorld); worldBtns->addWidget(restartWorld); worldBtns->addWidget(stopWorld);
    worldCardL->addLayout(worldBtns);

    statusRow->addWidget(realmCard,1); statusRow->addWidget(worldCard,1);
    root->addLayout(statusRow);

    // === Карточка путей и опций ===
    auto *paths=new QGroupBox(QStringLiteral("Пути к процессам и опции")); paths->setStyleSheet(cardQss);
    auto *pf=new QFormLayout(paths); pf->setSpacing(8); pf->setContentsMargins(12,18,12,12);
    worldPath=new QLineEdit; realmPath=new QLineEdit;
    auto *pickWorld=button(QStringLiteral("worldserver.exe")); auto *pickRealm=button(QStringLiteral("bnetserver.exe"));
    pickRealmBtn = pickRealm;
    auto *worldRow=new QWidget; auto *worldLay=new QHBoxLayout(worldRow); worldLay->setContentsMargins(0,0,0,0); worldLay->setSpacing(6);
    worldLay->addWidget(worldPath,1); worldLay->addWidget(pickWorld);
    auto *realmRow=new QWidget; auto *realmLay=new QHBoxLayout(realmRow); realmLay->setContentsMargins(0,0,0,0); realmLay->setSpacing(6);
    realmLay->addWidget(realmPath,1); realmLay->addWidget(pickRealm);
    serverOwnConsole=new QCheckBox(QStringLiteral("Отдельное окно консоли (команды Studio тогда только через RA)"));
    pf->addRow(QStringLiteral("Ядро:"), worldRow);
    pf->addRow(QStringLiteral("Реалм:"), realmRow);
    pf->addRow(serverOwnConsole);
    serverLaunchHint=wrapLabel(QStringLiteral("Сначала реалм, затем ядро. Рабочая папка — каталог с .exe и конфигами. Журнал: stdout процесса и файлы Server.log / Errors.log рядом с exe. Копии Studio — папка logs у WowDbStudio.exe."));
    pf->addRow(serverLaunchHint);
    root->addWidget(paths);

    // === Карточка быстрых действий ===
    auto *quick=new QGroupBox(QStringLiteral("Быстрые действия")); quick->setStyleSheet(cardQss);
    auto *qh=new QHBoxLayout(quick); qh->setSpacing(8); qh->setContentsMargins(12,18,12,12);
    auto *analyzeLogs=button(QStringLiteral("Джарвис: анализ логов")); auto *memTune=button(QStringLiteral("Снизить RAM ядра"));
    qh->addWidget(analyzeLogs); qh->addWidget(memTune); qh->addStretch();
    root->addWidget(quick);

    // === Основная область: каталог команд | консоль ===
    auto *split=new QSplitter(Qt::Horizontal);

    auto *cmdCard=new QGroupBox(QStringLiteral("Каталог команд")); cmdCard->setStyleSheet(cardQss);
    auto *cl=new QVBoxLayout(cmdCard); cl->setSpacing(8); cl->setContentsMargins(12,18,12,12);
    commandFilterHint=wrapLabel(QStringLiteral("Набор команд зависит от профиля ядра на вкладке «Подключение»: общие видны всегда, уникальные — только у своего ядра."));
    cl->addWidget(commandFilterHint);
    commandTree=new QTreeWidget; commandTree->setHeaderLabels({QStringLiteral("Команда"),QStringLiteral("Куда")});
    commandTree->setColumnWidth(0,280); commandTree->setRootIsDecorated(true); commandTree->setAlternatingRowColors(true);
    commandHelp=new QTextEdit; commandHelp->setReadOnly(true); commandHelp->setMaximumHeight(110);
    commandHelp->setPlaceholderText(QStringLiteral("Выберите команду слева — здесь будет инструкция."));
    fillCommandTree();
    cl->addWidget(commandTree,1); cl->addWidget(commandHelp);

    auto *conCard=new QGroupBox(QStringLiteral("Консоль и Remote Access")); conCard->setStyleSheet(cardQss);
    auto *rl=new QVBoxLayout(conCard); rl->setSpacing(8); rl->setContentsMargins(12,18,12,12);
    serverCommand=new QLineEdit; serverCommand->setPlaceholderText(QStringLiteral("Команда консоли, например: server info"));
    auto *sendWorld=button(QStringLiteral("В ядро")); auto *sendRealm=button(QStringLiteral("В реалм")); auto *sendRa=button(QStringLiteral("В RA"));
    auto *cmdRow=new QHBoxLayout; cmdRow->setSpacing(6); cmdRow->addWidget(serverCommand,1); cmdRow->addWidget(sendWorld); cmdRow->addWidget(sendRealm); cmdRow->addWidget(sendRa);
    rl->addLayout(cmdRow);
    auto *raBox=new QGroupBox(QStringLiteral("Remote Access  (worldserver.conf: Ra.Enable = 1)"));
    auto *raf=new QFormLayout(raBox); raf->setSpacing(6);
    raHost=new QLineEdit(QStringLiteral("127.0.0.1")); raPort=new QSpinBox; raPort->setRange(1,65535); raPort->setValue(3443);
    raUser=new QLineEdit; raPassword=new QLineEdit; raPassword->setEchoMode(QLineEdit::Password);
    auto *raConnect=button(QStringLiteral("Подключить RA"));
    raf->addRow(QStringLiteral("Хост:"), raHost); raf->addRow(QStringLiteral("Порт:"), raPort); raf->addRow(QStringLiteral("Пользователь RA:"), raUser); raf->addRow(QStringLiteral("Пароль RA:"), raPassword); raf->addRow(raConnect);
    rl->addWidget(raBox);
    worldLog=new QTextEdit; worldLog->setReadOnly(true); worldLog->setAcceptRichText(false);
    worldLog->setPlaceholderText(QStringLiteral("Журнал worldserver (stdout + Server.log)…"));
    worldLog->setMinimumHeight(160);
    realmLog=new QTextEdit; realmLog->setReadOnly(true); realmLog->setAcceptRichText(false);
    realmLog->setPlaceholderText(QStringLiteral("Журнал bnetserver / authserver (stdout + Server.log)…"));
    realmLog->setMinimumHeight(120);
    auto *logs=new QSplitter(Qt::Vertical);
    auto *wWrap=new QWidget; auto *wl=new QVBoxLayout(wWrap); wl->setContentsMargins(0,0,0,0); wl->addWidget(wrapLabel(QStringLiteral("Консоль ядра"))); wl->addWidget(worldLog);
    auto *rWrap=new QWidget; auto *rll=new QVBoxLayout(rWrap); rll->setContentsMargins(0,0,0,0); rll->addWidget(wrapLabel(QStringLiteral("Консоль реалма"))); rll->addWidget(realmLog);
    logs->addWidget(wWrap); logs->addWidget(rWrap);
    rl->addWidget(logs,1);

    split->addWidget(cmdCard); split->addWidget(conCard);
    split->setStretchFactor(0,2); split->setStretchFactor(1,3);
    root->addWidget(split,1);

    tabs->addTab(page, QStringLiteral("Сервер"));

    auto pickExe=[this](QLineEdit *edit, const QString &title){
        const auto f=QFileDialog::getOpenFileName(this,title,edit->text(),"Executable (*.exe);;All files (*)");
        if(!f.isEmpty()){ edit->setText(f); saveServerSettings(); }
    };
    connect(pickWorld,&QPushButton::clicked,this,[pickExe,this]{ pickExe(worldPath, "worldserver.exe"); });
    connect(pickRealm,&QPushButton::clicked,this,[pickExe,this]{ pickExe(realmPath, "bnetserver.exe / authserver.exe"); });
    connect(startWorld,&QPushButton::clicked,this,[this]{ saveServerSettings(); m_server.setWorldPath(worldPath->text()); m_server.setOwnConsole(serverOwnConsole->isChecked()); m_server.startWorld(); });
    connect(startRealm,&QPushButton::clicked,this,[this]{ saveServerSettings(); m_server.setRealmPath(realmPath->text()); m_server.setOwnConsole(serverOwnConsole->isChecked()); m_server.startRealm(); });
    connect(stopWorld,&QPushButton::clicked,this,[this]{ m_server.stopWorld(); });
    connect(stopRealm,&QPushButton::clicked,this,[this]{ m_server.stopRealm(); });
    connect(restartWorld,&QPushButton::clicked,this,[this]{ saveServerSettings(); m_server.setWorldPath(worldPath->text()); m_server.setOwnConsole(serverOwnConsole->isChecked()); m_server.restartWorld(); });
    connect(restartRealm,&QPushButton::clicked,this,[this]{ saveServerSettings(); m_server.setRealmPath(realmPath->text()); m_server.setOwnConsole(serverOwnConsole->isChecked()); m_server.restartRealm(); });
    connect(sendWorld,&QPushButton::clicked,this,[this]{ m_server.sendWorld(serverCommand->text()); });
    connect(sendRealm,&QPushButton::clicked,this,[this]{ m_server.sendRealm(serverCommand->text()); });
    connect(sendRa,&QPushButton::clicked,this,[this]{ m_server.sendRa(serverCommand->text()); });
    connect(raConnect,&QPushButton::clicked,this,[this]{
        saveServerSettings();
        m_server.configureRa(raHost->text().trimmed(), quint16(raPort->value()), raUser->text(), raPassword->text());
    });
    connect(commandTree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *it,int){
        if(!it || it->data(0,Qt::UserRole).toString().isEmpty()) return;
        serverCommand->setText(it->data(0,Qt::UserRole).toString());
        commandHelp->setPlainText(it->data(0,Qt::UserRole+1).toString());
    });
    connect(commandTree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *it,int){
        if(!it || it->data(0,Qt::UserRole).toString().isEmpty()) return;
        serverCommand->setText(it->data(0,Qt::UserRole).toString());
        commandHelp->setPlainText(it->data(0,Qt::UserRole+1).toString());
        sendPickedCommand();
    });
    connect(serverCommand,&QLineEdit::returnPressed,this,[this]{ sendPickedCommand(); });
    connect(&m_server,&ServerLauncher::logWorld,this,[this](const QString &s){ appendLiveLog(worldLog, s, QStringLiteral("worldserver")); });
    connect(&m_server,&ServerLauncher::logRealm,this,[this](const QString &s){ appendLiveLog(realmLog, s, QStringLiteral("bnetserver")); });
    connect(&m_server,&ServerLauncher::logRa,this,[this](const QString &s){ appendLiveLog(worldLog, QStringLiteral("[RA] ") + s, QStringLiteral("ra")); });
    connect(&m_server,&ServerLauncher::worldState,this,[this,worldDot](bool on){ worldStateLabel->setText(on?QStringLiteral("запущено"):QStringLiteral("не запущено")); worldDot->setStyleSheet(on?QStringLiteral("background:#2ecc71;border-radius:7px;"):QStringLiteral("background:#57606f;border-radius:7px;")); });
    connect(&m_server,&ServerLauncher::realmState,this,[this,realmDot](bool on){ realmStateLabel->setText(on?QStringLiteral("запущено"):QStringLiteral("не запущено")); realmDot->setStyleSheet(on?QStringLiteral("background:#2ecc71;border-radius:7px;"):QStringLiteral("background:#57606f;border-radius:7px;")); });
    connect(analyzeLogs,&QPushButton::clicked,this,[this]{ analyzeServerLogs(); });
    connect(memTune,&QPushButton::clicked,this,[this]{ reduceCoreMemory(); });
    updateCoreBranding();
}

void MainWindow::fillCommandTree() {
    if (!commandTree) return;
    const QString profile = core ? core->currentText() : QString();
    commandTree->clear();
    QString currentGroup;
    QTreeWidgetItem *groupItem = nullptr;
    int n = 0;
    for (const auto &c : ServerLauncher::catalogFor(profile)) {
        if (!groupItem || c.group != currentGroup) {
            currentGroup = c.group;
            groupItem = new QTreeWidgetItem(commandTree, {c.group});
            groupItem->setFirstColumnSpanned(true);
        }
        auto *it = new QTreeWidgetItem(groupItem, {c.title, c.world ? QString::fromUtf8("ядро") : QString::fromUtf8("реалм")});
        it->setData(0, Qt::UserRole, c.command);
        it->setData(0, Qt::UserRole+1, c.help);
        it->setData(0, Qt::UserRole+2, c.world);
        it->setToolTip(0, c.help);
        ++n;
    }
    commandTree->expandAll();
    if (commandFilterHint) {
        const QString name = profile.isEmpty() ? QString::fromUtf8("ядро не выбрано") : profile;
        const bool nord = profile.contains(QLatin1String("Nordrassil"), Qt::CaseInsensitive);
        QString hint = QString::fromUtf8(
            "Команды для «%1»: %2 шт. Общие (server info, reload, GM) — на Trinity-подобных ядрах. "
            "bnetaccount / hotfixes — Legion+ (7.3.5, 10.x, 12.x). account create / authserver — 3.3.5a–5.4.8 и Cypher.")
            .arg(name).arg(n);
        if (nord)
            hint += QString::fromUtf8(" Профиль Nordrassil: показаны его команды и подписи.");
        commandFilterHint->setText(hint);
    }
    if (commandHelp) commandHelp->clear();
    updateCoreBranding();
}

void MainWindow::updateCoreBranding() {
    const QString profile = core ? core->currentText() : QString();
    const bool nord = profile.contains(QLatin1String("Nordrassil"), Qt::CaseInsensitive);
    const quint32 mask = ServerLauncher::coreMaskFromProfile(profile);
    if (serverWorldCard) {
        if (nord)
            serverWorldCard->setTitle(QStringLiteral("Ядро  (worldserver) — Nordrassil 7.3.5 Legion"));
        else if (!profile.isEmpty())
            serverWorldCard->setTitle(QStringLiteral("Ядро  (worldserver) — ") + profile);
        else
            serverWorldCard->setTitle(QStringLiteral("Ядро  (worldserver)"));
    }
    if (serverRealmCard) {
        if (mask & CoreAuth && !(mask & CoreBnet))
            serverRealmCard->setTitle(QStringLiteral("Реалм  (authserver)"));
        else if (mask & CoreBnet)
            serverRealmCard->setTitle(QStringLiteral("Реалм  (bnetserver)"));
        else
            serverRealmCard->setTitle(QStringLiteral("Реалм  (bnetserver / authserver)"));
    }
    if (pickRealmBtn) {
        if (mask & CoreAuth && !(mask & CoreBnet))
            pickRealmBtn->setText(QStringLiteral("authserver.exe"));
        else
            pickRealmBtn->setText(QStringLiteral("bnetserver.exe"));
    }
    if (serverLaunchHint) {
        if (nord)
            serverLaunchHint->setText(QString::fromUtf8(
                "Nordrassil 7.3.5: сначала bnetserver, затем worldserver. Журнал Studio — stdout и Server.log рядом с exe. Копии: папка logs у WowDbStudio.exe."));
        else if (mask & CoreAuth && !(mask & CoreBnet))
            serverLaunchHint->setText(QString::fromUtf8(
                "Сначала authserver, затем worldserver. Журнал: stdout и Server.log / Errors.log рядом с exe. Копии Studio — папка logs у WowDbStudio.exe."));
        else
            serverLaunchHint->setText(QString::fromUtf8(
                "Сначала реалм, затем ядро. Журнал: stdout процесса и файлы Server.log / Errors.log рядом с exe. Копии Studio — папка logs у WowDbStudio.exe."));
    }
    if (contentEditorHint) {
        if (nord)
            contentEditorHint->setText(QString::fromUtf8(
                "Редактор контента для Nordrassil 7.3.5: сущности, SQL-шаблоны, связи. Схема — живая база «Подключение»."));
        else
            contentEditorHint->setText(QString::fromUtf8(
                "Редактор контента: сущности (предмет / NPC / объект / квест / лут), SQL-шаблоны и связи. "
                "Значения сверяются с живой схемой базы («Подключение»). Имена таблиц зависят от выбранного ядра."));
    }
}

void MainWindow::sendPickedCommand() {
    auto *it = commandTree->currentItem();
    const bool world = !it || it->data(0, Qt::UserRole+2).toBool();
    if (world) m_server.sendWorld(serverCommand->text());
    else m_server.sendRealm(serverCommand->text());
}

void MainWindow::buildWizardsTab(QTabWidget *tabs, QWidget *scriptSection) {
    auto *page=new QWidget; auto *root=new QVBoxLayout(page);
    root->setContentsMargins(16,16,16,16); root->setSpacing(12);
    const QString cardQss = QStringLiteral(
        "QGroupBox{border:1px solid #e2e8f0;border-radius:10px;margin-top:14px;"
        "padding:14px 12px 12px 12px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#2563eb;}");
    contentEditorHint = wrapLabel(QStringLiteral(
        "Редактор контента: сущности (предмет / NPC / объект / квест / лут), SQL-шаблоны и связи. "
        "Схема — живая база вкладки «Подключение»."));
    root->addWidget(contentEditorHint);

    // === Создать сущность ===
    auto *entCard=new QGroupBox(QStringLiteral("Создать сущность")); entCard->setStyleSheet(cardQss);
    auto *el=new QVBoxLayout(entCard); el->setSpacing(8); el->setContentsMargins(12,18,12,12);
    auto *entRow=new QHBoxLayout; entRow->setSpacing(8);
    ceEntity=new QComboBox;
    const auto ceSpecs = ContentEditorService::specs();
    for (const auto &s : ceSpecs) ceEntity->addItem(s.title, s.key);
    entRow->addWidget(wrapLabel(QStringLiteral("Тип:"))); entRow->addWidget(ceEntity,1);
    el->addLayout(entRow);
    ceFieldsHost=new QWidget; ceFieldsLayout=new QFormLayout(ceFieldsHost); ceFieldsLayout->setSpacing(6);
    el->addWidget(ceFieldsHost);
    auto *entGenRow=new QHBoxLayout; entGenRow->setSpacing(8);
    auto *entGen=button(QStringLiteral("Сгенерировать INSERT"));
    auto *entToConsole=button(QStringLiteral("В SQL-консоль"));
    entGenRow->addWidget(entGen); entGenRow->addWidget(entToConsole); entGenRow->addStretch();
    el->addLayout(entGenRow);
    root->addWidget(entCard);

    auto *selCard=new QGroupBox(QStringLiteral("Шаблон")); selCard->setStyleSheet(cardQss);
    auto *sl=new QVBoxLayout(selCard); sl->setSpacing(8); sl->setContentsMargins(12,18,12,12);
    auto *selRow=new QHBoxLayout; selRow->setSpacing(8);
    wizTemplate=new QComboBox;
    auto *loadFile=button(QStringLiteral("Загрузить шаблон из файла…"));
    selRow->addWidget(wizTemplate,1); selRow->addWidget(loadFile);
    sl->addLayout(selRow);
    sl->addWidget(wrapLabel(QStringLiteral("Текст шаблона:")));
    wizSource=new QTextEdit; wizSource->setReadOnly(true); wizSource->setMaximumHeight(170);
    wizSource->setPlaceholderText(QStringLiteral("Здесь будет текст шаблона с плейсхолдерами @TOKEN."));
    sl->addWidget(wizSource);
    root->addWidget(selCard);

    auto *fldCard=new QGroupBox(QStringLiteral("Плейсхолдеры")); fldCard->setStyleSheet(cardQss);
    auto *fl=new QVBoxLayout(fldCard); fl->setSpacing(8); fl->setContentsMargins(12,18,12,12);
    wizFieldsHost=new QWidget; wizFieldsLayout=new QFormLayout(wizFieldsHost); wizFieldsLayout->setSpacing(6);
    fl->addWidget(wizFieldsHost);
    auto *genRow=new QHBoxLayout; genRow->setSpacing(8);
    auto *generate=button(QStringLiteral("Сгенерировать SQL"));
    auto *toConsole=button(QStringLiteral("В SQL-консоль"));
    genRow->addWidget(generate); genRow->addWidget(toConsole); genRow->addStretch();
    fl->addLayout(genRow);
    root->addWidget(fldCard);

    // === Диагностика связей и триггеров ===
    auto *diagCard=new QGroupBox(QStringLiteral("Диагностика связей и триггеров (живая БД)")); diagCard->setStyleSheet(cardQss);
    auto *dl=new QVBoxLayout(diagCard); dl->setSpacing(8); dl->setContentsMargins(12,18,12,12);
    auto *diagRow=new QHBoxLayout; diagRow->setSpacing(8);
    ceDiagInput=new QLineEdit; ceDiagInput->setPlaceholderText(QStringLiteral("ID квеста или entry NPC"));
    auto *diagQuest=button(QStringLiteral("Проверить квест"));
    auto *diagNpc=button(QStringLiteral("Проверить NPC"));
    diagRow->addWidget(ceDiagInput,1); diagRow->addWidget(diagQuest); diagRow->addWidget(diagNpc);
    dl->addLayout(diagRow);
    dl->addWidget(wrapLabel(QStringLiteral(
        "Квест: привязка к NPC (creature_queststarter/questender), цели quest_objectives и выпадение предметов цели. "
        "NPC: выдаваемые квесты и триггеры SmartAI (smart_scripts). Нужна активная база.")));
    ceReport=new QTextEdit; ceReport->setReadOnly(true);
    ceReport->setPlaceholderText(QStringLiteral("Здесь появится отчёт диагностики и проверки схемы."));
    dl->addWidget(ceReport);
    root->addWidget(diagCard);

    // === Скриптинг (SmartAI) — перенесён из отдельной вкладки ===
    if (scriptSection) {
        auto *scriptCard=new QGroupBox(QStringLiteral("Скриптинг — SmartAI (NPC / игровые объекты)"));
        scriptCard->setStyleSheet(cardQss);
        auto *scl=new QVBoxLayout(scriptCard); scl->setContentsMargins(12,18,12,12); scl->setSpacing(8);
        scl->addWidget(scriptSection);
        root->addWidget(scriptCard);
    }

    // === WDBX / патч клиента (DB2) ===
    auto *wdbxCard=new QGroupBox(QStringLiteral("WDBX / патч клиента (DB2)"));
    wdbxCard->setStyleSheet(cardQss);
    auto *wl=new QVBoxLayout(wdbxCard); wl->setContentsMargins(12,18,12,12); wl->setSpacing(8);
    wl->addWidget(wrapLabel(QStringLiteral(
        "Legion хранит часть данных в клиентских DB2. WDBX Editor открывает DBC/DB2/WDB/ADB и позволяет добавить/изменить строки. "
        "ID в world, hotfixes и DB2 должны совпадать. Выбери задачу и папку клиента — Studio соберёт пошаговую инструкцию.")));
    wdbxTask=new QComboBox;
    wdbxTask->addItems({QStringLiteral("Новый предмет"),QStringLiteral("Правка предмета"),QStringLiteral("Новый спелл"),QStringLiteral("NPC → квестовый предмет (CreatureQuestItem)"),QStringLiteral("Статы предмета (item-sparse)")});
    wl->addWidget(wrapLabel(QStringLiteral("Задача:"))); wl->addWidget(wdbxTask);
    wdbxClientFolder=new QLineEdit; wdbxClientFolder->setPlaceholderText(QStringLiteral("Папка клиента (где Wow.exe / DBFilesClient)"));
    auto *browseClient=button(QStringLiteral("Выбрать папку…"));
    auto *wRow=new QHBoxLayout; wRow->addWidget(wdbxClientFolder,1); wRow->addWidget(browseClient);
    wl->addLayout(wRow);
    auto *genWdbx=button(QStringLiteral("Сформировать инструкцию WDBX"));
    wl->addWidget(genWdbx);
    wdbxOut=new QTextEdit; wdbxOut->setReadOnly(true); wdbxOut->setMinimumHeight(160);
    wl->addWidget(wdbxOut);
    root->addWidget(wdbxCard);
    connect(browseClient,&QAbstractButton::clicked,this,[this]{ const auto f=QFileDialog::getExistingDirectory(this,QStringLiteral("Папка клиента")); if(!f.isEmpty()) wdbxClientFolder->setText(f); });
    connect(genWdbx,&QAbstractButton::clicked,this,[this]{ generateWdbxInstructions(); });
    connect(wdbxTask,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){ generateWdbxInstructions(); });

    auto *outCard=new QGroupBox(QStringLiteral("Готовый SQL")); outCard->setStyleSheet(cardQss);
    auto *ol=new QVBoxLayout(outCard); ol->setSpacing(8); ol->setContentsMargins(12,18,12,12);
    wizOutput=new QTextEdit; wizOutput->setReadOnly(true);
    wizOutput->setPlaceholderText(QStringLiteral("Нажми «Сгенерировать SQL»."));
    ol->addWidget(wizOutput);
    root->addWidget(outCard,1);

    tabs->addTab(scrollWrap(page), QStringLiteral("Редактор контента"));

    m_wizTemplates = SqlWizardService::builtin();
    for (const auto &t : m_wizTemplates) wizTemplate->addItem(t.name);

    connect(wizTemplate, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i){ wizLoadTemplate(i); });
    connect(loadFile, &QPushButton::clicked, this, [this]{
        const auto f = QFileDialog::getOpenFileName(this, QStringLiteral("Шаблон SQL"), QString(), QStringLiteral("SQL (*.sql);;All files (*)"));
        if (f.isEmpty()) return;
        QString sqlText, err;
        if (!SqlWizardService::loadFile(f, &sqlText, &err)) { QMessageBox::critical(this, QStringLiteral("Ошибка"), err); return; }
        m_wizTemplates.append({QFileInfo(f).fileName(), sqlText});
        wizTemplate->addItem(m_wizTemplates.last().name);
        wizTemplate->setCurrentIndex(m_wizTemplates.size()-1);
    });
    connect(generate, &QPushButton::clicked, this, [this]{ wizGenerate(); });
    connect(toConsole, &QPushButton::clicked, this, [this,tabs]{
        const QString s = wizOutput->toPlainText().trimmed();
        if (s.isEmpty()) return;
        if (sql) sql->setPlainText(s);
        if (tabs) tabs->setCurrentIndex(1);
    });

    // --- Редактор контента: сущности и диагностика ---
    connect(ceEntity, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i){ ceLoadEntity(i); });
    connect(entGen, &QPushButton::clicked, this, [this]{ ceGenerate(); });
    connect(entToConsole, &QPushButton::clicked, this, [this,tabs]{
        const QString s = wizOutput->toPlainText().trimmed();
        if (s.isEmpty()) return;
        if (sql) sql->setPlainText(s);
        if (tabs) tabs->setCurrentIndex(1);
    });
    connect(diagQuest, &QPushButton::clicked, this, [this]{ ceDiagnose(0); });
    connect(diagNpc, &QPushButton::clicked, this, [this]{ ceDiagnose(1); });

    if (!m_wizTemplates.isEmpty()) { wizTemplate->setCurrentIndex(0); wizLoadTemplate(0); }
    if (ceEntity->count() > 0) ceLoadEntity(0);
    updateCoreBranding();
}

void MainWindow::wizLoadTemplate(int index) {
    if (index < 0 || index >= m_wizTemplates.size()) return;
    const QString sqlText = m_wizTemplates.at(index).sql;
    if (wizSource) wizSource->setPlainText(sqlText);
    if (wizFieldsLayout) {
        while (wizFieldsLayout->count() > 0) {
            QLayoutItem *it = wizFieldsLayout->takeAt(0);
            if (!it) break;
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
    }
    m_wizFields.clear();
    const QStringList toks = SqlWizardService::placeholders(sqlText);
    for (const auto &t : toks) {
        auto *e = new QLineEdit;
        e->setPlaceholderText(QStringLiteral("значение для @") + t);
        if (wizFieldsLayout) wizFieldsLayout->addRow(QStringLiteral("@") + t, e);
        m_wizFields.insert(t, e);
    }
    if (wizOutput) wizOutput->clear();
}

void MainWindow::wizGenerate() {
    if (wizTemplate == nullptr) return;
    const int index = wizTemplate->currentIndex();
    if (index < 0 || index >= m_wizTemplates.size()) return;
    QMap<QString, QString> values;
    for (auto it = m_wizFields.constBegin(); it != m_wizFields.constEnd(); ++it)
        values.insert(it.key(), it.value()->text().trimmed());
    const QString out = SqlWizardService::generate(m_wizTemplates.at(index).sql, values);
    if (wizOutput) wizOutput->setPlainText(out);
}

void MainWindow::ceLoadEntity(int index) {
    if (!ceEntity || !ceFieldsLayout || index < 0) return;
    const QString key = ceEntity->itemData(index).toString();
    const ContentEntitySpec *spec = ContentEditorService::specByKey(key);
    if (!spec) return;
    while (ceFieldsLayout->count() > 0) {
        QLayoutItem *it = ceFieldsLayout->takeAt(0);
        if (!it) break;
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    m_ceFields.clear();
    for (const auto &f : spec->fields) {
        auto *e = new QLineEdit;
        e->setPlaceholderText(f.placeholder);
        ceFieldsLayout->addRow(f.label + (f.required ? QStringLiteral(" *") : QString()), e);
        m_ceFields.insert(f.column, e);
    }
}

void MainWindow::ceGenerate() {
    if (!ceEntity) return;
    const QString key = ceEntity->currentData().toString();
    const ContentEntitySpec *spec = ContentEditorService::specByKey(key);
    if (!spec) return;
    QMap<QString, QString> values;
    for (auto it = m_ceFields.constBegin(); it != m_ceFields.constEnd(); ++it)
        values.insert(it.key(), it.value()->text().trimmed());
    QString report;
    const QString sqlText = ContentEditorService::buildInsert(*spec, values, m_db.isOpen() ? &m_db : nullptr, &report);
    if (wizOutput) wizOutput->setPlainText(sqlText.isEmpty() ? QStringLiteral("-- Нет данных для вставки.") : sqlText);
    if (ceReport) ceReport->setPlainText(report);
}

void MainWindow::ceDiagnose(int mode) {
    if (!ceDiagInput || !ceReport) return;
    if (!m_db.isOpen()) {
        ceReport->setPlainText(QStringLiteral("Нет подключения к базе — сначала подключитесь во вкладке «Подключение»."));
        return;
    }
    bool ok = false;
    const int id = ceDiagInput->text().trimmed().toInt(&ok);
    if (!ok) { ceReport->setPlainText(QStringLiteral("Введите числовой ID квеста или entry NPC.")); return; }
    const QString res = (mode == 0)
        ? ContentEditorService::diagnoseQuest(m_db, id)
        : ContentEditorService::diagnoseNpc(m_db, id);
    ceReport->setPlainText(res);
}

void MainWindow::generateWdbxInstructions() {
    if (!wdbxOut) return;
    const QString folder = wdbxClientFolder ? wdbxClientFolder->text().trimmed() : QString();
    const int t = wdbxTask ? wdbxTask->currentIndex() : 0;
    QString s = QStringLiteral("===== WDBX Editor: пошаговая инструкция =====\n");
    if (!folder.isEmpty()) {
        const bool isDb = folder.endsWith("/DBFilesClient", Qt::CaseInsensitive) || folder.endsWith("\\DBFilesClient", Qt::CaseInsensitive);
        s += QStringLiteral("Папка клиента: %1\nDBFilesClient: %1\n\n").arg(isDb ? folder : folder + QStringLiteral("/DBFilesClient"));
    } else {
        s += QStringLiteral("Папка клиента не указана — выбери каталог с Wow.exe / DBFilesClient.\n\n");
    }
    s += QStringLiteral("Общий порядок: (1) запусти WDBX Editor; (2) File → Open → выбери .db2 из DBFilesClient; "
                        "(3) добавь/измени строки, ID обязаны совпадать с world/hotfixes; (4) File → Save; (5) перезапусти клиент (или обнови DBCache.bin).\n\n");
    switch (t) {
        case 0: s += QStringLiteral("Задача: НОВЫЙ предмет.\nФайлы: item.db2 (базовая запись), item-sparse.db2 (ItemLevel, качество, статы, InventoryType), item-effect.db2 (эффекты), ItemDisplayInfo.db2 (иконка/модель).\nДобавь строку с ID = entry из world.item_template; в item-sparse тот же ID; заполни ItemStatValue1..10 + StatModifierBonusStat1..10, ItemLevel, RequiredLevel, InventoryType, Quality.\n"); break;
        case 1: s += QStringLiteral("Задача: ПРАВКА существующего предмета.\nСервер: world.item_template + hotfixes.item_sparse. Если меняются отображаемые поля (имя, ItemLevel, статы) — правь item-sparse.db2 (и item.db2) в клиенте с тем же ID.\n"); break;
        case 2: s += QStringLiteral("Задача: НОВЫЙ спелл.\nФайлы: spell.db2, spell-effect.db2, spell-aura-options.db2. Добавь строки с ID, совпадающим с серверным spell_id.\n"); break;
        case 3: s += QStringLiteral("Задача: привязка NPC → квестовый предмет (подсветка + 0/N в тултипе).\nФайл: CreatureQuestItem.db2 (если присутствует в клиенте). Добавь строку CreatureEntry=<NPC> → Item=<предмет> (свои ID).\nЕсли файла в клиенте нет — серверный обход: kill-цель в quest_objectives (Type=0, ObjectID=<NPC>).\n"); break;
        default: s += QStringLiteral("Задача: статы предмета.\nФайл: item-sparse.db2. Заполни ItemStatValue1..10 и StatModifierBonusStat1..10 (4=Agility,5=Intellect,7=Stamina,32=Crit,36=Haste,40=Versatility,49=Mastery), ItemLevel, RequiredLevel.\n"); break;
    }
    s += QStringLiteral("\nВАЖНО: ID в world, hotfixes и DB2 обязаны совпадать, иначе клиент не отобразит контент или вылетит. Hotfixes не reload-ится — рестарт ядра; DB2 — перезапуск клиента.\n");
    wdbxOut->setPlainText(s);
}

void MainWindow::buildConsoleTab(QTabWidget *tabs) {
    auto *page=new QWidget; auto *root=new QVBoxLayout(page);
    root->setContentsMargins(16,16,16,16); root->setSpacing(12);
    const QString cardQss = QStringLiteral(
        "QGroupBox{border:1px solid #e2e8f0;border-radius:12px;margin-top:16px;"
        "padding:16px 14px 14px 14px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;padding:2px 8px;color:#2563eb;background:#ffffff;border-radius:6px;}");

    root->addWidget(wrapLabel(QStringLiteral(
        "Универсальная консоль: пакетный SQL (несколько запросов через точку с запятой) и команды ядру / реалму / RA. "
        "Каждый SQL-запрос выполняется отдельно, вывод и ошибки — ниже. Команды ядру идут через RA-подключение (настраивается во вкладке «Сервер»).")));

    auto *inCard=new QGroupBox(QStringLiteral("Ввод")); inCard->setStyleSheet(cardQss);
    auto *il=new QVBoxLayout(inCard); il->setSpacing(8); il->setContentsMargins(12,18,12,12);
    consoleInput=new QTextEdit;
    consoleInput->setPlaceholderText(QStringLiteral("SQL через ; (например: UPDATE ...; INSERT ...;)  или команда консоли (например: .reload quest_template)"));
    consoleInput->setMaximumHeight(160);
    il->addWidget(consoleInput);
    auto *btnRow=new QHBoxLayout; btnRow->setSpacing(6);
    auto *runSql=button(QStringLiteral("Выполнить SQL (пакет)"));
    auto *toWorld=button(QStringLiteral("В ядро"));
    auto *toRealm=button(QStringLiteral("В реалм"));
    auto *toRa=button(QStringLiteral("В RA"));
    auto *clearOut=button(QStringLiteral("Очистить вывод"));
    btnRow->addWidget(runSql); btnRow->addWidget(toWorld); btnRow->addWidget(toRealm); btnRow->addWidget(toRa); btnRow->addStretch(); btnRow->addWidget(clearOut);
    il->addLayout(btnRow);
    root->addWidget(inCard);

    auto *outCard=new QGroupBox(QStringLiteral("Вывод")); outCard->setStyleSheet(cardQss);
    auto *ol=new QVBoxLayout(outCard); ol->setSpacing(8); ol->setContentsMargins(12,18,12,12);
    consoleOutput=new QTextEdit; consoleOutput->setReadOnly(true);
    consoleOutput->setPlaceholderText(QStringLiteral("Результаты появятся здесь."));
    ol->addWidget(consoleOutput);
    root->addWidget(outCard,1);

    tabs->addTab(scrollWrap(page), QStringLiteral("Консоль"));

    connect(runSql, &QPushButton::clicked, this, [this]{ consoleRunSql(); });
    connect(toWorld, &QPushButton::clicked, this, [this]{ const QString c=consoleInput->toPlainText().trimmed(); if(c.isEmpty())return; m_server.sendWorld(c); consoleAppend(QStringLiteral("→ в ядро: ")+c); });
    connect(toRealm, &QPushButton::clicked, this, [this]{ const QString c=consoleInput->toPlainText().trimmed(); if(c.isEmpty())return; m_server.sendRealm(c); consoleAppend(QStringLiteral("→ в реалм: ")+c); });
    connect(toRa, &QPushButton::clicked, this, [this]{ const QString c=consoleInput->toPlainText().trimmed(); if(c.isEmpty())return; m_server.sendRa(c); consoleAppend(QStringLiteral("→ в RA: ")+c); });
    connect(clearOut, &QPushButton::clicked, this, [this]{ if(consoleOutput) consoleOutput->clear(); });
    connect(&m_server, &ServerLauncher::logWorld, this, [this](const QString &l){ consoleAppend(QStringLiteral("[ядро] ")+l); });
    connect(&m_server, &ServerLauncher::logRealm, this, [this](const QString &l){ consoleAppend(QStringLiteral("[реалм] ")+l); });
    connect(&m_server, &ServerLauncher::logRa, this, [this](const QString &l){ consoleAppend(QStringLiteral("[RA] ")+l); });
}

void MainWindow::consoleRunSql() {
    if (!consoleInput) return;
    if (!m_db.isOpen()) { consoleAppend(QStringLiteral("✗ Нет подключения к базе — сначала подключитесь во вкладке «Подключение».")); return; }
    const QString text = consoleInput->toPlainText();
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    int total=0, ok=0, fail=0;
    consoleAppend(QStringLiteral("=== Пакетный SQL ==="));
    for (const QString &p : parts) {
        const QString q = p.trimmed();
        if (q.isEmpty()) continue;
        ++total;
        QString err; qint64 affected = 0;
        if (m_db.execute(q, &err, &affected)) {
            ++ok;
            consoleAppend(QStringLiteral("✓ OK (%1 затронуто): %2").arg(affected).arg(q.length()>90 ? q.left(90)+QLatin1String("…") : q));
        } else {
            ++fail;
            consoleAppend(QStringLiteral("✗ ОШИБКА: %1\n   → %2").arg(err, q.length()>120 ? q.left(120)+QLatin1String("…") : q));
        }
    }
    consoleAppend(QStringLiteral("=== Готово: всего %1, успешно %2, с ошибкой %3 ===").arg(total).arg(ok).arg(fail));
}

void MainWindow::consoleAppend(const QString &line) {
    if (consoleOutput) { consoleOutput->append(line); capLog(consoleOutput); }
}

void MainWindow::buildQaTab(QTabWidget *tabs) {
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);
    root->addWidget(wrapLabel(QStringLiteral(
        "Пять ботов. Цель — как на ретейле. Клиент не нужен: world + hotfixes + логи. "
        "Координаты: map X Y Z [orientation в радианах]. "
        "Каждая строка ниже — проблема и SQL. Отметьте и внесите в базу.")));
    root->addWidget(wrapLabel(QStringLiteral(
        "1) Геодезист — карта, XYZ, ориентация, бродяжничество.\n"
        "2) Бестиарий — NPC, модели, имена (Wago Creature / DisplayInfo).\n"
        "3) Квестолог — квесты, стартер/энд.\n"
        "4) Интендант — предметы, item_sparse, вендор.\n"
        "5) Режиссёр — SmartAI, маршруты (не бродить, идти по пути).")));
    qaPreset = new QComboBox;
    qaPreset->addItem(QStringLiteral("Свободная задача"), QString());
    qaPreset->addItem(QStringLiteral("Аудит всего (5 ботов)"), QStringLiteral("аудит сервера"));
    qaPreset->addItem(QStringLiteral("NPC: шаблон, модель, спавн, AI"), QStringLiteral("проверь NPC "));
    qaPreset->addItem(QStringLiteral("NPC не бродить по карте"), QStringLiteral("NPC  не бродить"));
    qaPreset->addItem(QStringLiteral("NPC идти по траектории"), QStringLiteral("NPC  идти по траектории"));
    qaPreset->addItem(QStringLiteral("Поставить NPC в точку map x y z"),
                     QStringLiteral("NPC  map=0 x=0 y=0 z=0 o=0"));
    qaPreset->addItem(QStringLiteral("Квест"), QStringLiteral("квест "));
    qaPreset->addItem(QStringLiteral("Предмет"), QStringLiteral("предмет "));
    qaPreset->addItem(QStringLiteral("Остров изгнанников: мир"),
                     QStringLiteral("Остров изгнанников: все NPC, движения, объекты, задания, способности NPC, заселённость, входы в подземелья"));
    auto *scopeCard = new QGroupBox(QStringLiteral("Область QA — выбрать, где именно искать поломки"));
    auto *scopeLayout = new QGridLayout(scopeCard);
    qaScopeType = new QComboBox;
    qaScopeType->addItem(QStringLiteral("Авто по задаче"), QStringLiteral("auto"));
    qaScopeType->addItem(QStringLiteral("Локация / зона"), QStringLiteral("zone"));
    qaScopeType->addItem(QStringLiteral("Подземелье"), QStringLiteral("dungeon"));
    qaScopeType->addItem(QStringLiteral("Рейд"), QStringLiteral("raid"));
    qaScopeName = new QLineEdit;
    qaScopeName->setPlaceholderText(QStringLiteral("Например: Harandar, Zul'Aman, The Blinding Vale, raid name…"));
    {
        const QStringList scopeHints = {
            QStringLiteral("Harandar"), QStringLiteral("Zul'Aman"), QStringLiteral("Silvermoon"),
            QStringLiteral("Murder Row"), QStringLiteral("Den of Nalorakk"),
            QStringLiteral("The Blinding Vale"), QStringLiteral("Voidscar Arena"),
            QStringLiteral("Altar of Fangs"), QStringLiteral("Manaforge Omega")
        };
        auto *completer = new QCompleter(scopeHints, qaScopeName);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        qaScopeName->setCompleter(completer);
    }
    qaScopeMap = new QSpinBox; qaScopeMap->setRange(0, 99999); qaScopeMap->setSpecialValueText(QStringLiteral("auto"));
    qaScopeZone = new QSpinBox; qaScopeZone->setRange(0, 999999); qaScopeZone->setSpecialValueText(QStringLiteral("auto"));
    qaScopeStatus = new QLabel(QStringLiteral("Не задано — область определяется по тексту задачи."));
    qaScopeStatus->setWordWrap(true);
    auto *scopeResolve = button(QStringLiteral("Найти в Wago-кэше"));
    auto *scopeClear = button(QStringLiteral("Сбросить"));
    scopeLayout->addWidget(new QLabel(QStringLiteral("Тип:")), 0, 0);
    scopeLayout->addWidget(qaScopeType, 0, 1);
    scopeLayout->addWidget(new QLabel(QStringLiteral("Название:")), 0, 2);
    scopeLayout->addWidget(qaScopeName, 0, 3, 1, 3);
    scopeLayout->addWidget(new QLabel(QStringLiteral("Map ID:")), 1, 0);
    scopeLayout->addWidget(qaScopeMap, 1, 1);
    scopeLayout->addWidget(new QLabel(QStringLiteral("Zone/Area ID:")), 1, 2);
    scopeLayout->addWidget(qaScopeZone, 1, 3);
    scopeLayout->addWidget(scopeResolve, 1, 4);
    scopeLayout->addWidget(scopeClear, 1, 5);
    scopeLayout->addWidget(qaScopeStatus, 2, 0, 1, 6);
    root->addWidget(scopeCard);
    {
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        qaScopeType->setCurrentText(s.value(QStringLiteral("qa/scopeType"), QStringLiteral("Авто по задаче")).toString());
        qaScopeName->setText(s.value(QStringLiteral("qa/scopeName")).toString());
        qaScopeMap->setValue(s.value(QStringLiteral("qa/scopeMap"), 0).toInt());
        qaScopeZone->setValue(s.value(QStringLiteral("qa/scopeZone"), 0).toInt());
    }
    connect(scopeResolve, &QPushButton::clicked, this, &MainWindow::resolveQaScopeFromCache);
    connect(scopeClear, &QPushButton::clicked, this, [this] {
        if (qaScopeType) qaScopeType->setCurrentIndex(0);
        if (qaScopeName) qaScopeName->clear();
        if (qaScopeMap) qaScopeMap->setValue(0);
        if (qaScopeZone) qaScopeZone->setValue(0);
        if (qaScopeStatus) qaScopeStatus->setText(QStringLiteral("Не задано — область определяется по тексту задачи."));
    });
    auto saveScope = [this] {
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        if (qaScopeType) s.setValue(QStringLiteral("qa/scopeType"), qaScopeType->currentText());
        if (qaScopeName) s.setValue(QStringLiteral("qa/scopeName"), qaScopeName->text());
        if (qaScopeMap) s.setValue(QStringLiteral("qa/scopeMap"), qaScopeMap->value());
        if (qaScopeZone) s.setValue(QStringLiteral("qa/scopeZone"), qaScopeZone->value());
    };
    connect(qaScopeType, &QComboBox::currentTextChanged, this, [this, saveScope](const QString &) { if (qaScopeStatus) qaScopeStatus->setText(QStringLiteral("Область задана. При запуске QA будут использоваться эти границы; классы/таланты остаются отдельным контуром.")); saveScope(); });
    connect(qaScopeName, &QLineEdit::editingFinished, this, [saveScope] { saveScope(); });
    connect(qaScopeMap, QOverload<int>::of(&QSpinBox::valueChanged), this, [saveScope](int) { saveScope(); });
    connect(qaScopeZone, QOverload<int>::of(&QSpinBox::valueChanged), this, [saveScope](int) { saveScope(); });

    qaSniffPath = new QLineEdit;
    qaSniffPath->setPlaceholderText(QStringLiteral("Папка .sql сниффов (вывод WowPacketParser)"));
    {
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        qaSniffPath->setText(s.value(QStringLiteral("qa/sniffDir")).toString());
    }
    auto *sniffBrowse = button(QStringLiteral("Папка сниффов…"));
    auto *sniffHelp = button(QStringLiteral("Откуда сниффы"));
    auto *sniffRow = new QHBoxLayout;
    sniffRow->addWidget(qaSniffPath, 1);
    sniffRow->addWidget(sniffBrowse);
    sniffRow->addWidget(sniffHelp);
    root->addWidget(wrapLabel(QStringLiteral("Снифф WPP (необязательно; ярды/GUID/movement):")));
    root->addLayout(sniffRow);

    auto *refCard = new QGroupBox(QStringLiteral("Retail Reference — без клиента и WPP"));
    auto *refLayout = new QHBoxLayout(refCard);
    qaReferenceBuild = new QLineEdit(RetailReferenceService::defaultBuild());
    qaReferenceBuild->setPlaceholderText(QStringLiteral("12.1.0.69497"));
    auto *refLocaleLabel = new QLabel(QStringLiteral("Locale: ruRU"));
    if (wagoLocale) {
        refLocaleLabel->setText(QStringLiteral("Locale: %1").arg(wagoLocale->currentText().trimmed().isEmpty() ? QStringLiteral("ruRU") : wagoLocale->currentText().trimmed()));
        connect(wagoLocale, &QComboBox::currentTextChanged, refLocaleLabel, [refLocaleLabel](const QString &value) {
            refLocaleLabel->setText(QStringLiteral("Locale: %1").arg(value.trimmed().isEmpty() ? QStringLiteral("ruRU") : value.trimmed()));
        });
    }
    auto *refSync = button(QStringLiteral("Синхронизировать Wago DB2"));
    auto *refOpen = button(QStringLiteral("Открыть кэш"));
    auto *refHelp = button(QStringLiteral("Что это?"));
    refLayout->addWidget(new QLabel(QStringLiteral("Build:")));
    refLayout->addWidget(qaReferenceBuild, 1);
    refLayout->addWidget(refLocaleLabel);
    refLayout->addWidget(refSync);
    refLayout->addWidget(refOpen);
    refLayout->addWidget(refHelp);
    root->addWidget(refCard);

    auto *communityCard = new QGroupBox(QStringLiteral("Community Reference — MDT + DBM + LittleWigs + BigWigs + ATT + Keystone.guru"));
    auto *communityLayout = new QHBoxLayout(communityCard);
    auto *communityBuildLabel = new QLabel(QStringLiteral("Build:"));
    auto *communitySync = button(QStringLiteral("Синхронизировать Community"));
    auto *communityOpen = button(QStringLiteral("Открыть кэш"));
    auto *communityHelp = button(QStringLiteral("Источники"));
    communityLayout->addWidget(communityBuildLabel);
    communityLayout->addWidget(communitySync);
    communityLayout->addWidget(communityOpen);
    communityLayout->addWidget(communityHelp);
    root->addWidget(communityCard);
    connect(communitySync, &QPushButton::clicked, this, [this] {
        const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailCommunityReferenceService::defaultBuild();
        m_qaCommunityBusy = true;
        m_qaCommunityForRun = false;
        if (qaReport) qaReport->setPlainText(QStringLiteral("Community Reference: синхронизация источников build %1…").arg(build));
        m_retailCommunity.syncAll(build);
    });
    connect(communityOpen, &QPushButton::clicked, this, [this] {
        const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailCommunityReferenceService::defaultBuild();
        QDir().mkpath(RetailCommunityReferenceService::cacheRoot(build));
        QDesktopServices::openUrl(QUrl::fromLocalFile(RetailCommunityReferenceService::cacheRoot(build)));
    });
    connect(communityHelp, &QPushButton::clicked, this, [this] {
        askScrollDialog(this, QStringLiteral("Community Retail Reference"),
                        QStringLiteral("Мульти-источниковый reference для QA"),
                        RetailCommunityReferenceService::help(), {QStringLiteral("OK")});
    });
    connect(&m_retailCommunity, &RetailCommunityReferenceService::progress, this, [this](const QString &m) {
        addLog(m);
        if (qaReport) qaReport->setPlainText(m);
    });
    connect(&m_retailCommunity, &RetailCommunityReferenceService::failed, this, [this](const QString &m) {
        addLog(QStringLiteral("Community Reference: ") + m);
    });
    connect(&m_retailCommunity, &RetailCommunityReferenceService::finished, this, [this](bool ok, const QString &m, const QString &build) {
        m_qaCommunityBusy = false;
        addLog(m);
        if (!m_qaCommunityForRun) {
            if (qaReport) qaReport->setPlainText(m);
            return;
        }
        if (ok) m_retailCommunity.enrichCatalog(&m_pendingQaCatalog, build);
        else m_pendingQaCatalog.note += QStringLiteral("\nCommunity Reference не полностью синхронизирован: %1").arg(m);
        m_qaCommunityForRun = false;
        finishQaRun(m_pendingQaCatalog);
    });

    connect(refSync, &QPushButton::clicked, this, [this] {
        const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailReferenceService::defaultBuild();
        m_qaReferenceBusy = true;
        m_qaReferenceForRun = false;
        if (wagoLocale) m_retailReference.setLocale(wagoLocale->currentText().trimmed());
        if (qaReport) qaReport->setPlainText(QStringLiteral("Retail Reference: синхронизация Wago DB2 build %1, locale %2…").arg(build, m_retailReference.locale()));
        m_retailReference.syncBuild(build);
    });
    connect(refOpen, &QPushButton::clicked, this, [this] {
        const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailReferenceService::defaultBuild();
        QDir().mkpath(RetailReferenceService::cacheRoot(build));
        QDesktopServices::openUrl(QUrl::fromLocalFile(RetailReferenceService::cacheRoot(build)));
    });
    connect(refHelp, &QPushButton::clicked, this, [this] {
        askScrollDialog(this, QStringLiteral("Retail Reference"),
                        QStringLiteral("Данные для QA без официального клиента WoW"),
                        RetailReferenceService::help(), {QStringLiteral("OK")});
    });
    connect(sniffBrowse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка SQL сниффов WPP"));
        if (d.isEmpty() || !qaSniffPath) return;
        qaSniffPath->setText(d);
        QSettings s(QStringLiteral("WoWDBStudio"), QStringLiteral("WoWDBStudio"));
        s.setValue(QStringLiteral("qa/sniffDir"), d);
    });
    connect(sniffHelp, &QPushButton::clicked, this, [this] {
        askScrollDialog(this, QStringLiteral("Сниффы"),
                        QStringLiteral("Откуда брать сниффы для QA-ботов"),
                        QaSniffSource::sourcesHelp(), {QStringLiteral("OK")});
    });
    qaTask = new QTextEdit;
    qaTask->setMaximumHeight(90);
    qaTask->setPlaceholderText(QStringLiteral(
        "проверь NPC 7381\n"
        "NPC 123 не бродить\n"
        "NPC 123 идти по траектории\n"
        "NPC 123 map=0 x=-8833 y=627 z=94 o=1.57\n"
        "квест 12345\nаудит сервера"));
    qaReport = new QTextEdit;
    qaReport->setReadOnly(true);
    qaFixesTable = new QTableWidget(0, 4);
    qaFixesTable->setHorizontalHeaderLabels(
        {QStringLiteral("Внести"), QStringLiteral("Бот"), QStringLiteral("Проблема"), QStringLiteral("SQL")});
    qaFixesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    qaFixesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    qaFixesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    qaFixesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    qaFixesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    qaFixesTable->setMinimumHeight(180);
    auto *run = button(QStringLiteral("Запустить 5 ботов"));
    auto *apply = button(QStringLiteral("Согласиться и внести выбранные в БД"));
    auto *all = button(QStringLiteral("Отметить все с SQL"));
    auto *none = button(QStringLiteral("Снять все"));
    auto *row = new QHBoxLayout;
    row->addWidget(run);
    row->addWidget(apply);
    row->addWidget(all);
    row->addWidget(none);
    row->addStretch();
    root->addWidget(wrapLabel(QStringLiteral("Шаблон:")));
    root->addWidget(qaPreset);
    root->addWidget(wrapLabel(QStringLiteral("Задача:")));
    root->addWidget(qaTask);
    root->addLayout(row);
    root->addWidget(wrapLabel(QStringLiteral("Решения (отметьте, с чем согласны):")));
    root->addWidget(qaFixesTable, 2);
    root->addWidget(wrapLabel(QStringLiteral("Отчёт:")));
    root->addWidget(qaReport, 1);
    tabs->addTab(scrollWrap(page), QStringLiteral("QA боты"));
    connect(qaPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!qaPreset || !qaTask) return;
        const QString p = qaPreset->currentData().toString();
        if (p.isEmpty()) return;
        qaTask->setPlainText(p);
    });
    connect(run, &QPushButton::clicked, this, [this] { runQaBots(); });
    connect(&m_retail, &QaRetailSource::ready, this, &MainWindow::onRetailCatalogReady);
    connect(&m_retailReference, &RetailReferenceService::progress, this, [this](const QString &m) { addLog(m); if (qaReport) qaReport->setPlainText(m); });
    connect(&m_retailReference, &RetailReferenceService::failed, this, [this](const QString &m) { addLog(QStringLiteral("Retail Reference: ") + m); });
    connect(&m_retailReference, &RetailReferenceService::finished, this, [this](bool ok, const QString &m, const QString &build) {
        m_qaReferenceBusy = false;
        addLog(m);
        Q_UNUSED(build);
        if (!m_qaReferenceForRun) {
            if (qaReport) qaReport->setPlainText(m);
            return;
        }
        if (ok) {
            const QString refBuild = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailReferenceService::defaultBuild();
            m_pendingQaCatalog.note += QStringLiteral("\n%1").arg(m);
            m_retailReference.enrichCatalog(&m_pendingQaCatalog, refBuild);
            m_qaReferenceForRun = false;
            continueQaAfterCommunity(refBuild);
            return;
        }
        m_qaReferenceForRun = false;
        finishQaRun(m_pendingQaCatalog);
    });
    connect(apply, &QPushButton::clicked, this, [this] { applySelectedQaFixes(); });
    connect(all, &QPushButton::clicked, this, [this] {
        if (!qaFixesTable) return;
        for (int i = 0; i < qaFixesTable->rowCount(); ++i) {
            auto *it = qaFixesTable->item(i, 0);
            if (it && it->flags() & Qt::ItemIsUserCheckable) it->setCheckState(Qt::Checked);
        }
    });
    connect(none, &QPushButton::clicked, this, [this] {
        if (!qaFixesTable) return;
        for (int i = 0; i < qaFixesTable->rowCount(); ++i) {
            auto *it = qaFixesTable->item(i, 0);
            if (it) it->setCheckState(Qt::Unchecked);
        }
    });
}

QString MainWindow::composeQaTask() const {
    QString task = qaTask ? qaTask->toPlainText().trimmed() : QString();
    if (qaScopeType && qaScopeType->currentData().toString() != QLatin1String("auto")) {
        const QString kind = qaScopeType->currentData().toString();
        const QString name = qaScopeName ? qaScopeName->text().trimmed() : QString();
        const int map = qaScopeMap ? qaScopeMap->value() : 0;
        const int zone = qaScopeZone ? qaScopeZone->value() : 0;
        QString header = QStringLiteral("scope_type=%1\nscope_name=%2\nscope_map=%3\nscope_zone=%4\n")
                             .arg(kind, name).arg(map).arg(zone);
        task = header + task;
    }
    return task;
}

void MainWindow::resolveQaScopeFromCache() {
    if (!qaScopeName) return;
    const QString query = qaScopeName->text().trimmed();
    if (query.isEmpty()) {
        if (qaScopeStatus) qaScopeStatus->setText(QStringLiteral("Введите название локации/зоны/подземелья/рейда."));
        return;
    }
    const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailReferenceService::defaultBuild();
    int mapId = 0, zoneId = 0; QString canonical;
    if (RetailReferenceService::resolveScopeFromCache(build, query, &mapId, &zoneId, &canonical)) {
        if (qaScopeMap && mapId > 0) qaScopeMap->setValue(mapId);
        if (qaScopeZone && zoneId > 0) qaScopeZone->setValue(zoneId);
        if (!canonical.isEmpty()) qaScopeName->setText(canonical);
        if (qaScopeStatus) qaScopeStatus->setText(QStringLiteral("Найдено в Wago-кэше build %1: %2 (map=%3, zone=%4).")
                                                     .arg(build, canonical.isEmpty() ? query : canonical).arg(mapId).arg(zoneId));
        return;
    }
    if (qaScopeStatus) qaScopeStatus->setText(QStringLiteral("«%1» не найдено в локальном Wago-кэше. Сначала синхронизируйте Map + AreaTable или укажите Map ID вручную.").arg(query));
}

void MainWindow::runQaBots() {
    if (m_qaBusy) return;
    m_qaBusy = true;
    if (qaReport) qaReport->setPlainText(QStringLiteral(
        "Источник QA: Wowhead/кэш + Wago DB2 reference (если кэш есть/синхронизируется) + ваша БД.\n"
        "Читаю зону и подготавливаю эталон…"));
    addLog(QStringLiteral("QA: запрос World source + Retail Reference…"));
    m_retail.fetchForTask(composeQaTask());
}

void MainWindow::onRetailCatalogReady(const QaRetailCatalog &catalog) {
    m_pendingQaCatalog = catalog;
    const QString build = qaReferenceBuild ? qaReferenceBuild->text().trimmed() : RetailReferenceService::defaultBuild();
    if (m_retailReference.hasCachedBuild(build)) {
        m_retailReference.enrichCatalog(&m_pendingQaCatalog, build);
        continueQaAfterCommunity(build);
        return;
    }
    m_qaReferenceBusy = true;
    m_qaReferenceForRun = true;
    if (wagoLocale) m_retailReference.setLocale(wagoLocale->currentText().trimmed());
    m_retailReference.syncBuild(build);
}

void MainWindow::continueQaAfterCommunity(const QString &build) {
    if (!m_retailCommunity.hasCachedBuild(build)) {
        m_qaCommunityBusy = true;
        m_qaCommunityForRun = true;
        m_retailCommunity.syncAll(build);
        return;
    }
    m_retailCommunity.enrichCatalog(&m_pendingQaCatalog, build);
    finishQaRun(m_pendingQaCatalog);
}

void MainWindow::finishQaRun(const QaRetailCatalog &catalog) {
    m_qaBusy = false;
    const QString task = composeQaTask();
    QString logs;
    if (worldLog) logs += worldLog->toPlainText();
    logs += collectServerLogFiles();
    QaRetailCatalog cat = catalog;
    if (qaSniffPath && !qaSniffPath->text().trimmed().isEmpty())
        QaSniffSource::mergeFromFolder(qaSniffPath->text().trimmed(), &cat);
    const QaRunResult result = QaBotService::run(m_db, hotfixSchema(), task, logs, cat);
    m_qaFixes = result.fixes;
    if (qaReport) {
        QString extra = result.report;
        extra += QStringLiteral("\n—— Как на ретейле (кратко) ——\n");
        for (const QaFix &f : m_qaFixes) {
            extra += QStringLiteral("[%1] %2\n  ретейл: %3\n")
                         .arg(f.bot, f.problem, f.retail);
        }
        qaReport->setPlainText(extra);
    }
    if (qaFixesTable) {
        qaFixesTable->setRowCount(0);
        qaFixesTable->setRowCount(m_qaFixes.size());
        for (int i = 0; i < m_qaFixes.size(); ++i) {
            const QaFix &f = m_qaFixes[i];
            auto *chk = new QTableWidgetItem;
            const bool can = !f.sql.isEmpty() && !f.sql.startsWith(QLatin1Char('-'));
            chk->setFlags(can ? (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
                              : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            chk->setCheckState(can ? Qt::Unchecked : Qt::Unchecked);
            if (!can) chk->setText(QStringLiteral("нет SQL"));
            qaFixesTable->setItem(i, 0, chk);
            qaFixesTable->setItem(i, 1, new QTableWidgetItem(f.bot));
            qaFixesTable->setItem(i, 2, new QTableWidgetItem(f.problem));
            auto *sqlItem = new QTableWidgetItem(f.sql.isEmpty() ? QStringLiteral("(нужны данные с ретейла)") : f.sql);
            sqlItem->setToolTip(f.retail + QLatin1Char('\n') + f.sql);
            qaFixesTable->setItem(i, 3, sqlItem);
        }
    }
    addLog(QStringLiteral("QA: 5 ботов, предложений %1.").arg(m_qaFixes.size()));
    QString dump;
    if (qaReport) dump += qaReport->toPlainText();
    for (const QaFix &f : m_qaFixes)
        dump += QStringLiteral("\n[%1] %2\nSQL:\n%3\n").arg(f.bot, f.problem, f.sql);
    persistErrorLog(QStringLiteral("qa"),
                    QStringLiteral("QA run, предложений %1").arg(m_qaFixes.size()),
                    dump);
}

void MainWindow::applySelectedQaFixes() {
    if (!m_db.isOpen()) {
        QMessageBox::warning(this, QStringLiteral("QA"), QStringLiteral("Нет подключения к MySQL."));
        return;
    }
    QVector<int> picked;
    if (qaFixesTable) {
        for (int i = 0; i < qaFixesTable->rowCount() && i < m_qaFixes.size(); ++i) {
            auto *it = qaFixesTable->item(i, 0);
            if (it && it->checkState() == Qt::Checked) picked << i;
        }
    }
    if (picked.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("QA"),
                                 QStringLiteral("Отметьте решения с SQL, с которыми согласны."));
        return;
    }
    QString preview;
    for (int i : picked) preview += m_qaFixes[i].sql + QLatin1Char('\n');
    if (askScrollDialog(this, QStringLiteral("Внести в базу?"),
                        QStringLiteral("Будет выполнено запросов: %1. Кнопки внизу: «Внести» или «Отмена».")
                            .arg(picked.size()),
                        preview,
                        {QStringLiteral("Внести"), QStringLiteral("Отмена")})
        != 0)
        return;
    int okN = 0, failN = 0;
    QString errors;
    for (int i : picked) {
        QString sql = m_qaFixes[i].sql.trimmed();
        if (sql.startsWith(QLatin1Char('-'))) continue;
        const QStringList parts = sql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QString part : parts) {
            part = part.trimmed();
            if (part.isEmpty() || part.startsWith(QLatin1Char('-'))) continue;
            QString err;
            if (!m_db.execute(part, &err)) {
                ++failN;
                errors += err + QLatin1Char('\n');
            } else {
                ++okN;
            }
        }
    }
    addLog(QStringLiteral("QA apply: ok=%1 fail=%2").arg(okN).arg(failN));
    persistErrorLog(QStringLiteral("qa"),
                    QStringLiteral("QA apply ok=%1 fail=%2").arg(okN).arg(failN),
                    failN ? errors : QStringLiteral("без ошибок"));
    if (failN)
        askScrollDialog(this, QStringLiteral("QA"),
                        QStringLiteral("Успешно: %1. Ошибок: %2. Лог: папка logs у Studio.").arg(okN).arg(failN),
                        errors, {QStringLiteral("OK")});
    else
        askScrollDialog(this, QStringLiteral("QA"),
                        QStringLiteral("Внесено в базу: %1 запрос(ов). Перезапустите/reload ядра при необходимости.").arg(okN),
                        QString(), {QStringLiteral("OK")});
}

void MainWindow::showPerformanceDialog() {
    m_memKb = processMemoryKb();
    const QString mem = (m_memKb > 0) ? QString::number(double(m_memKb)/1024.0, 'f', 1) + QStringLiteral(" МБ") : QStringLiteral("недоступно на этой ОС");
    QString s;
    s += QStringLiteral("===== WoW DB Studio: производительность и ресурсы =====\n\n");
    s += QStringLiteral("Построение интерфейса: %1 мс\n").arg(m_startupMs);
    s += QStringLiteral("Таблиц в активной базе: %1\n").arg(m_tableCount);
    s += QStringLiteral("Последний запрос списка таблиц: %1\n").arg(m_lastDbMs >= 0 ? QString::number(m_lastDbMs) + QStringLiteral(" мс") : QStringLiteral("не выполнялся"));
    s += QStringLiteral("Память процесса (рабочее множество): %1\n").arg(mem);
    s += QStringLiteral("Записей в базе знаний Джарвиса: %1\n").arg(m_knowledge.count());
    s += QStringLiteral("Qt: %1, %2-bit\n").arg(QString::fromLatin1(qVersion())).arg(int(sizeof(void*) * 8));
    s += QStringLiteral("\nЧто уже оптимизировано:\n");
    s += QStringLiteral("• Логи (журнал, консоль, ядро/реалм) ограничены ~1500 строк — память не растёт бесконечно.\n");
    s += QStringLiteral("• Фоновые сетевые проверки стартуют после открытия окна (быстрый старт).\n");
    s += QStringLiteral("• Тяжёлые данные (таблицы, колонки, SmartAI, схема) читаются только по запросу.\n");
    if (m_tableCount > 20000)
        s += QStringLiteral("• В базе очень много таблиц — при ручных SQL используйте LIMIT и WHERE по ключам.\n");

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Производительность и ресурсы"));
    dlg.resize(560, 430);
    auto *vl = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit; te->setReadOnly(true); te->setPlainText(s);
    auto *bb = new QDialogButtonBox;
    auto *refresh = bb->addButton(QStringLiteral("Обновить"), QDialogButtonBox::ActionRole);
    auto *closeBtn = bb->addButton(QDialogButtonBox::Close);
    vl->addWidget(te); vl->addWidget(bb);
    connect(refresh, &QAbstractButton::clicked, this, [this, te]{
        m_memKb = processMemoryKb();
        te->append(QStringLiteral("Память: %1 | таблиц: %2 | запрос таблиц: %3")
                       .arg(m_memKb>0 ? QString::number(double(m_memKb)/1024.0,'f',1)+QStringLiteral(" МБ") : QStringLiteral("н/д"))
                       .arg(m_tableCount)
                       .arg(m_lastDbMs>=0 ? QString::number(m_lastDbMs)+QStringLiteral(" мс") : QStringLiteral("н/д")));
    });
    connect(closeBtn, &QAbstractButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
}


void MainWindow::loadServerSettings() {
    QSettings s("WoWDBStudio","WoWDBStudio");
    if (worldPath) worldPath->setText(s.value("server/world").toString());
    if (realmPath) realmPath->setText(s.value("server/realm").toString());
    if (serverOwnConsole) serverOwnConsole->setChecked(s.value("server/ownConsole", false).toBool());
    if (raHost) raHost->setText(s.value("server/raHost", "127.0.0.1").toString());
    if (raPort) raPort->setValue(s.value("server/raPort", 3443).toInt());
    if (raUser) raUser->setText(s.value("server/raUser").toString());
}

void MainWindow::saveServerSettings() {
    QSettings s("WoWDBStudio","WoWDBStudio");
    if (worldPath) s.setValue("server/world", worldPath->text());
    if (realmPath) s.setValue("server/realm", realmPath->text());
    if (serverOwnConsole) s.setValue("server/ownConsole", serverOwnConsole->isChecked());
    if (raHost) s.setValue("server/raHost", raHost->text());
    if (raPort) s.setValue("server/raPort", raPort->value());
    if (raUser) s.setValue("server/raUser", raUser->text());
}

void MainWindow::fillWagoLocalTables(const QStringList &list) {
    if (!wagoLocalTable) return;
    const auto current = wagoLocalTable->currentText();
    const bool blocked = wagoLocalTable->blockSignals(true);
    wagoLocalTable->clear();
    wagoLocalTable->addItems(list);
    int i = wagoLocalTable->findText(current, Qt::MatchFixedString);
    if (i < 0) i = wagoLocalTable->findText(WagoService::resolveTableName(current, list), Qt::MatchFixedString);
    if (i >= 0) wagoLocalTable->setCurrentIndex(i);
    else if (!current.isEmpty()) { wagoLocalTable->setEditText(current); }
    wagoLocalTable->blockSignals(blocked);
    updateWagoNameMatch();
}

QString MainWindow::hotfixSchema() const {
    return wagoHotfixDb ? wagoHotfixDb->currentText().trimmed() : QStringLiteral("hotfixes");
}

void MainWindow::selectHotfixDatabase(const QStringList &dbs) {
    if (!wagoHotfixDb) return;
    const auto previous = wagoHotfixDb->currentText();
    const bool blocked = wagoHotfixDb->blockSignals(true);
    wagoHotfixDb->clear();
    wagoHotfixDb->addItems(dbs);
    int i = wagoHotfixDb->findText(QStringLiteral("hotfixes"), Qt::MatchFixedString);
    if (i < 0) {
        for (int k = 0; k < dbs.size(); ++k) {
            if (dbs.at(k).contains(QLatin1String("hotfix"), Qt::CaseInsensitive)) { i = k; break; }
        }
    }
    if (i < 0) i = wagoHotfixDb->findText(previous, Qt::MatchFixedString);
    if (i >= 0) wagoHotfixDb->setCurrentIndex(i);
    else wagoHotfixDb->setCurrentText(QStringLiteral("hotfixes"));
    wagoHotfixDb->blockSignals(blocked);
    updateHotfixInfoLabel();
    refreshWagoHotfixTables();
}

void MainWindow::refreshWagoHotfixTables() {
    if (!wagoLocalTable) return;
    if (!m_db.isOpen()) { fillWagoLocalTables({}); return; }
    const QString schema = hotfixSchema();
    if (!safeIdent(schema)) { fillWagoLocalTables({}); return; }
    QSqlQuery q(m_db.database());
    q.prepare("SELECT table_name FROM information_schema.tables WHERE table_schema = ? AND table_type = 'BASE TABLE' ORDER BY table_name");
    q.addBindValue(schema);
    QStringList list;
    if (!q.exec()) {
        addLog("Wago: не удалось прочитать таблицы `" + schema + "`: " + q.lastError().text());
        fillWagoLocalTables({});
        return;
    }
    while (q.next()) list << q.value(0).toString();
    fillWagoLocalTables(list);
    if (list.isEmpty())
        addLog("Wago: в `" + schema + "` нет таблиц. Для DB2 нужна база hotfixes TrinityCore, не world.");
    else
        addLog("Wago: база hotfixes `" + schema + "`, таблиц: " + QString::number(list.size()) + ".");
}

QString MainWindow::collectServerLogFiles() const {
    QStringList dirs;
    auto addDir = [&](const QString &exe) {
        if (exe.trimmed().isEmpty()) return;
        const QString d = QFileInfo(exe).absolutePath();
        if (!d.isEmpty() && !dirs.contains(d)) dirs << d;
    };
    if (worldPath) addDir(worldPath->text());
    if (realmPath) addDir(realmPath->text());
    const QStringList names = {
        "Server.log", "Errors.log", "DBErrors.log", "worldserver.log", "bnetserver.log", "authserver.log",
        "Logs/Server.log", "Logs/Errors.log", "Logs/DBErrors.log",
        "logs/Server.log", "logs/Errors.log", "logs/DBErrors.log"
    };
    auto tail = [](const QString &path, qint64 maxBytes) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        const qint64 sz = f.size();
        if (sz > maxBytes) f.seek(sz - maxBytes);
        return QString::fromLocal8Bit(f.readAll());
    };
    QString out;
    QSet<QString> seen;
    for (const auto &dir : dirs) {
        for (const auto &n : names) {
            const QString p = dir + "/" + n;
            if (!QFileInfo::exists(p) || seen.contains(QFileInfo(p).absoluteFilePath())) continue;
            seen.insert(QFileInfo(p).absoluteFilePath());
            const QString body = tail(p, 12000);
            if (body.trimmed().isEmpty()) continue;
            out += "----- " + QDir::toNativeSeparators(p) + " -----\n" + body.right(12000) + "\n";
        }
        QDir crash(dir + "/Crashes");
        if (!crash.exists()) crash = QDir(dir + "/crashes");
        if (crash.exists()) {
            const auto files = crash.entryInfoList(QStringList() << "*.txt" << "*.log" << "*.dmp", QDir::Files, QDir::Time);
            int n = 0;
            for (const auto &fi : files) {
                if (n >= 3) break;
                if (fi.suffix().compare("dmp", Qt::CaseInsensitive) == 0) {
                    out += "----- crash dump: " + QDir::toNativeSeparators(fi.absoluteFilePath()) + " (бинарный, откройте в VS) -----\n";
                    ++n;
                    continue;
                }
                const QString body = tail(fi.absoluteFilePath(), 8000);
                if (body.trimmed().isEmpty()) continue;
                out += "----- " + QDir::toNativeSeparators(fi.absoluteFilePath()) + " -----\n" + body.right(8000) + "\n";
                ++n;
            }
        }
    }
    return out;
}

void MainWindow::analyzeServerLogs() {
    const QString world = worldLog ? worldLog->toPlainText() : QString();
    const QString realm = realmLog ? realmLog->toPlainText() : QString();
    const QString files = collectServerLogFiles();
    if (world.trimmed().isEmpty() && realm.trimmed().isEmpty() && files.trimmed().isEmpty()) {
        QMessageBox::information(this, "Джарвис",
            "Нет логов. Запустите ядро/реалм из этой вкладки или укажите пути к exe — тогда читаются Server.log / Errors.log / Crashes рядом с ними.\n\n"
            "Если ядро зависло после входа: снимите worldserver.exe в Диспетчере задач (не только закройте окно), затем снова «Джарвис: анализ логов».");
        return;
    }
    const QString coreStr = (core ? core->currentText() : QString()) + " / "
        + (coreBranch ? coreBranch->currentText() : QString()) + " / "
        + (corePatch ? corePatch->text() : QString());
    const auto res = m_jarvis.analyzeServerLogs(world, realm, files, coreStr);
    m_jarvisUrls = res.suggestedUrls;
    QString full;
    for (const auto &step : res.reasoning) full += "▸ " + step + "\n";
    full += "\n";
    full += res.answer;
    if (aiError) {
        QString err = QString::fromUtf8("Логи ядра/реалма (авто):\n");
        err += world.right(4000);
        if (!realm.trimmed().isEmpty()) err += "\n--- реалм ---\n" + realm.right(2000);
        if (!files.trimmed().isEmpty()) err += "\n--- файлы ---\n" + files.right(4000);
        aiError->setPlainText(err);
    }
    if (aiTask && aiTask->toPlainText().trimmed().isEmpty())
        aiTask->setPlainText(QString::fromUtf8("Ядро зависло после входа в игру, при перезапуске ошибка. Разбери логи worldserver/bnetserver."));
    if (aiResponse) aiResponse->setPlainText(full);
    if (auto *tabs = qobject_cast<QTabWidget *>(centralWidget()))
        tabs->setCurrentIndex(7);
    addLog("Джарвис: анализ логов ядра/реалма.");
    persistErrorLog(QStringLiteral("jarvis"), QStringLiteral("Анализ логов ядра/реалма"), full);
}

void MainWindow::reduceCoreMemory() {
    saveServerSettings();
    const QString path = worldPath ? worldPath->text().trimmed() : QString();
    if (path.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("RAM ядра"),
            QString::fromUtf8("Укажите worldserver.exe. Studio правит только worldserver.conf рядом с ним (копия .bak-studio). Exe не патчится."));
        return;
    }
    if (QMessageBox::question(this, QString::fromUtf8("Снизить RAM ядра ~8→4 ГБ"),
            QString::fromUtf8(
                "Записать в worldserver.conf профиль «около 4 ГБ»:\n"
                "• не грузить все карты/гриды при старте (preload = 0)\n"
                "• выгружать пустые гриды через 60 с\n"
                "• visibility континентов 70 (меньше активных NPC)\n"
                "• не кэшировать random-path, waypoint-NPC не держать все активными\n"
                "• выключить metrics/SOAP/LogDB/packet log\n\n"
                "mmap/vmap не отключаются. Debug-сборка 8 ГБ не станет 4 — нужен Release.\n"
                "После записи — полный рестарт worldserver. Продолжить?"))
        != QMessageBox::Yes)
        return;
    QString report;
    if (!ServerLauncher::reduceMemoryLoad(path, &report)) {
        QMessageBox::warning(this, QString::fromUtf8("RAM ядра"), report);
        return;
    }
    addLog(QString::fromUtf8("worldserver.conf: ключи снижения RAM записаны."));
    QMessageBox::information(this, QString::fromUtf8("RAM ядра"), report);
}

void MainWindow::setOnline(bool ok,const QString &message){statusBar()->showMessage(message);addLog(message);}
void MainWindow::addLog(const QString &s){log->append("["+QTime::currentTime().toString("HH:mm:ss")+"] "+s); capLog(log);}

void MainWindow::appendLiveLog(QTextEdit *te, const QString &s, const QString &fileCategory) {
    if (s.isEmpty()) return;
    QString t = s;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.remove(QLatin1Char('\r'));
    if (!t.endsWith(QLatin1Char('\n'))) t += QLatin1Char('\n');
    if (te) {
        const bool atEnd = te->verticalScrollBar()
            ? te->verticalScrollBar()->value() >= te->verticalScrollBar()->maximum() - 8
            : true;
        QTextCursor c = te->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertText(t);
        if (atEnd) {
            c.movePosition(QTextCursor::End);
            te->setTextCursor(c);
            te->ensureCursorVisible();
        }
        capLog(te, 4000);
    }
    if (!fileCategory.isEmpty())
        StudioFileLog::append(fileCategory, t);
}

void MainWindow::persistErrorLog(const QString &category, const QString &title, const QString &body) {
    QString block = QStringLiteral("[%1] %2\n%3\n")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                             title, body);
    const QString path = StudioFileLog::append(category, block);
    StudioFileLog::append(QStringLiteral("errors"), block);
    if (!path.isEmpty())
        addLog(QString::fromUtf8("Лог записан: %1").arg(QDir::toNativeSeparators(path)));
}

// ---------------------------------------------------------------------------
// Вкладка «Патч Wow.exe» — режим Firestorm: самостоятельный exe на диске
// ---------------------------------------------------------------------------
void MainWindow::buildWowPatchTab(QTabWidget *tabs) {
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->addWidget(wrapLabel(ClientPatchService::methodSummary()));

    // -----------------------------------------------------------------------
    // 1. Клиент
    // -----------------------------------------------------------------------
    auto *clientBox = new QGroupBox(QStringLiteral("1. Клиент"));
    auto *clientForm = new QFormLayout(clientBox);

    wowExePath = new QLineEdit;
    wowExePath->setPlaceholderText(R"(C:\Games\World of Warcraft\_retail_\Wow.exe)");
    auto *chooseExe = button(QStringLiteral("Выбрать Wow.exe…"));
    auto *exeRow = new QHBoxLayout;
    exeRow->addWidget(wowExePath, 1);
    exeRow->addWidget(chooseExe);
    clientForm->addRow(QStringLiteral("Wow.exe:"), exeRow);

    wowDataInfo = wrapLabel(QStringLiteral("Папка Data ищется рядом с Wow.exe и уровнем выше. Выберите файл — результат появится здесь."));
    clientForm->addRow(QStringLiteral("Папка Data:"), wowDataInfo);

    wowOutputExe = new QLineEdit;
    wowOutputExe->setPlaceholderText(QStringLiteral("Пусто = писать поверх выбранного Wow.exe (оригинал сохранится в Wow.exe.orig)"));
    auto *chooseOut = button(QStringLiteral("Сохранить как…"));
    auto *likeFirestorm = button(QStringLiteral("Как у Firestorm: рядом и с тем же именем"));
    auto *outRow = new QHBoxLayout;
    outRow->addWidget(wowOutputExe, 1);
    outRow->addWidget(chooseOut);
    outRow->addWidget(likeFirestorm);
    clientForm->addRow(QStringLiteral("Результат:"), outRow);

    auto *diagRow = new QHBoxLayout;
    auto *diagnose = button(QStringLiteral("Диагностика файла"));
    auto *readPortal = button(QStringLiteral("Портал из Config.wtf"));
    auto *checkTls = button(QStringLiteral("Проверить TLS сервера"));
    auto *selfTest = button(QStringLiteral("Самопроверка ключей"));
    diagRow->addWidget(diagnose);
    diagRow->addWidget(readPortal);
    diagRow->addWidget(checkTls);
    diagRow->addWidget(selfTest);
    diagRow->addStretch();
    clientForm->addRow(QStringLiteral("Проверки:"), diagRow);
    lay->addWidget(clientBox);

    // -----------------------------------------------------------------------
    // 2. Портал и сервер
    // -----------------------------------------------------------------------
    auto *portalBox = new QGroupBox(QStringLiteral("2. Портал (адрес вашего сервера)"));
    auto *portalForm = new QFormLayout(portalBox);
    wowPortal = new QLineEdit;
    wowPortal->setPlaceholderText(QStringLiteral("127.0.0.1:1119  или  logon.ваш-домен.ru"));
    portalForm->addRow(QStringLiteral("Портал:"), wowPortal);
    wowPort = new QSpinBox;
    wowPort->setRange(1, 65535);
    wowPort->setValue(1119);
    portalForm->addRow(QStringLiteral("Порт (если не указан в портале):"), wowPort);

    wowWriteConfigWtf = new QCheckBox(QStringLiteral("Записать SET portal в WTF/Config.wtf — ОСНОВНОЙ способ задать адрес"));
    wowWriteConfigWtf->setChecked(true);
    wowPortalSuffix = new QCheckBox(QStringLiteral("Править суффикс .actual.battle.net -> .actual.<домен ≤10 байт> (страховка: без Config.wtf клиент не уйдёт на Blizzard)"));
    wowPortalSuffix->setChecked(true);
    wowPortalWhole = new QCheckBox(QStringLiteral("Legacy: писать host:port во всё окно суффикса — клиент склеит префикс региона, результат может быть нерабочим"));
    auto *portalOpts = new QVBoxLayout;
    portalOpts->addWidget(wowWriteConfigWtf);
    portalOpts->addWidget(wowPortalSuffix);
    portalOpts->addWidget(wowPortalWhole);
    portalForm->addRow(portalOpts);
    lay->addWidget(portalBox);

    // -----------------------------------------------------------------------
    // 3. Ключи
    // -----------------------------------------------------------------------
    auto *keysBox = new QGroupBox(QStringLiteral("3. Ключи TrinityCore (по умолчанию вшиты правильные — менять не нужно)"));
    auto *keysLayout = new QVBoxLayout(keysBox);
    wowAutoDetect = new QCheckBox(QStringLiteral("Автоопределение профиля по версии и пути (1.13.x / 1.14+ / 2.5.x / 3.4.x / 6.x-8.x / 9.x-10.x / 11.x / 12.x)"));
    wowAutoDetect->setChecked(true);
    wowRequireEd = new QCheckBox(QStringLiteral("Ed25519 обязателен (11.x/12.x): без него вход в мир обрывается на переходе к шифрованию"));
    wowRequireEd->setChecked(true);
    wowLegacyRsa = new QCheckBox(QStringLiteral("Патчить Signature/GameCrypto RSA (legacy-клиенты 2.5.x/3.4.x/9.x-10.x)"));
    wowLegacyRsa->setChecked(true);
    wowKeysBe = new QCheckBox(QStringLiteral("Ключи в hex заданы в big-endian (формат openssl) — развернуть в little-endian"));
    keysLayout->addWidget(wowAutoDetect);
    keysLayout->addWidget(wowRequireEd);
    keysLayout->addWidget(wowLegacyRsa);
    keysLayout->addWidget(wowKeysBe);

    wowRsaPem = new QLineEdit;
    wowRsaPem->setPlaceholderText(QStringLiteral("Необязательно: PEM с RSA-ключом/сертификатом bnetserver (PKCS#1, PKCS#8, SPKI)"));
    auto *pickRsa = button(QStringLiteral("…"));
    auto *rsaRow = new QHBoxLayout;
    rsaRow->addWidget(wowRsaPem, 1);
    rsaRow->addWidget(pickRsa);
    keysLayout->addWidget(wrapLabel(QStringLiteral("Свой RSA-модуль (PEM):")));
    keysLayout->addLayout(rsaRow);

    wowEdPem = new QLineEdit;
    wowEdPem->setPlaceholderText(QStringLiteral("Необязательно: PEM с ПУБЛИЧНЫМ ключом Ed25519 (PUBLIC KEY или сертификат)"));
    auto *pickEd = button(QStringLiteral("…"));
    auto *edRow = new QHBoxLayout;
    edRow->addWidget(wowEdPem, 1);
    edRow->addWidget(pickEd);
    keysLayout->addWidget(wrapLabel(QStringLiteral("Свой Ed25519 (PEM):")));
    keysLayout->addLayout(edRow);

    wowVersionUrls = new QCheckBox(QStringLiteral("Патчить version/CDN URL (свой CDN)"));
    keysLayout->addWidget(wowVersionUrls);
    wowVersionUrl = new QLineEdit;
    wowVersionUrl->setPlaceholderText(QStringLiteral("http://my.cdn/wow/versions"));
    wowCdnsUrl = new QLineEdit;
    wowCdnsUrl->setPlaceholderText(QStringLiteral("http://my.cdn/wow/cdns"));
    keysLayout->addWidget(wrapLabel(QStringLiteral("Version URL:")));
    keysLayout->addWidget(wowVersionUrl);
    keysLayout->addWidget(wrapLabel(QStringLiteral("CDNs URL:")));
    keysLayout->addWidget(wowCdnsUrl);

    wowLauncherReg = new QCheckBox(QStringLiteral("Подменить ключ реестра лаунчера (Battle.net -> свой лаунчер)"));
    keysLayout->addWidget(wowLauncherReg);
    wowCertBundleUrl = new QLineEdit;
    wowCertBundleUrl->setPlaceholderText(QStringLiteral("Свой URL cert-bundle (bgs-key-fingerprint)"));
    keysLayout->addWidget(wowCertBundleUrl);
    wowCertBundleFile = new QLineEdit;
    wowCertBundleFile->setPlaceholderText(QStringLiteral("Подписанный bundle ({\"Created\":…) — влезает в слот 32761 байт"));
    auto *pickBundle = button(QStringLiteral("…"));
    auto *bundleRow = new QHBoxLayout;
    bundleRow->addWidget(wowCertBundleFile, 1);
    bundleRow->addWidget(pickBundle);
    keysLayout->addLayout(bundleRow);
    lay->addWidget(keysBox);

    // -----------------------------------------------------------------------
    // 4. Рецепты (перенос чужого патча, например Firestorm 11.2.5 -> 12.1.0)
    // -----------------------------------------------------------------------
    auto *recipeBox = new QGroupBox(QStringLiteral("4. Рецепты — перенос чужого патча на свой билд"));
    auto *recipeForm = new QFormLayout(recipeBox);
    wowRecipeOriginal = new QLineEdit;
    wowRecipeOriginal->setPlaceholderText(QStringLiteral("Чистый Wow.exe того же билда, что и пропатченный"));
    auto *pickRecipeOrig = button(QStringLiteral("…"));
    auto *roRow = new QHBoxLayout;
    roRow->addWidget(wowRecipeOriginal, 1);
    roRow->addWidget(pickRecipeOrig);
    recipeForm->addRow(QStringLiteral("Оригинал:"), roRow);

    wowRecipePatched = new QLineEdit;
    wowRecipePatched->setPlaceholderText(QStringLiteral("Например «WoW 11.2.5 - Firestorm.exe»"));
    auto *pickRecipePatched = button(QStringLiteral("…"));
    auto *rpRow = new QHBoxLayout;
    rpRow->addWidget(wowRecipePatched, 1);
    rpRow->addWidget(pickRecipePatched);
    recipeForm->addRow(QStringLiteral("Пропатченный:"), rpRow);

    wowRecipeOut = new QLineEdit;
    wowRecipeOut->setPlaceholderText(QStringLiteral("Куда сохранить JSON-рецепт"));
    auto *pickRecipeOut = button(QStringLiteral("…"));
    auto *buildRecipe = button(QStringLiteral("Снять рецепт"));
    auto *ro2Row = new QHBoxLayout;
    ro2Row->addWidget(wowRecipeOut, 1);
    ro2Row->addWidget(pickRecipeOut);
    ro2Row->addWidget(buildRecipe);
    recipeForm->addRow(QStringLiteral("Рецепт:"), ro2Row);

    wowRecipeApply = new QLineEdit;
    wowRecipeApply->setPlaceholderText(QStringLiteral("JSON-рецепты через «;» — применяются ДО сигнатурных патчей"));
    auto *pickRecipeApply = button(QStringLiteral("…"));
    auto *applyRecipe = button(QStringLiteral("Применить только рецепт"));
    auto *raRow = new QHBoxLayout;
    raRow->addWidget(wowRecipeApply, 1);
    raRow->addWidget(pickRecipeApply);
    raRow->addWidget(applyRecipe);
    recipeForm->addRow(QStringLiteral("Применить:"), raRow);
    lay->addWidget(recipeBox);

    // -----------------------------------------------------------------------
    // 5. Что сделать и как сохранить
    // -----------------------------------------------------------------------
    auto *runBox = new QGroupBox(QStringLiteral("5. Патч"));
    auto *runLayout = new QVBoxLayout(runBox);
    auto *actions = new QHBoxLayout;
    auto *patchDisk = button(QStringLiteral("Пропатчить на диске (Firestorm-стиль)"));
    auto *patchMemory = button(QStringLiteral("Пропатчить и запустить (память, Arctium)"));
    auto *launchOnly = button(QStringLiteral("Просто запустить результат"));
    actions->addWidget(patchDisk);
    actions->addWidget(patchMemory);
    actions->addWidget(launchOnly);
    actions->addStretch();
    runLayout->addLayout(actions);

    wowBackup = new QCheckBox(QStringLiteral("Делать резервную копию (.orig при записи поверх, .bak при записи в другой файл)"));
    wowBackup->setChecked(true);
    wowVerify = new QCheckBox(QStringLiteral("Перепроверить результат: перечитать файл с диска и подтвердить каждый патч"));
    wowVerify->setChecked(true);
    wowFixChecksum = new QCheckBox(QStringLiteral("Пересчитать PE CheckSum (Windows его не проверяет, но заголовок станет «чистым»)"));
    wowStripSig = new QCheckBox(QStringLiteral("Снять Authenticode-подпись (после правки байт она всё равно невалидна)"));
    wowLaunchAfter = new QCheckBox(QStringLiteral("Запустить клиент сразу после патча"));
    wowCheckTls = new QCheckBox(QStringLiteral("Проверить TLS-сертификат сервера перед запуском (режим «память»)"));
    wowBypassCert = new QCheckBox(QStringLiteral("Обход проверки сертификата в памяти (фаза B, для legacy-профилей)"));
    wowBypassCert->setChecked(true);
    wowExpandPortal = new QCheckBox(QStringLiteral("Память: расширять окно под строкой портала (до 128 Б) — только если проверен расклад строк билда"));
    runLayout->addWidget(wowBackup);
    runLayout->addWidget(wowVerify);
    runLayout->addWidget(wowFixChecksum);
    runLayout->addWidget(wowStripSig);
    runLayout->addWidget(wowLaunchAfter);
    runLayout->addWidget(wowCheckTls);
    runLayout->addWidget(wowBypassCert);
    runLayout->addWidget(wowExpandPortal);

    auto *waitRow = new QHBoxLayout;
    wowWaitUnpackMs = new QSpinBox;
    wowWaitUnpackMs->setRange(1000, 60000);
    wowWaitUnpackMs->setValue(6000);
    wowWaitUnpackMs->setSuffix(QStringLiteral(" мс"));
    waitRow->addWidget(wrapLabel(QStringLiteral("Ожидание расшифровки .text (режим «память», фаза B):")));
    waitRow->addWidget(wowWaitUnpackMs);
    waitRow->addStretch();
    runLayout->addLayout(waitRow);

    wowExtraArgs = new QLineEdit;
    wowExtraArgs->setPlaceholderText(QStringLiteral("Дополнительные аргументы Wow.exe (необязательно)"));
    runLayout->addWidget(wrapLabel(QStringLiteral("Аргументы игры:")));
    runLayout->addWidget(wowExtraArgs);
    lay->addWidget(runBox);

    wowModeHint = wrapLabel(QStringLiteral(
        "Порядок действий: (1) выберите _retail_\\Wow.exe — вкладка покажет версию, профиль, папку Data и все сайты патча; "
        "(2) укажите портал (IP:порт или домен) — он попадёт в SET portal в WTF/Config.wtf; "
        "(3) нажмите «Пропатчить на диске». Результат — самостоятельный exe: запускается двойным щелчком, лаунчер не нужен. "
        "Оригинал сохраняется рядом как Wow.exe.orig. Правки идут ТОЛЬКО в секции данных (.rdata/.data): код у ретейла "
        "упакован и восстанавливается при запуске, поэтому .text на диске не трогается."));
    lay->addWidget(wowModeHint);

    wowLog = new QTextEdit;
    wowLog->setReadOnly(true);
    wowLog->setPlaceholderText(QStringLiteral("Здесь будет подробный лог: профиль, PE, секции, каждый патч со смещением и RVA, проверка результата."));
    lay->addWidget(wowLog, 1);
    tabs->addTab(scrollWrap(page), QStringLiteral("Патч Wow.exe"));

    connect(chooseExe, &QPushButton::clicked, this, &MainWindow::wowChooseExe);
    connect(chooseOut, &QPushButton::clicked, this, [this] {
        wowPickFile(wowOutputExe, QStringLiteral("Куда сохранить пропатченный Wow.exe"),
                    QStringLiteral("Executable (*.exe);;All files (*)"), /*saveMode=*/true);
    });
    connect(likeFirestorm, &QPushButton::clicked, this, &MainWindow::wowSetOutputLikeFirestorm);
    connect(diagnose, &QPushButton::clicked, this, &MainWindow::wowDiagnose);
    connect(readPortal, &QPushButton::clicked, this, &MainWindow::wowReadPortal);
    connect(checkTls, &QPushButton::clicked, this, &MainWindow::wowRunTlsCheck);
    connect(selfTest, &QPushButton::clicked, this, &MainWindow::wowKeysSelfTest);
    connect(pickRsa, &QPushButton::clicked, this, [this] {
        wowPickFile(wowRsaPem, QStringLiteral("PEM с RSA-ключом или сертификатом"),
                    QStringLiteral("PEM (*.pem *.key *.crt *.cer);;All files (*)"), false);
    });
    connect(pickEd, &QPushButton::clicked, this, [this] {
        wowPickFile(wowEdPem, QStringLiteral("PEM с публичным ключом Ed25519"),
                    QStringLiteral("PEM (*.pem *.key *.crt *.cer);;All files (*)"), false);
    });
    connect(pickBundle, &QPushButton::clicked, this, [this] {
        wowPickFile(wowCertBundleFile, QStringLiteral("Подписанный cert-bundle"),
                    QStringLiteral("JSON (*.json);;All files (*)"), false);
    });
    connect(pickRecipeOrig, &QPushButton::clicked, this, [this] {
        wowPickFile(wowRecipeOriginal, QStringLiteral("Чистый оригинал того же билда"),
                    QStringLiteral("Executable (*.exe);;All files (*)"), false);
    });
    connect(pickRecipePatched, &QPushButton::clicked, this, [this] {
        wowPickFile(wowRecipePatched, QStringLiteral("Уже пропатченный клиент"),
                    QStringLiteral("Executable (*.exe);;All files (*)"), false);
    });
    connect(pickRecipeOut, &QPushButton::clicked, this, [this] {
        wowPickFile(wowRecipeOut, QStringLiteral("Куда сохранить рецепт"),
                    QStringLiteral("JSON (*.json);;All files (*)"), true);
    });
    connect(pickRecipeApply, &QPushButton::clicked, this, [this] {
        wowPickFile(wowRecipeApply, QStringLiteral("JSON-рецепт"),
                    QStringLiteral("JSON (*.json);;All files (*)"), false);
    });
    connect(buildRecipe, &QPushButton::clicked, this, &MainWindow::wowBuildRecipe);
    connect(applyRecipe, &QPushButton::clicked, this, &MainWindow::wowApplyRecipe);
    connect(patchDisk, &QPushButton::clicked, this, &MainWindow::wowPatchFile);
    connect(patchMemory, &QPushButton::clicked, this, &MainWindow::wowPatchMemory);
    connect(launchOnly, &QPushButton::clicked, this, &MainWindow::wowLaunchResult);
}

// ---------------------------------------------------------------------------
// Фоновый запуск тяжёлых операций: лог и ошибка возвращаются в UI-поток.
// ---------------------------------------------------------------------------
void MainWindow::wowRunBackground(const QString &title, const QString &okText,
                                  const std::function<bool (QStringList *, QString *)> &job,
                                  const std::function<void ()> &after) {
    if (m_wowPatchBusy) {
        QMessageBox::information(this, title,
            QStringLiteral("Предыдущая операция ещё выполняется — дождитесь её завершения."));
        return;
    }
    m_wowPatchBusy = true;
    wowLog->append(QStringLiteral("=== %1 ===").arg(title));
    auto *worker = QThread::create([this, title, okText, job, after]() {
        QStringList lines;
        QString err;
        const bool ok = job(&lines, &err);
        QMetaObject::invokeMethod(this, [this, ok, lines, err, title, okText, after]() {
            m_wowPatchBusy = false;
            for (const QString &s : lines) wowLog->append(s);
            if (!ok) {
                const QString body = err.isEmpty() ? lines.join(QLatin1Char('\n')) : err;
                QMessageBox::critical(this, title,
                    err.isEmpty() ? QStringLiteral("Операция не выполнена. Подробности в логе ниже.") : err);
                persistErrorLog(QStringLiteral("wowpatch"), title, body);
            } else if (!okText.isEmpty()) {
                QMessageBox::information(this, title, okText);
            }
            if (after) after();
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

// ---------------------------------------------------------------------------
// Сбор опций из полей вкладки
// ---------------------------------------------------------------------------
WowPatchOptions MainWindow::wowCollectOptions() const {
    WowPatchOptions o;
    o.portal = wowPortal->text().trimmed();
    o.port = wowPort->value();
    o.writeConfigWtf = wowWriteConfigWtf->isChecked();
    o.patchPortalSuffix = wowPortalSuffix->isChecked();
    o.patchPortalWholeSlot = wowPortalWhole->isChecked();
    o.expandPortalBuffer = wowExpandPortal->isChecked();
    o.autoDetect = wowAutoDetect->isChecked();
    o.requireEd25519 = wowRequireEd->isChecked();
    o.patchLegacyGameCryptoRsa = wowLegacyRsa->isChecked();
    o.keysAssumeBigEndian = wowKeysBe->isChecked();
    o.rsaPrivatePemPath = QDir::fromNativeSeparators(wowRsaPem->text().trimmed());
    o.ed25519PemPath = QDir::fromNativeSeparators(wowEdPem->text().trimmed());
    o.patchVersionUrls = wowVersionUrls->isChecked();
    o.versionUrl = wowVersionUrl->text().trimmed();
    o.cdnsUrl = wowCdnsUrl->text().trimmed();
    o.patchLauncherRegistry = wowLauncherReg->isChecked();
    o.certBundleUrl = wowCertBundleUrl->text().trimmed();
    o.certBundlePath = QDir::fromNativeSeparators(wowCertBundleFile->text().trimmed());
    o.makeBackup = wowBackup->isChecked();
    o.verifyAfterWrite = wowVerify->isChecked();
    o.fixChecksum = wowFixChecksum->isChecked();
    o.stripSignature = wowStripSig->isChecked();
    o.launchAfterPatch = wowLaunchAfter->isChecked();
    o.bypassCertValidation = wowBypassCert->isChecked();
    o.checkTlsBeforeLaunch = wowCheckTls->isChecked();
    o.waitUnpackMs = wowWaitUnpackMs->value();
    o.extraArgs = wowExtraArgs->text().trimmed();
    const QStringList recipes = wowRecipeApply->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &r : recipes) {
        const QString path = QDir::fromNativeSeparators(r.trimmed());
        if (!path.isEmpty()) o.recipePaths << path;
    }
    return o;
}

void MainWindow::wowPickFile(QLineEdit *target, const QString &title, const QString &filter, bool saveMode) {
    if (!target) return;
    QString start = target->text().trimmed();
    if (start.isEmpty()) start = wowExePath->text().trimmed();
    const QString dir = start.isEmpty() ? QString() : QFileInfo(QDir::fromNativeSeparators(start)).absolutePath();
    const QString picked = saveMode ? QFileDialog::getSaveFileName(this, title, dir, filter)
                                    : QFileDialog::getOpenFileName(this, title, dir, filter);
    if (!picked.isEmpty()) target->setText(QDir::toNativeSeparators(picked));
}

void MainWindow::wowSetOutputLikeFirestorm() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Сначала выберите Wow.exe."));
        return;
    }
    wowOutputExe->setText(QDir::toNativeSeparators(ClientPatchService::defaultOutputPath(exe)));
    wowLog->append(QStringLiteral("Результат будет записан поверх %1 (оригинал сохранится в %2). "
                                  "Именно так сделан «WoW 11.2.5 - Firestorm.exe»: тот же размер, то же имя, "
                                  "та же папка — поэтому клиент находит свои Data и WTF.")
                       .arg(QDir::toNativeSeparators(exe), QDir::toNativeSeparators(exe + QStringLiteral(".orig"))));
}

// ---------------------------------------------------------------------------
// Выбор клиента
// ---------------------------------------------------------------------------
void MainWindow::wowChooseExe() {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите Wow.exe"), {},
                                                   QStringLiteral("Executable (*.exe);;All files (*)"));
    if (f.isEmpty()) return;
    wowExePath->setText(QDir::toNativeSeparators(f));
    if (wowOutputExe->text().isEmpty())
        wowOutputExe->setText(QDir::toNativeSeparators(ClientPatchService::defaultOutputPath(f)));
    if (wowRecipeOriginal->text().isEmpty())
        wowRecipeOriginal->setText(QDir::toNativeSeparators(f));

    wowLog->clear();
    QString err;
    const QString portal = ClientPatchService::readPortalFromConfigWtf(f, &err);
    if (!portal.isEmpty() && wowPortal->text().trimmed().isEmpty()) wowPortal->setText(portal);
    const WowDetection det = ClientPatchService::detectProfile(f);
    wowLog->append(QStringLiteral("Версия клиента: %1   профиль [%2], ветка %3")
                       .arg(det.version.isEmpty() ? QStringLiteral("?") : det.version, det.profile, det.branch));
    if (!det.note.isEmpty()) wowLog->append(QStringLiteral("  %1").arg(det.note));
    bool hasData = false;
    const QString dataDir = QDir::toNativeSeparators(ClientPatchService::clientDataDir(f, &hasData));
    wowDataInfo->setText(QStringLiteral("Data: %1 — %2").arg(dataDir,
        hasData ? QStringLiteral("найдена") : QStringLiteral("НЕ НАЙДЕНА (нужна рядом с Wow.exe)")));
    wowLog->append(QStringLiteral("Папка Data: %1%2").arg(dataDir,
                   hasData ? QStringLiteral(" — найдена") : QStringLiteral(" — НЕ НАЙДЕНА")));
    if (!portal.isEmpty()) wowLog->append(QStringLiteral("SET portal в Config.wtf: %1").arg(portal));
    else if (!err.isEmpty()) wowLog->append(QStringLiteral("Config.wtf: %1").arg(err));
    wowLog->append(QStringLiteral("Нажмите «Диагностика файла», чтобы увидеть секции PE и все сайты патча."));
}

// ---------------------------------------------------------------------------
// Диагностика
// ---------------------------------------------------------------------------
void MainWindow::wowDiagnose() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Сначала выберите Wow.exe."));
        return;
    }
    if (!QFileInfo::exists(exe)) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Файл не найден: %1").arg(exe));
        return;
    }
    auto result = std::make_shared<WowInspect>();
    wowLog->clear();
    wowRunBackground(QStringLiteral("Диагностика Wow.exe"), QString(),
        [exe, result](QStringList *lines, QString *error) {
            *result = ClientPatchService::inspectExecutable(exe);
            if (lines) *lines = result->lines;
            if (!result->ok && error) *error = result->error;
            return result->ok;
        },
        [this, result, exe]() {
            bool hasData = result->dataDirFound;
            wowDataInfo->setText(QStringLiteral("Data: %1 — %2").arg(QDir::toNativeSeparators(result->dataDir),
                hasData ? QStringLiteral("найдена") : QStringLiteral("НЕ НАЙДЕНА (нужна рядом с Wow.exe)")));
            if (!result->configPortal.isEmpty() && wowPortal->text().trimmed().isEmpty())
                wowPortal->setText(result->configPortal);
            if (wowOutputExe->text().trimmed().isEmpty())
                wowOutputExe->setText(QDir::toNativeSeparators(ClientPatchService::defaultOutputPath(exe)));
            const QString state = result->trinityRsaFound
                ? QStringLiteral("файл УЖЕ пропатчен (ключ TrinityCore на месте)")
                : (result->blizzardRsaFound ? QStringLiteral("стоит родной ключ Blizzard — файл не пропатчен")
                                            : QStringLiteral("родной ключ Blizzard не найден — проверьте, тот ли это Wow.exe"));
            wowLog->append(QStringLiteral("Вывод: %1.").arg(state));
        });
}

void MainWindow::wowKeysSelfTest() {
    QStringList lines;
    const bool ok = ClientPatchService::keysSelfTest(&lines);
    for (const QString &s : lines) wowLog->append(s);
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("Ключи TrinityCore"),
                             QStringLiteral("Встроенные ключи НЕ совпали с константами TrinityCore. Смотрите строки [!!] в логе."));
}

void MainWindow::wowReadPortal() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Сначала выберите Wow.exe."));
        return;
    }
    QString err;
    const QString portal = ClientPatchService::readPortalFromConfigWtf(exe, &err);
    if (portal.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), err);
        return;
    }
    wowPortal->setText(portal);
    wowLog->append(QStringLiteral("Портал из Config.wtf: %1").arg(portal));
}

void MainWindow::wowRunTlsCheck() {
    QString portal = wowPortal->text().trimmed();
    QString host = portal;
    int port = wowPort->value();
    if (int i = portal.lastIndexOf(QLatin1Char(':')); i > 0) {
        host = portal.left(i);
        bool ok = false;
        const int p = portal.mid(i + 1).toInt(&ok);
        if (ok) port = p;
    }
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Укажите портал."));
        return;
    }
    QStringList lines;
    const bool ok = ClientPatchService::checkPortalTls(host, port, &lines);
    for (const QString &l : lines) wowLog->append(l);
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("TLS-проверка"),
                             QStringLiteral("Сервер недоступен или сертификат не прошёл проверку.\n%1").arg(lines.join(QLatin1Char('\n'))));
}

// ---------------------------------------------------------------------------
// ОСНОВНОЙ РЕЖИМ: патч на диске (Firestorm-стиль)
// ---------------------------------------------------------------------------
void MainWindow::wowPatchFile() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Сначала выберите Wow.exe."));
        return;
    }
    if (!QFileInfo::exists(exe)) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Файл не найден: %1").arg(exe));
        return;
    }
    const WowPatchOptions opts = wowCollectOptions();
    if (opts.portal.isEmpty() && opts.recipePaths.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"),
            QStringLiteral("Укажите портал (адрес сервера) или рецепт. Без адреса патчить нечего."));
        return;
    }
    QString out = QDir::fromNativeSeparators(wowOutputExe->text().trimmed());
    if (out.isEmpty()) out = ClientPatchService::defaultOutputPath(exe);
    const bool inPlace = QDir::cleanPath(out) == QDir::cleanPath(exe);

    if (inPlace) {
        const QString bak = out + QStringLiteral(".orig");
        const auto r = QMessageBox::question(this, QStringLiteral("Патч Wow.exe"),
            QStringLiteral("Патчим %1 ПО ВЕРХ оригинала.\n%2\n\n%3\n\nПродолжить?")
                .arg(QDir::toNativeSeparators(out),
                     opts.makeBackup ? QStringLiteral("Оригинал будет сохранён как %1.").arg(QDir::toNativeSeparators(bak))
                                     : QStringLiteral("ВНИМАНИЕ: резервная копия отключена — оригинал будет потерян."),
                     QFileInfo::exists(bak) ? QStringLiteral("Бэкап %1 уже есть — он НЕ будет перезаписан.")
                                            : QString()));
        if (r != QMessageBox::Yes) return;
    } else if (QFileInfo::exists(out)) {
        const auto r = QMessageBox::question(this, QStringLiteral("Патч Wow.exe"),
            QStringLiteral("Файл %1 уже существует — перезаписать?").arg(QDir::toNativeSeparators(out)));
        if (r != QMessageBox::Yes) return;
    }

    wowLog->clear();
    const QString okText = QStringLiteral("Готово: %1\n\nЭто самостоятельный exe — запускается двойным щелчком, "
                                          "лаунчер не нужен. Адрес сервера берётся из SET portal в Config.wtf рядом с ним.")
                               .arg(QDir::toNativeSeparators(out));
    wowRunBackground(QStringLiteral("Патч на диске (Firestorm-стиль)"), okText,
        [exe, out, opts](QStringList *lines, QString *error) {
            const WowPatchReport rep = ClientPatchService::patchStandalone(exe, out, opts, lines);
            if (!rep.ok && error) *error = rep.error;
            return rep.ok;
        });
}

// ---------------------------------------------------------------------------
// Режим «память» (Arctium): файл не меняется
// ---------------------------------------------------------------------------
void MainWindow::wowPatchMemory() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Сначала выберите Wow.exe."));
        return;
    }
    const WowPatchOptions opts = wowCollectOptions();
    if (opts.portal.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"), QStringLiteral("Укажите портал."));
        return;
    }
    if (m_wowPatchBusy) {
        QMessageBox::information(this, QStringLiteral("Патч Wow.exe"),
                                 QStringLiteral("Патч уже выполняется — подождите, окно должно оставаться отзывчивым."));
        return;
    }

    wowLog->clear();
    if (opts.checkTlsBeforeLaunch) {
        QStringList lines;
        QString host = opts.portal;
        int port = opts.port;
        if (int i = host.lastIndexOf(QLatin1Char(':')); i > 0) {
            const QString h = host.left(i);
            bool ok = false;
            const int p = host.mid(i + 1).toInt(&ok);
            if (ok) { host = h; port = p; }
        }
        const bool tlsOk = ClientPatchService::checkPortalTls(host, port, &lines);
        for (const QString &s : lines) wowLog->append(s);
        if (!tlsOk) {
            QMessageBox::warning(this, QStringLiteral("Патч Wow.exe"),
                                 QStringLiteral("TLS-проверка не пройдена — запуск отменён."));
            return;
        }
    }

    wowLog->append(QStringLiteral("Патч в памяти запущен в фоне (12.x не должен вешать Studio). Не закрывайте окно…"));
    wowRunBackground(QStringLiteral("Патч в памяти (Arctium)"),
        QStringLiteral("Клиент запущен с патчами. Файл на диске не изменён."),
        [exe, opts](QStringList *lines, QString *error) {
            return ClientPatchService::patchAndLaunch(exe, opts, lines, error);
        });
}

// ---------------------------------------------------------------------------
// Рецепты
// ---------------------------------------------------------------------------
void MainWindow::wowBuildRecipe() {
    const QString orig = QDir::fromNativeSeparators(wowRecipeOriginal->text().trimmed());
    const QString patched = QDir::fromNativeSeparators(wowRecipePatched->text().trimmed());
    if (orig.isEmpty() || patched.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Рецепт"),
            QStringLiteral("Нужны ОБА файла: чистый оригинал того же билда и уже пропатченный клиент."));
        return;
    }
    QString out = QDir::fromNativeSeparators(wowRecipeOut->text().trimmed());
    if (out.isEmpty()) {
        out = QFileInfo(patched).absolutePath() + QDir::separator() +
              QStringLiteral("recipe-") + QFileInfo(patched).completeBaseName() + QStringLiteral(".json");
        wowRecipeOut->setText(QDir::toNativeSeparators(out));
    }
    wowLog->clear();
    const QString okText = QStringLiteral("Рецепт сохранён: %1\n\nТеперь укажите его в поле «Применить» — hunks "
                                          "будут перенесены на ваш билд поиском по контексту.")
                               .arg(QDir::toNativeSeparators(out));
    wowRunBackground(QStringLiteral("Извлечение рецепта"), okText,
        [this, orig, patched, out](QStringList *lines, QString *error) {
            const WowPatchOptions opts;
            const bool ok = ClientPatchService::buildRecipe(orig, patched, out, opts, lines, error);
            return ok;
        },
        [this, out]() {
            const QString current = wowRecipeApply->text().trimmed();
            const QString native = QDir::toNativeSeparators(out);
            if (current.isEmpty()) wowRecipeApply->setText(native);
            else if (!current.contains(native)) wowRecipeApply->setText(current + QLatin1Char(';') + native);
        });
}

void MainWindow::wowApplyRecipe() {
    const QString exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Рецепт"),
            QStringLiteral("Сначала выберите Wow.exe, к которому применять рецепт."));
        return;
    }
    const WowPatchOptions opts = wowCollectOptions();
    if (opts.recipePaths.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Рецепт"),
            QStringLiteral("Укажите хотя бы один JSON-рецепт в поле «Применить»."));
        return;
    }
    QString out = QDir::fromNativeSeparators(wowOutputExe->text().trimmed());
    if (out.isEmpty()) out = ClientPatchService::defaultOutputPath(exe);

    WowPatchOptions only = opts;
    only.applySignaturePatches = false;   // переносим ровно чужие hunks
    only.writeConfigWtf = false;

    wowLog->clear();
    wowRunBackground(QStringLiteral("Применение рецепта"),
        QStringLiteral("Рецепт перенесён: %1").arg(QDir::toNativeSeparators(out)),
        [exe, out, only](QStringList *lines, QString *error) {
            const WowPatchReport rep = ClientPatchService::patchStandalone(exe, out, only, lines);
            if (!rep.ok && error) *error = rep.error;
            return rep.ok;
        });
}

void MainWindow::wowLaunchResult() {
    QString exe = QDir::fromNativeSeparators(wowOutputExe->text().trimmed());
    if (exe.isEmpty()) exe = QDir::fromNativeSeparators(wowExePath->text().trimmed());
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::warning(this, QStringLiteral("Запуск клиента"),
                             QStringLiteral("Не найден exe для запуска. Сначала пропатчите клиент."));
        return;
    }
    QStringList lines;
    QString err;
    const bool ok = ClientPatchService::launchClient(exe, wowExtraArgs->text().trimmed(), &lines, &err);
    for (const QString &s : lines) wowLog->append(s);
    if (!ok) QMessageBox::critical(this, QStringLiteral("Запуск клиента"), err);
}
void MainWindow::refreshTables(){
    tables->clear();
    QString e;
    QElapsedTimer t; t.start();
    auto list=m_db.tables(&e);
    m_lastDbMs = t.elapsed();
    if(!e.isEmpty()){addLog(e);return;}
    m_tableCount = list.size();
    tables->addItems(list);
}
