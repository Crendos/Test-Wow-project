#include "content_editor_service.h"
#include "database_service.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QVariant>

// --- helpers ---------------------------------------------------------------
static bool looksNumeric(const QString &v) {
    const QString t = v.trimmed();
    if (t.isEmpty()) return false;
    for (const QChar &c : t)
        if (!c.isDigit() && c != QLatin1Char('-') && c != QLatin1Char('.')) return false;
    return true;
}
static QString quoteValue(const QString &v) {
    if (looksNumeric(v)) return v.trimmed();
    QString e = v;
    e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    e.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + e + QLatin1Char('\'');
}
static QString objectiveType(int t) {
    switch (t) {
        case 0: return QStringLiteral("MONSTER (убить)");
        case 1: return QStringLiteral("ITEM (предмет)");
        case 2: return QStringLiteral("GAMEOBJECT (объект)");
        case 3: return QStringLiteral("TALKTO (поговорить)");
        case 4: return QStringLiteral("CURRENCY (валюта)");
        case 5: return QStringLiteral("LEARNSPELL (выучить)");
        case 8: return QStringLiteral("MONEY (деньги)");
        case 9: return QStringLiteral("PLAYERKILLS (PvP)");
        case 10: return QStringLiteral("AREATRIGGER");
        case 14: return QStringLiteral("COMPLETE_CRITERIA_TREE");
        case 15: return QStringLiteral("TASK_IN_ZONE");
        default: return QStringLiteral("TYPE=%1").arg(t);
    }
}

