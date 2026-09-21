#include "sql_wizard_service.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringConverter>
#include <algorithm>

QVector<SqlWizardService::Template> SqlWizardService::builtin() {
    QVector<Template> v;

    v.append({QStringLiteral("Лут: добавить предмет к дропу существа"),
        QStringLiteral(
        "-- Лут: независимый дроп (groupid=0). shared=0.\n"
        "DELETE FROM `creature_loot_template` WHERE entry=@LOOT_ENTRY AND item=@ITEM;\n"
        "INSERT INTO `creature_loot_template`\n"
        "(`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`,`shared`)\n"
        "VALUES (@LOOT_ENTRY,@ITEM,@CHANCE,0,0,1,1,0);\n")});

    v.append({QStringLiteral("Лут: группа «один из N» (groupid>0)"),
        QStringLiteral(
        "-- Из группы выпадает РОВНО ОДИН предмет. Веса = ChanceOrQuestChance.\n"
        "DELETE FROM `creature_loot_template` WHERE entry=@LOOT_ENTRY AND groupid=@GROUPID;\n"
        "INSERT INTO `creature_loot_template`\n"
        "(`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`,`shared`)\n"
        "VALUES\n"
        "(@LOOT_ENTRY,@ITEM_A,@W_A,0,@GROUPID,1,1,0),\n"
        "(@LOOT_ENTRY,@ITEM_B,@W_B,0,@GROUPID,1,1,0),\n"
        "(@LOOT_ENTRY,@ITEM_C,@W_C,0,@GROUPID,1,1,0);\n")});

    v.append({QStringLiteral("Квест: текст ruRU (quest_template_locale)"),
        QStringLiteral(
        "-- В Legion читаемый текст квеста живёт здесь, не в quest_template.\n"
        "DELETE FROM `quest_template_locale` WHERE ID=@QUEST AND locale='ruRU';\n"
        "INSERT INTO `quest_template_locale`\n"
        "(`ID`,`locale`,`LogTitle`,`LogDescription`,`QuestDescription`,`AreaDescription`,\n"
        " `PortraitGiverText`,`PortraitGiverName`,`PortraitTurnInText`,`PortraitTurnInName`,\n"
        " `QuestCompletionLog`,`VerifiedBuild`)\n"
        "VALUES (@QUEST,'ruRU','@TITLE','@LOG_DESC','@QUEST_DESC','','','','','','',0);\n")});

    v.append({QStringLiteral("Квест: цель «убить N мобов»"),
        QStringLiteral(
        "-- Type 0=MONSTER,1=ITEM,2=GAMEOBJECT,3=TALKTO. ID цели глобально уникален.\n"
        "DELETE FROM `quest_objectives` WHERE QuestID=@QUEST AND StorageIndex=0;\n"
        "INSERT INTO `quest_objectives`\n"
        "(`ID`,`QuestID`,`Type`,`StorageIndex`,`ObjectID`,`Amount`,`Flags`,`Flags2`,`TaskStep`,`Description`,`VerifiedBuild`)\n"
        "VALUES (@OBJ_ID,@QUEST,0,0,@MOB,@AMOUNT,0,0,0,'',0);\n")});

    v.append({QStringLiteral("Маршрут: точка пути SmartAI (waypoints)"),
        QStringLiteral(
        "-- entry = path ID (не entry моба!), pointid с 1. Для SMART_ACTION_WP_START(53).\n"
        "DELETE FROM `waypoints` WHERE entry=@PATH_ID;\n"
        "INSERT INTO `waypoints` (`entry`,`pointid`,`position_x`,`position_y`,`position_z`,`point_comment`) VALUES\n"
        "(@PATH_ID,1,@X1,@Y1,@Z1,'точка 1'),\n"
        "(@PATH_ID,2,@X2,@Y2,@Z2,'точка 2'),\n"
        "(@PATH_ID,3,@X3,@Y3,@Z3,'точка 3');\n")});

    v.append({QStringLiteral("Маршрут: путь по GUID (waypoint_data)"),
        QStringLiteral(
        "-- id = GUID существа. Привязка: creature_addon.path_id = @GUID; creature.MovementType=2.\n"
        "DELETE FROM `waypoint_data` WHERE id=@GUID;\n"
        "INSERT INTO `waypoint_data`\n"
        "(`id`,`point`,`position_x`,`position_y`,`position_z`,`orientation`,`delay`,`delay_chance`,`move_flag`,`speed`,`action`,`action_chance`,`entry`,`wpguid`)\n"
        "VALUES\n"
        "(@GUID,1,@X1,@Y1,@Z1,0,0,0,0,0,0,100,0,0),\n"
        "(@GUID,2,@X2,@Y2,@Z2,0,3000,0,0,0,0,100,0,0),\n"
        "(@GUID,3,@X3,@Y3,@Z3,0,0,0,0,0,0,100,0,0);\n")});

    v.append({QStringLiteral("Предмет: правка статов (item_sparse)"),
        QStringLiteral(
        "-- Статы Legion живут в hotfixes.item_sparse. Типы статов сверяй по ItemStat.db2.\n"
        "UPDATE `item_sparse`\n"
        "SET ItemLevel=@ILVL, RequiredLevel=@REQLEVEL,\n"
        "    StatModifierBonusStat1=@STAT1_TYPE, ItemStatValue1=@STAT1_VAL,\n"
        "    StatModifierBonusStat2=@STAT2_TYPE, ItemStatValue2=@STAT2_VAL\n"
        "WHERE ID=@ITEM;\n")});

    v.append({QStringLiteral("Способность: каст по SmartAI (CAST=11)"),
        QStringLiteral(
        "-- event 0=UPDATE_IC(initialMin,initialMax,repeatMin,repeatMax). target 2=VICTIM.\n"
        "INSERT INTO `smart_scripts`\n"
        "(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,\n"
        " `event_param1`,`event_param2`,`event_param3`,`event_param4`,\n"
        " `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,`action_param5`,`action_param6`,\n"
        " `target_type`,`target_param1`,`target_param2`,`target_param3`,`target_x`,`target_y`,`target_z`,`target_o`,`comment`)\n"
        "VALUES (@MOB,0,@ID,0,0,0,100,0,@INIT_MIN,@INIT_MAX,@REP_MIN,@REP_MAX,11,@SPELL,0,0,0,0,0,2,0,0,0,0,0,0,0,'каст по цели');\n")});

    return v;
}

bool SqlWizardService::loadFile(const QString &path, QString *sql, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    *sql = ts.readAll();
    return true;
}

QStringList SqlWizardService::placeholders(const QString &sql) {
    static const QRegularExpression rx(QStringLiteral("@([A-Za-z_][A-Za-z0-9_]*)"));
    QStringList out;
    auto it = rx.globalMatch(sql);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString tok = m.captured(1);
        if (!out.contains(tok)) out.append(tok);
    }
    return out;
}

QString SqlWizardService::generate(const QString &sql, const QMap<QString, QString> &values) {
    QString out = sql;
    // Заменяем от более длинных токенов к коротким, чтобы @ITEM не съело часть @ITEM_A.
    QStringList keys = values.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const auto &k : keys) {
        const QString val = values.value(k);
        if (val.isEmpty()) continue;
        out.replace(QLatin1Char('@') + k, val);
    }
    return out;
}