// --- entity specs ----------------------------------------------------------
QVector<ContentEntitySpec> ContentEditorService::specs() {
    QVector<ContentEntitySpec> v;

    ContentEntitySpec item; item.key = QStringLiteral("item"); item.title = QStringLiteral("Предмет"); item.table = QStringLiteral("item_template");
    item.fields = {
        {QStringLiteral("entry"), QStringLiteral("Entry / ID"), QStringLiteral("например 90001"), true},
        {QStringLiteral("name"), QStringLiteral("Название (3.3.5; в Legion — в item_sparse)"), QStringLiteral("Меч Героя"), false},
        {QStringLiteral("class"), QStringLiteral("Класс (2=Weapon,4=Armor)"), QStringLiteral("2"), false},
        {QStringLiteral("subclass"), QStringLiteral("Подкласс"), QStringLiteral("1"), false},
        {QStringLiteral("displayid"), QStringLiteral("DisplayID (модель)"), QStringLiteral("12345"), false},
        {QStringLiteral("InventoryType"), QStringLiteral("InventoryType (слот)"), QStringLiteral("17"), false},
        {QStringLiteral("ItemLevel"), QStringLiteral("ItemLevel"), QStringLiteral("100"), false},
        {QStringLiteral("RequiredLevel"), QStringLiteral("Требуемый уровень"), QStringLiteral("60"), false},
        {QStringLiteral("quality"), QStringLiteral("Качество (0..7)"), QStringLiteral("4"), false},
        {QStringLiteral("AllowableClass"), QStringLiteral("AllowableClass (-1=все)"), QStringLiteral("-1"), false},
    };
    v.append(item);

    ContentEntitySpec npc; npc.key = QStringLiteral("creature"); npc.title = QStringLiteral("NPC / существо"); npc.table = QStringLiteral("creature_template");
    npc.fields = {
        {QStringLiteral("entry"), QStringLiteral("Entry / ID"), QStringLiteral("например 900001"), true},
        {QStringLiteral("name"), QStringLiteral("Имя"), QStringLiteral("Стражник"), false},
        {QStringLiteral("subname"), QStringLiteral("Подимя"), QStringLiteral("Страж города"), false},
        {QStringLiteral("minlevel"), QStringLiteral("Мин. уровень"), QStringLiteral("60"), false},
        {QStringLiteral("maxlevel"), QStringLiteral("Макс. уровень"), QStringLiteral("60"), false},
        {QStringLiteral("faction"), QStringLiteral("Фракция (ID)"), QStringLiteral("11"), false},
        {QStringLiteral("npcflag"), QStringLiteral("npcflag (1=квесты,2=вендор)"), QStringLiteral("3"), false},
        {QStringLiteral("rank"), QStringLiteral("Ранг (0=обычный,3=босс)"), QStringLiteral("0"), false},
        {QStringLiteral("scale"), QStringLiteral("Масштаб"), QStringLiteral("1"), false},
        {QStringLiteral("AIName"), QStringLiteral("AIName (SmartAI)"), QStringLiteral("SmartAI"), false},
    };
    v.append(npc);

    ContentEntitySpec go; go.key = QStringLiteral("gameobject"); go.title = QStringLiteral("Объект (GameObject)"); go.table = QStringLiteral("gameobject_template");
    go.fields = {
        {QStringLiteral("entry"), QStringLiteral("Entry / ID"), QStringLiteral("например 900002"), true},
        {QStringLiteral("type"), QStringLiteral("Тип (2=QUEST,3=CHEST,8=DOOR)"), QStringLiteral("3"), false},
        {QStringLiteral("displayId"), QStringLiteral("DisplayID (модель)"), QStringLiteral("1234"), false},
        {QStringLiteral("name"), QStringLiteral("Название"), QStringLiteral("Сундук"), false},
        {QStringLiteral("faction"), QStringLiteral("Фракция"), QStringLiteral("0"), false},
        {QStringLiteral("size"), QStringLiteral("Размер"), QStringLiteral("1"), false},
    };
    v.append(go);

    ContentEntitySpec quest; quest.key = QStringLiteral("quest"); quest.title = QStringLiteral("Квест"); quest.table = QStringLiteral("quest_template");
    quest.fields = {
        {QStringLiteral("ID"), QStringLiteral("ID квеста"), QStringLiteral("например 90001"), true},
        {QStringLiteral("MinLevel"), QStringLiteral("Мин. уровень"), QStringLiteral("1"), false},
        {QStringLiteral("QuestLevel"), QStringLiteral("Уровень квеста"), QStringLiteral("60"), false},
        {QStringLiteral("QuestType"), QStringLiteral("QuestType (2=обычный)"), QStringLiteral("2"), false},
        {QStringLiteral("AllowableClasses"), QStringLiteral("AllowableClasses (-1=все)"), QStringLiteral("-1"), false},
        {QStringLiteral("Flags"), QStringLiteral("Flags"), QStringLiteral("0"), false},
    };
    v.append(quest);

    ContentEntitySpec loot; loot.key = QStringLiteral("loot"); loot.title = QStringLiteral("Лут существа"); loot.table = QStringLiteral("creature_loot_template");
    loot.fields = {
        {QStringLiteral("entry"), QStringLiteral("Entry NPC"), QStringLiteral("например 900001"), true},
        {QStringLiteral("item"), QStringLiteral("Entry предмета (<0=валюта)"), QStringLiteral("90001"), true},
        {QStringLiteral("ChanceOrQuestChance"), QStringLiteral("Шанс %"), QStringLiteral("50"), false},
        {QStringLiteral("groupid"), QStringLiteral("Группа (0=независимо)"), QStringLiteral("0"), false},
        {QStringLiteral("mincountOrRef"), QStringLiteral("Мин. кол-во (<0=reference)"), QStringLiteral("1"), false},
        {QStringLiteral("maxcount"), QStringLiteral("Макс. кол-во"), QStringLiteral("1"), false},
        {QStringLiteral("shared"), QStringLiteral("shared (0/1, кастомный лут)"), QStringLiteral("0"), false},
    };
    v.append(loot);

    return v;
}

const ContentEntitySpec *ContentEditorService::specByKey(const QString &key) {
    static const QVector<ContentEntitySpec> all = specs();
    for (const auto &s : all) if (s.key == key) return &s;
    return nullptr;
}

// --- live schema -----------------------------------------------------------
QStringList ContentEditorService::tableColumns(DatabaseService &db, const QString &table, bool *ok) {
    QStringList cols;
    if (ok) *ok = false;
    if (!db.isOpen()) return cols;
    QSqlQuery q(db.database());
    if (!q.exec(QStringLiteral("SHOW COLUMNS FROM `%1`").arg(table))) return cols;
    while (q.next()) cols << q.value(0).toString();
    if (ok) *ok = !cols.isEmpty();
    return cols;
}

bool ContentEditorService::tableExists(DatabaseService &db, const QString &table) {
    bool ok = false;
    const auto cols = tableColumns(db, table, &ok);
    return ok && !cols.isEmpty();
}

QStringList ContentEditorService::similarTables(DatabaseService &db, const QString &table) {
    QString err;
    const QStringList all = db.tables(&err);
    const QString t = table.toLower();
    const QString stem = t.section(QLatin1Char('_'), 0, 0);
    QStringList out;
    if (stem.size() >= 4) {
        for (const auto &x : all) {
            const QString xl = x.toLower();
            if (xl == t) continue;
            if (xl.startsWith(stem) || xl.contains(QLatin1Char('_') + stem)) out << x;
            if (out.size() >= 8) break;
        }
    }
    return out;
}

// --- INSERT generation with validation -------------------------------------
QString ContentEditorService::buildInsert(const ContentEntitySpec &spec, const QMap<QString, QString> &values,
                                          DatabaseService *db, QString *report) {
    QString rep;
    bool hadIssue = false;
    QStringList liveCols;
    bool haveSchema = false;

    if (db && db->isOpen()) {
        if (!tableExists(*db, spec.table)) {
            hadIssue = true;
            rep += QStringLiteral("⚠ Таблица `%1` НЕ найдена в подключённой базе.\n").arg(spec.table);
            const auto sim = similarTables(*db, spec.table);
            if (!sim.isEmpty())
                rep += QStringLiteral("  Похожие / возможно совместимые таблицы: %1.\n").arg(sim.join(QStringLiteral(", ")));
            rep += QStringLiteral("  Проверьте, к той ли базе подключены (world?) и та ли ветка ядра.\n");
        } else {
            bool ok = false;
            liveCols = tableColumns(*db, spec.table, &ok);
            haveSchema = ok;
        }
    }

    QStringList cols, vals, missing;
    for (const auto &f : spec.fields) {
        const QString raw = values.value(f.column).trimmed();
        if (raw.isEmpty()) continue;
        if (haveSchema && !liveCols.contains(f.column, Qt::CaseInsensitive)) {
            missing << f.column;
            continue;
        }
        cols << QLatin1Char('`') + f.column + QLatin1Char('`');
        vals << quoteValue(raw);
    }

    if (!missing.isEmpty()) {
        hadIssue = true;
        rep += QStringLiteral("⚠ В таблице `%1` нет колонок: %2 — они пропущены (возможно, ядро другой ветки).\n")
                   .arg(spec.table, missing.join(QStringLiteral(", ")));
    }

    if (cols.isEmpty()) {
        rep += QStringLiteral("✗ Нет ни одного заполненного поля, совместимого со схемой.\n");
        if (report) *report = rep;
        return QString();
    }

    const QString sql = QStringLiteral("INSERT INTO `%1` (%2)\nVALUES (%3);\n")
                            .arg(spec.table, cols.join(QStringLiteral(", ")), vals.join(QStringLiteral(", ")));
    if (!hadIssue)
        rep = QStringLiteral("✓ SQL совместим со схемой подключённой базы.\n");
    if (report) *report = rep;
    return sql;
}

// --- diagnostics -----------------------------------------------------------
QString ContentEditorService::diagnoseQuest(DatabaseService &db, int questId) {
    if (!db.isOpen()) return QStringLiteral("Нет подключения к базе — диагностика недоступна.");
    QSqlDatabase sqlDb = db.database();
    QString out = QStringLiteral("Диагностика квеста ID=%1\n\n").arg(questId);

    QSqlQuery q(sqlDb);
    q.prepare(QStringLiteral("SELECT ID FROM quest_template WHERE ID = ?"));
    q.addBindValue(questId);
    if (!(q.exec() && q.next())) {
        out += QStringLiteral("✗ Квест не найден в quest_template.\n");
        return out;
    }
    out += QStringLiteral("✓ Квест есть в quest_template.\n");

    // Кто выдаёт
    QSqlQuery qs(sqlDb);
    qs.prepare(QStringLiteral("SELECT id FROM creature_queststarter WHERE quest = ?"));
    qs.addBindValue(questId);
    QStringList starters;
    if (qs.exec()) while (qs.next()) starters << qs.value(0).toString();
    if (starters.isEmpty())
        out += QStringLiteral("✗ Квест НЕ привязан ни к одному NPC (creature_queststarter пуст) — при наведении на NPC задание НЕ отобразится.\n")
             + QStringLiteral("  Лечение: INSERT INTO creature_queststarter (id, quest) VALUES (<NPC_ENTRY>, %1);\n").arg(questId);
    else
        out += QStringLiteral("✓ Выдают NPC (creature_queststarter): %1\n").arg(starters.join(QStringLiteral(", ")));

    // Кто принимает
    QSqlQuery qe(sqlDb);
    qe.prepare(QStringLiteral("SELECT id FROM creature_questender WHERE quest = ?"));
    qe.addBindValue(questId);
    QStringList enders;
    if (qe.exec()) while (qe.next()) enders << qe.value(0).toString();
    if (enders.isEmpty())
        out += QStringLiteral("⚠ Квест некому сдавать (creature_questender пуст).\n");
    else
        out += QStringLiteral("✓ Принимают NPC (creature_questender): %1\n").arg(enders.join(QStringLiteral(", ")));

    // Текст (Legion)
    QSqlQuery qt(sqlDb);
    qt.prepare(QStringLiteral("SELECT locale FROM quest_template_locale WHERE ID = ?"));
    qt.addBindValue(questId);
    bool hasLocale = false;
    if (qt.exec()) hasLocale = qt.next();
    if (!hasLocale)
        out += QStringLiteral("⚠ Нет текста в quest_template_locale (в Legion без него квест безымянный).\n");

    // Цели
    out += QStringLiteral("\nЦели (quest_objectives):\n");
    QSqlQuery qo(sqlDb);
    qo.prepare(QStringLiteral("SELECT Type, ObjectID, Amount FROM quest_objectives WHERE QuestID = ?"));
    qo.addBindValue(questId);
    int objCount = 0;
    if (qo.exec()) {
        while (qo.next()) {
            ++objCount;
            const int type = qo.value(0).toInt();
            const int obj = qo.value(1).toInt();
            const int amt = qo.value(2).toInt();
            out += QStringLiteral("• %1: obj=%2 x%3\n").arg(objectiveType(type)).arg(obj).arg(amt);
            if (type == 1) { // ITEM — проверить, выпадает ли предмет
                QSqlQuery ql(sqlDb);
                ql.prepare(QStringLiteral("SELECT entry FROM creature_loot_template WHERE item = ? LIMIT 1"));
                ql.addBindValue(obj);
                if (ql.exec() && ql.next()) {
                    const QString lootEntry = ql.value(0).toString();
                    out += QStringLiteral("   ✓ Предмет %1 есть в луте (entry=%2).\n").arg(obj).arg(lootEntry);
                    // creature_questitem — именно она даёт клиенту подсветку NPC и прогресс 0/N в тултипе.
                    if (tableExists(db, QStringLiteral("creature_questitem"))) {
                        QSqlQuery qi(sqlDb);
                        qi.prepare(QStringLiteral("SELECT COUNT(*) FROM creature_questitem WHERE Item = ?"));
                        qi.addBindValue(obj);
                        bool linked = false;
                        if (qi.exec() && qi.next()) linked = qi.value(0).toInt() > 0;
                        if (linked)
                            out += QStringLiteral("   ✓ creature_questitem: предмет привязан к NPC — клиент подсветит NPC и покажет прогресс в тултипе.\n");
                        else
                            out += QStringLiteral("   ⚠ В creature_questitem нет привязки предмета %1 к NPC %2 — клиент НЕ подсветит NPC и не покажет 0/N в тултипе.\n"
                                                  "     Лечение: INSERT INTO creature_questitem (CreatureEntry, Idx, Item) VALUES (%2, 0, %1);\n"
                                                  "     (имена колонок сверь по DESCRIBE creature_questitem; после — релог клиента)\n").arg(obj).arg(lootEntry);
                    } else {
                        // Нет creature_questitem — пробуем старый механизм creature_template.QuestItem1..6.
                        const QStringList tcols = tableColumns(db, QStringLiteral("creature_template"));
                        QStringList qiCols;
                        for (const QString &c : tcols)
                            if (c.startsWith(QLatin1String("QuestItem"), Qt::CaseInsensitive)) qiCols << c;
                        if (!qiCols.isEmpty()) {
                            QSqlQuery qt2(sqlDb);
                            qt2.prepare(QStringLiteral("SELECT %1 FROM creature_template WHERE entry = ?").arg(qiCols.join(QStringLiteral(", "))));
                            qt2.addBindValue(lootEntry.toInt());
                            bool has = false;
                            if (qt2.exec() && qt2.next())
                                for (int i = 0; i < qiCols.size(); ++i) if (qt2.value(i).toInt() == obj) { has = true; break; }
                            if (has)
                                out += QStringLiteral("   ✓ creature_template.QuestItem*: предмет привязан к NPC — клиент подсветит его и покажет прогресс.\n");
                            else
                                out += QStringLiteral("   ⚠ В creature_template нет QuestItem-привязки предмета %1 для NPC %2 — клиент не подсветит NPC.\n"
                                                      "     Лечение: UPDATE creature_template SET %3 = %1 WHERE entry = %2; (затем .reload creature_template и релог)\n")
                                           .arg(obj).arg(lootEntry).arg(qiCols.first());
                        } else {
                            out += QStringLiteral("   ⚠ В БД (world/hotfixes) нет ни creature_questitem, ни колонок QuestItem* — на Legion эта привязка хранится в клиентских DB2 (CreatureQuestItem.db2): правится через WDBX, либо используй kill-цель (Type=0) для серверной подсветки NPC.\n");
                        }
                    }
                } else
                    out += QStringLiteral("   ✗ Предмет %1 НЕ выпадает ни с кого (creature_loot_template) — игрок не сможет его получить.\n").arg(obj);
            } else if (type == 0) { // MONSTER — существует ли моб
                QSqlQuery qc(sqlDb);
                qc.prepare(QStringLiteral("SELECT entry FROM creature_template WHERE entry = ?"));
                qc.addBindValue(obj);
                if (!(qc.exec() && qc.next()))
                    out += QStringLiteral("   ✗ Моб %1 не найден в creature_template.\n").arg(obj);
            }
        }
    }
    if (objCount == 0)
        out += QStringLiteral("⚠ У квеста нет целей (quest_objectives пуст).\n");

    return out;
}

QString ContentEditorService::diagnoseNpc(DatabaseService &db, int entry) {
    if (!db.isOpen()) return QStringLiteral("Нет подключения к базе — диагностика недоступна.");
    QSqlDatabase sqlDb = db.database();
    QString out = QStringLiteral("Диагностика NPC entry=%1\n\n").arg(entry);

    QSqlQuery q(sqlDb);
    q.prepare(QStringLiteral("SELECT AIName, npcflag FROM creature_template WHERE entry = ?"));
    q.addBindValue(entry);
    if (!(q.exec() && q.next())) {
        out += QStringLiteral("✗ NPC не найден в creature_template.\n");
        return out;
    }
    const QString ai = q.value(0).toString();
    // Имя в Legion лежит в creature_template_locale, а не в creature_template.
    QString name;
    QSqlQuery qn(sqlDb);
    qn.prepare(QStringLiteral("SELECT Name FROM creature_template_locale WHERE ID = ? AND locale IN ('ruRU','enUS') ORDER BY locale DESC LIMIT 1"));
    qn.addBindValue(entry);
    if (qn.exec() && qn.next()) name = qn.value(0).toString();
    if (name.isEmpty()) name = QStringLiteral("(имя — в creature_template_locale)");
    out += QStringLiteral("✓ NPC: %1 (AIName=%2)\n").arg(name, ai.isEmpty() ? QStringLiteral("—") : ai);

    // Квесты, которые он выдаёт
    QSqlQuery qs(sqlDb);
    qs.prepare(QStringLiteral("SELECT quest FROM creature_queststarter WHERE id = ?"));
    qs.addBindValue(entry);
    QStringList quests;
    if (qs.exec()) while (qs.next()) quests << qs.value(0).toString();
    out += quests.isEmpty()
        ? QStringLiteral("• Не выдаёт квестов (creature_queststarter пуст).\n")
        : QStringLiteral("• Выдаёт квесты: %1\n").arg(quests.join(QStringLiteral(", ")));

    // Триггеры SmartAI
    out += QStringLiteral("\nТриггеры SmartAI (smart_scripts):\n");
    QSqlQuery sm(sqlDb);
    sm.prepare(QStringLiteral("SELECT id, event_type, action_type, target_type, comment FROM smart_scripts WHERE entryorguid = ? AND source_type = 0 ORDER BY id"));
    sm.addBindValue(entry);
    int n = 0;
    if (sm.exec()) {
        while (sm.next()) {
            ++n;
            out += QStringLiteral("• #%1 событие=%2 действие=%3 цель=%4  %5\n")
                       .arg(sm.value(0).toString())
                       .arg(sm.value(1).toString())
                       .arg(sm.value(2).toString())
                       .arg(sm.value(3).toString())
                       .arg(sm.value(4).toString());
        }
    }
    if (n == 0)
        out += QStringLiteral("• Триггеров нет. Если NPC должен реагировать (каст, призыв, квест-триггер) — добавьте smart_scripts и AIName='SmartAI'.\n");
    else if (!ai.contains(QStringLiteral("SmartAI"), Qt::CaseInsensitive))
        out += QStringLiteral("⚠ Есть smart_scripts, но AIName != 'SmartAI' — триггеры НЕ сработают.\n");

    return out;
}
