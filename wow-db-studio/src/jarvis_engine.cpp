#include "jarvis_engine.h"
#include <QRegularExpression>
#include <QSet>

JarvisEngine::JarvisEngine() {}

// ---- Tokenization ----------------------------------------------------------
QStringList JarvisEngine::tokenize(const QString &text) {
    return text.toLower().split(QRegularExpression("[^\\p{L}\\p{N}_]+"), Qt::SkipEmptyParts);
}

// ---- Lightweight stemming (RU + EN) ---------------------------------------
static const char *kRuSuffixes[] = {
    "ами","ями","ого","его","ому","ему","ыми","ими","остей","остям","остями","остью",
    "ая","яя","ое","ее","ую","юю","ый","ий","ой","ей","ов","ев","ам","ям","ах","ях",
    "ность","тель","ение","ание","ирова","ость",
    "а","я","о","е","у","ю","ы","и","ь","ъ"
};
static const char *kEnSuffixes[] = {
    "ation","ition","ment","ness","ing","ied","ies","ed","ers","er","ly","es","s"
};
QString JarvisEngine::stem(const QString &word) {
    if (word.size() < 3) return word;
    // English suffixes first (Latin words)
    for (const char *s : kEnSuffixes) {
        const int n = int(qstrlen(s));
        if (word.size() > n + 2 && word.endsWith(QLatin1String(s)))
            return word.left(word.size() - n);
    }
    // Russian suffixes
    for (const char *s : kRuSuffixes) {
        const int n = int(qstrlen(s));
        if (word.size() > n + 2 && word.endsWith(QLatin1String(s)))
            return word.left(word.size() - n);
    }
    return word;
}

static const QSet<QString> kStopwords = {
    "и","в","во","не","что","он","на","я","с","со","как","а","то","все","она","так",
    "его","но","да","ты","к","у","же","вы","за","бы","по","только","ее","мне","было",
    "вот","от","меня","еще","нет","о","из","ему","теперь","когда","даже","ну","ли",
    "если","уже","или","ни","быть","был","него","до","вас","нибудь","опять","уж","вам",
    "ведь","там","потом","себя","ничего","ей","может","они","тут","где","есть","надо",
    "the","a","an","of","to","and","in","is","it","for","on","with","this","that","be",
    "as","are","was","at","by","or","not","but","have","has","from","you","we","they",
    "i","he","she","so","if","do","does","did","can","will","would","should","could","about"
};

// ---- Intent detection ------------------------------------------------------
JarvisEngine::Intent JarvisEngine::detectIntent(const QString &text, const QString &core) const {
    const QString t = text.toLower();
    struct Cand { QString label; QStringList kw; QString prefix; };
    const Cand cands[] = {
        {"ошибка SQL / базы данных", {"ошибк","sql","duplicate","unknown column","foreign key","syntax","неизвестн","дубликат","таблиц","столбец","cannot add","doesn't exist","insert","update","delete","select","mysql","database"}, ""},
        {"ошибка компиляции C++", {"компил","compile","error c2","error lnk","linker","необъявлен","identifier","undefined","c2039","c2065","c2664","cl.exe","namespace","include","c++","cpp"}, ""},
        {"предмет (item)", {"предмет","item","вещь","item_template","loot","предметов"}, "item="},
        {"NPC / существо", {"npc","существ","creature","моб","монстр","npcflag","creature_template","smartai","spawn"}, "npc="},
        {"квест (quest)", {"квест","quest","задан","quest_template"}, "quest="},
        {"подземелье / рейд", {"подземель","рейд","dungeon","raid","instance","инстанс","босс","boss","encounter","map"}, "zone="},
        {"заклинание (spell)", {"заклина","spell","spell_template","aura","эффект"}, "spell="},
        {"игровой объект", {"gameobject","объект","gameobject_template","door","object"}, "object="},
        {"скриптинг / SmartAI", {"smartai","скрипт","script","event_type","action_type","source_type","entryorguid","sai","smart_scripts","waypoint","траектор","патрул","cast","каст","aggro","агр","summon","призыв","gossip","диалог","phase","фаз","spellhit","healt_pct","health_pct","event_param","action_param","target_type"}, ""},
        {"анализ ядра", {"определи ядро","какое ядро","проанализируй ядро","версия ядра","core detect","smartai.h","enum smart","nordrassil","trinitycore","cyphercore","azerothcore"}, ""},
        {"ошибка ядра / реалма", {"worldserver","bnetserver","authserver","завис","зависл","freeze","hung","crash","assertion","segfault","bind","already running","mysql gone","sqlerror","fatal","exception","nordrassil","реалм","ядро","access violation","stack overflow","can't connect","could not bind","out of memory","mmap","vmap","dbc","db2","hotfix"}, ""},
    };
    int best = -1, bestScore = 0;
    for (int i = 0; i < int(sizeof(cands)/sizeof(cands[0])); ++i) {
        int score = 0;
        for (const auto &k : cands[i].kw) if (t.contains(k)) score++;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0 || bestScore == 0) {
        if (core.contains("CypherCore", Qt::CaseInsensitive)) return {"серверный скриптинг (C#/SQL)", {}, ""};
        return {"серверный скриптинг (C++/SQL/SmartAI)", {}, ""};
    }
    return {cands[best].label, cands[best].kw, cands[best].prefix};
}

QStringList JarvisEngine::extractIds(const QString &text) const {
    QStringList ids;
    QRegularExpression re("\\b([0-9]{2,8})\\b");
    auto it = re.globalMatch(text);
    QSet<QString> seen;
    int count = 0;
    while (it.hasNext() && count < 6) {
        const QString n = it.next().captured(1);
        if (!seen.contains(n)) { seen.insert(n); ids << n; ++count; }
    }
    return ids;
}

QString JarvisEngine::triageSql(const QString &t) const {
    if (t.contains("duplicate entry") || t.contains("дубликат"))
        return "Похоже на «duplicate entry»: запись с таким ключом уже существует. Проверьте существующий entry/guid через SELECT, используйте UPDATE вместо INSERT, либо удалите/измените конфликтующую строку.";
    if (t.contains("unknown column") || t.contains("неизвестн") || t.contains("нет такого столбца") || t.contains("столбц"))
        return "Похоже на «unknown column»: имя столбца не совпадает со схемой. Сверьте поля через DESCRIBE / SHOW CREATE TABLE именно вашей ветки ядра (имена различаются между 3.3.5a и 12.x).";
    if (t.contains("foreign key") || t.contains("cannot add or update") || t.contains("внешн"))
        return "Похоже на нарушение внешнего ключа: добавляйте родительскую запись первой и соблюдайте порядок INSERT.";
    if (t.contains("syntax") || t.contains("синтакс"))
        return "Похоже на синтаксическую ошибку SQL: проверьте запятые, скобки, кавычки и точки с запятой.";
    if (t.contains("event_param5") || t.contains("even_param5"))
        return "Unknown column event_param5: на TrinityCore 3.3.5a в smart_scripts только event_param1…4. Колонка event_param5 появилась позже (6.x / AzerothCore). Studio теперь читает SHOW COLUMNS и не запрашивает отсутствующие поля. Не копируйте SQL с 7.3.5/Nordrassil в базу 3.3.5a.";
    if (t.contains("doesn't exist") || t.contains("не существует"))
        return "Похоже, таблица не найдена: проверьте имя таблицы и то, что вы подключены к нужной базе (world/auth/characters).";
    if (t.contains("insert") || t.contains("update") || t.contains("delete"))
        return "Изменяющий запрос: перед выполнением сделайте резервную копию, используйте транзакцию и SELECT-проверку с WHERE по entry/guid.";
    return QString();
}

QString JarvisEngine::triageCompile(const QString &t) const {
    if (t.contains("необъявлен") || t.contains("undeclared") || t.contains("identifier") || t.contains("c2065"))
        return "Необъявленный идентификатор: проверьте подключение заголовка (include) и имя переменной/функции.";
    if (t.contains("c2039") || t.contains("не является членом"))
        return "«Не является членом»: метод/член не существует у этого типа — сверьте сигнатуру с API вашей ветки ядра.";
    if (t.contains("linker") || t.contains("lnk") || t.contains("unresolved") || t.contains("неразрешённ"))
        return "Ошибка линковки: не подключена библиотека или не определён символ. Проверьте target_link_libraries и реализацию функции.";
    if (t.contains("c2664") || t.contains("невозможно преобразовать"))
        return "Несовпадение типов: проверьте const/ref и сигнатуру вызываемой функции.";
    if (t.contains("error"))
        return "Ошибка компиляции: всегда начинайте с первой ошибки в логе — последующие часто являются её следствием.";
    return QString();
}

QStringList JarvisEngine::extractLogHits(const QString &text) const {
    QStringList hits;
    const QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    QRegularExpression re("error|fatal|assert|exception|crash|segfault|access violation|couldn't|could not|failed|unable|bind|already|gone away|doesn't exist|unknown command|out of memory|stack overflow|hung|freeze",
                          QRegularExpression::CaseInsensitiveOption);
    for (const auto &line : lines) {
        if (re.match(line).hasMatch())
            hits << line.trimmed().left(400);
        if (hits.size() >= 40) break;
    }
    return hits;
}

QString JarvisEngine::triageServer(const QString &t) const {
    QStringList tips;
    if (t.contains("address already in use") || t.contains("already in use") || t.contains("only one instance")
        || t.contains("another instance") || t.contains("could not bind") || t.contains("couldn't bind")
        || t.contains("unable to bind") || t.contains("bind failed") || t.contains("не удалось привязать"))
        tips << "Порт занят или предыдущий worldserver/bnetserver не умер. Закройте процесс в Диспетчере задач (worldserver.exe, bnetserver.exe), подождите 5–10 с. Порты 7.3.5: bnet 1119, мир 8085/8086, RA 3443, SOAP 7878. Команда: netstat -ano | findstr \"1119 8085 8086\".";
    if (t.contains("mysql") && (t.contains("gone away") || t.contains("lost connection") || t.contains("can't connect")
        || t.contains("cannot connect") || t.contains("access denied") || t.contains("unknown database")
        || t.contains("too many connections")))
        tips << "MySQL: ядро не достучалось до базы. После зависания старые соединения могли остаться. Проверьте, что mysqld запущен, логин/пароль в worldserver.conf и bnetserver.conf, имена баз (world/auth/characters/hotfixes) существуют.";
    if (t.contains("doesn't exist") || t.contains("unknown table") || t.contains("unknown column")
        || (t.contains("table") && t.contains("exist")) || t.contains("updates_include") || t.contains("db updater")
        || t.contains("database structure is not up to date") || t.contains("sql/updates"))
        tips << "Схема БД не догнала ядро. Прогоните sql/updates (world/auth/hotfixes) вашей сборки Nordrassil/Trinity 7.3.5. Не мешайте TDB другой ветки.";
    if (t.contains("dbc") || t.contains("db2") || t.contains("hotfix") || t.contains("unable to open file")
        || t.contains("client data") || t.contains("datadir") || t.contains("no locale") || t.contains("enus"))
        tips << "Данные клиента: DataDir в worldserver.conf, извлечённые dbc/db2/maps/vmaps/mmaps. На 7.3.5 без db2/hotfixes вход в мир часто зависает на загрузке карты.";
    if ((t.contains("map") && (t.contains("unable") || t.contains("not exist") || t.contains("couldn't") || t.contains("fail")))
        || t.contains("vmap") || t.contains("mmap") || t.contains("unable to load map"))
        tips << "Карты/vmaps/mmaps не найдены или битые. После входа ядро грузит грид карты — без mmap/vmap поток может встать. Extractor с того же клиента 7.3.5.26972.";
    if (t.contains("assertion") || t.contains("assert") || t.contains("segfault") || t.contains("access violation")
        || t.contains("0xc0000005") || t.contains("stack overflow") || t.contains("unhandled") || t.contains("crash")
        || t.contains("fatal error") || t.contains("exception"))
        tips << "Крах процесса. Ищите файл в Logs/Crashes рядом с worldserver.exe и первую строку Assertion/Exception. На кастомном 7.3.5 часто: скрипт инстанса, phase, нулевой указатель после LoadFromDB.";
    if (t.contains("завис") || t.contains("freeze") || t.contains("hung") || t.contains("not responding")
        || t.contains("deadlock") || t.contains("timeout"))
        tips << "Зависание без закрытия окна: поток карты или SQL ждёт блокировку. Не убивайте только окно — снимите процесс, иначе на рестарте будет port already in use. Смотрите последнюю строку ДО зависания (Loading map, Player::LoadFromDB, Spell, Script).";
    if (t.contains("out of memory") || t.contains("bad alloc") || t.contains("not enough memory"))
        tips << "Нехватка RAM: maps+vmaps+mmaps Legion тяжёлые. Нужна 64-bit сборка.";
    if (t.contains("scripts") && (t.contains("error") || t.contains("fail") || t.contains("abort")))
        tips << "Ошибка скриптов при загрузке/входе. На Nordrassil много Legion-скриптов — имя скрипта в строке ERROR.";
    if (tips.isEmpty() && (t.contains("worldserver") || t.contains("bnetserver") || t.contains("error") || t.contains("fatal")))
        tips << "Явных шаблонов мало. Ориентируйтесь на ПОСЛЕДНЮЮ строку ERROR/FATAL до зависания и на код выхода при рестарте.";
    return tips.join("\n\n");
}

QString JarvisEngine::tableGuide(const QString &text) const {
    const QString t = text.toLower();
    QString a;
    a += "Таблицы world (что за что отвечает):\n";
    a += "• creature_template — ТИП NPC (entry): имя, скорость, AIName, npcflag, MovementType по умолчанию.\n";
    a += "• creature — СПАВН на карте (guid): координаты, map, spawndist, MovementType этого экземпляра.\n";
    a += "• creature_template_addon — ауры/байты/path_id для всего типа.\n";
    a += "• creature_addon — то же для конкретного guid (важнее template_addon).\n";
    a += "• waypoint_data — точки пути (id = path_id, point, position_x/y/z, delay, move_type).\n";
    a += "• waypoints — альтернативные точки SAI на части ядер (entry, pointid).\n";
    a += "• smart_scripts — события и действия (не движение по карте само по себе).\n";
    a += "• gameobject_template / gameobject — объекты на карте.\n";
    a += "• item_template — серверная логика предмета (на 7.3.5 имя в клиенте часто из hotfixes).\n";
    a += "• quest_template — квест.\n";
    if (t.contains("waypoint") || t.contains("траектор") || t.contains("вейпоинт") || t.contains("ходил") || t.contains("бегал") || t.contains("movement"))
        a += "\nДля вашей задачи (не бегать, а ходить по маршруту) ключевые: creature.MovementType, creature.spawndist, creature_addon.path_id, waypoint_data.\n";
    return a;
}

// ---- Core family detection (для SmartAI-специфики и анализа ядра) ----------
static QString coreFamily(const QString &core) {
    const QString c = core.toLower();
    if (c.contains("cypher")) return QStringLiteral("cypher");
    if (c.contains("azeroth")) return QStringLiteral("azeroth");
    if (c.contains("3.3.5")) return QStringLiteral("335");
    if (c.contains("4.3.4")) return QStringLiteral("434");
    if (c.contains("5.4.8")) return QStringLiteral("548");
    if (c.contains("nordrassil") || c.contains("7.3.5")) return QStringLiteral("735");
    if (c.contains("8.") || c.contains("9.") || c.contains("10.")) return QStringLiteral("8x10x");
    if (c.contains("11.") || c.contains("12.") || c.contains("master")) return QStringLiteral("12x");
    return QStringLiteral("unknown");
}

// ---- Анализ ядра: что умеет, чем SmartAI/схема отличается ------------------
QString JarvisEngine::coreProfile(const QString &core) const {
    const QString fam = coreFamily(core);
    QString a = QStringLiteral("===== АНАЛИЗ ЯДРА =====\nЯдро: ") + core + QStringLiteral("\n");
    if (fam == QLatin1String("335")) {
        a += QStringLiteral("• TrinityCore 3.3.5a (WotLK). smart_scripts БЕЗ event_param5 (только event_param1–4), action_param1–6.\n");
        a += QStringLiteral("• Событие % HP = HEALT_PCT (id 2) — именно так (опечатка ядра), не HEALTH_PCT.\n");
        a += QStringLiteral("• Квесты: quest_template (цели прямо в template), БЕЗ quest_objectives.\n");
        a += QStringLiteral("• Не копируйте SQL с 7.3.5/12.x: имена колонок и id событий расходятся.\n");
    } else if (fam == QLatin1String("434")) {
        a += QStringLiteral("• TrinityCore 4.3.4 (Cataclysm). smart_scripts с event_param1–5. Квесты: quest_template + objectives.\n");
    } else if (fam == QLatin1String("548")) {
        a += QStringLiteral("• TrinityCore 5.4.8 (MoP). smart_scripts с event_param1–5. Появляются DB2-данные клиента.\n");
    } else if (fam == QLatin1String("735")) {
        a += QStringLiteral("• Nordrassil / TrinityCore 7.3.5 (Legion). smart_scripts: event_param1–5, action_param1–6.\n");
        a += QStringLiteral("• Событие % HP = HEALT_PCT (id 2). GOSSIP_SELECT id 62, SPELL_CLICK id 73 (на кастомных ядрах бывают свои id, напр. 82 — сверьте SmartAI.h).\n");
        a += QStringLiteral("• Квесты: quest_template + quest_objectives + quest_template_locale (текст ruRU).\n");
        a += QStringLiteral("• Лут: КАСТОМНЫЙ LootMgr (item=abs(), chance=fabs(), shared≈доп. проверка 0.5%) — НЕ стоковая схема Trinity.\n");
        a += QStringLiteral("• Статы предметов: hotfixes DB часто заглушка → item-sparse/item-effect правятся в клиентских DB2 (WDBX).\n");
    } else if (fam == QLatin1String("8x10x")) {
        a += QStringLiteral("• TrinityCore 8.x–10.x. smart_scripts: event_param1–6, action_param1–6.\n");
        a += QStringLiteral("• Событие % HP переименовано в HEALTH_PCT (id 2). Квесты: quest_template + quest_objectives; hotfixes DB полноценная.\n");
    } else if (fam == QLatin1String("12x")) {
        a += QStringLiteral("• TrinityCore 11.x/12.x (upstream master). Самая свежая схема SmartAI, event_param1–6, много новых action/target id.\n");
        a += QStringLiteral("• HEALTH_PCT (id 2). Имена таблиц/колонок и id событий сверяйте ТОЛЬКО по SmartAI.h вашего master.\n");
    } else if (fam == QLatin1String("cypher")) {
        a += QStringLiteral("• CypherCore (C#). SmartAI-подобная система, но реализация на C#; id и таблицы могут отличаться от Trinity. Скриптинг — C# (Scripts/), не только SQL.\n");
    } else if (fam == QLatin1String("azeroth")) {
        a += QStringLiteral("• AzerothCore = форк TrinityCore 3.3.5a. SmartAI как в 3.3.5a (без event_param5), но AC добавляет свои action/event id — сверяйте SmartAI.h AzerothCore.\n");
    } else {
        a += QStringLiteral("• Ядро не определено точно. Определите: вкладка «Подключение» → «Определить ядро» (читает version/realmlist/README/.git/.build.info).\n");
    }
    a += QStringLiteral("\nКак проверить SmartAI-схему именно вашего ядра:\n");
    a += QStringLiteral("1. SQL: SHOW COLUMNS FROM smart_scripts; — покажет, есть ли event_param5/6.\n");
    a += QStringLiteral("2. В исходниках ядра: SmartAI.h (enum SMART_EVENT / SMART_ACTION / SMART_TARGET) — точные id.\n");
    a += QStringLiteral("3. id событий/действий НЕ портабельны между ветками — всегда сверяйте enum своего ядра.\n");
    return a;
}

// ---- Полный справочник SmartAI ---------------------------------------------
QString JarvisEngine::smartAiReference(const QString &core) const {
    const QString fam = coreFamily(core);
    QString a;
    a += QStringLiteral("===== СПРАВОЧНИК SmartAI (smart_scripts) =====\n");
    a += QStringLiteral("Одна строка = «событие → действие → цель». Ключ: (entryorguid, source_type, id, link).\n");
    a += QStringLiteral("entryorguid = entry (весь тип) или -guid (конкретный спавн, со знаком минус).\n\n");
    a += QStringLiteral("source_type: 0=CREATURE, 1=GAMEOBJECT, 2=AREATRIGGER, 5=QUEST, 6=SPELL, 9=TIMED_ACTIONLIST.\n");
    a += QStringLiteral("  (На кастомных ядрах, вкл. Nordrassil, могут быть доп. типы — сверьте enum.)\n\n");
    a += QStringLiteral("event_type (id стабильны в нижнем диапазоне; ВЫШЕ — сверяйте ядро):\n");
    a += QStringLiteral("  0 UPDATE_IC (тик в бою), 1 UPDATE_OOC (вне боя), 2 HEALT_PCT/HEALTH_PCT (% HP), 3 MANA_PCT,\n");
    a += QStringLiteral("  4 AGGRO, 5 KILL, 6 DEATH, 7 EVADE, 8 SPELLHIT, 9 RANGE, 10 OOC_LOS, 12 RESPAWN,\n");
    a += QStringLiteral("  13 TARGET_HEALTH_PCT, 14 VICTIM_CASTING, 15 FRIENDLY_HEALTH, 18 SUMMONED_UNIT, 19 ACTION_DONE,\n");
    a += QStringLiteral("  20 UPDATE (универс. таймер), 21 LINK (по link), 39/40 WAYPOINT_START/REACHED,\n");
    a += QStringLiteral("  62 GOSSIP_SELECT, 73 SPELL_CLICK (id 62/73/82 — сверьте ядро). event_param1–4 (+5 на 6.x/7.3.5).\n\n");
    a += QStringLiteral("action_type (общие для Nordrassil 7.3.5 и TC master; сверяйте SmartScriptMgr.h):\n");
    a += QStringLiteral("  1 TALK, 11 CAST, 12 SUMMON_CREATURE, 24 EVADE, 29 FOLLOW, 30 SET_PHASE,\n");
    a += QStringLiteral("  49 ATTACK_START, 53 WAYPOINT_START, 54 WAYPOINT_PAUSE, 55 WAYPOINT_STOP,\n");
    a += QStringLiteral("  59 SET_RUN, 62 TELEPORT, 65 WAYPOINT_RESUME, 69 MOVE_TO_POS, 75 ADD_AURA. action_param1–6.\n");
    a += QStringLiteral("  ВНИМАНИЕ: id действий НЕ портабельны между ветками (в Nordrassil есть кастомные 161–234). Полный список — enum SMART_ACTION вашего ядра.\n\n");
    a += QStringLiteral("target_type (сверяйте enum; проверены на 7.3.5):\n");
    a += QStringLiteral("  1 SELF, 2 VICTIM, 5 HOSTILE_RANDOM, 7 ACTION_INVOKER, 8 POSITION;\n");
    a += QStringLiteral("  3/4 HOSTILE_2ND/LAST_AGGRO, 9 CREATURE_RANGE, 11 CREATURE_DISTANCE, 13 GAMEOBJECT_RANGE, 17 PLAYER_RANGE, 19 CLOSEST_PLAYER — сверяйте.\n\n");
    a += QStringLiteral("Связывание (link): у строки A link = id строки B → после A сразу идёт B (цепочки/фазы).\n");
    a += QStringLiteral("event_phase_mask — битовая маска фаз (1,2,4,8…); event_chance = 100 или %; event_flags.\n\n");
    if (fam == QLatin1String("335")) a += QStringLiteral("ВАШЕ ЯДРО 3.3.5a: без event_param5; HEALT_PCT (id 2). Не вставляйте SQL с 7.3.5/12.x.\n");
    else if (fam == QLatin1String("735")) a += QStringLiteral("ВАШЕ ЯДРО 7.3.5/Nordrassil: event_param1–5; HEALT_PCT (id 2); GOSSIP_SELECT 62, SPELL_CLICK 73; кастомный LootMgr.\n");
    else if (fam == QLatin1String("8x10x") || fam == QLatin1String("12x")) a += QStringLiteral("ВАШЕ ЯДРО 8.x–12.x: event_param1–6; HEALTH_PCT (id 2); много новых id — сверяйте SmartAI.h.\n");
    else a += QStringLiteral("Сверьте id и колонки по SHOW COLUMNS и SmartAI.h вашего ядра.\n");
    return a;
}

// ---- Рецепты SmartAI под задачу --------------------------------------------
QString JarvisEngine::smartAiRecipes(const QString &task, const QString &core) const {
    Q_UNUSED(core);
    const QString t = task.toLower();
    QString a = QStringLiteral("===== РЕЦЕПТЫ SmartAI (подставьте свой entry/spell) =====\n");
    bool any = false;
    if (t.contains("aggro") || t.contains(QStringLiteral("агр")) || t.contains(QStringLiteral("в бою")) || t.contains(QStringLiteral("нападает"))) {
        a += QStringLiteral("• Каст при аггро: event_type=4 (AGGRO), action_type=11 (CAST), action_param1=SPELLID, target_type=2 (VICTIM).\n"); any = true;
    }
    if (t.contains("health") || t.contains("hp") || t.contains(QStringLiteral("здоров")) || t.contains("%")) {
        a += QStringLiteral("• Каст при N% HP: event_type=2 (HEALT_PCT), event_param1=N, event_param2=повтор, action_type=11 (CAST), target=1 (SELF).\n"); any = true;
    }
    if (t.contains("death") || t.contains(QStringLiteral("смерт")) || t.contains(QStringLiteral("умирает"))) {
        a += QStringLiteral("• Реплика при смерти: event_type=6 (DEATH), action_type=1 (TALK), action_param1=GROUPID (creature_text), target=1 (SELF).\n"); any = true;
    }
    if (t.contains("summon") || t.contains(QStringLiteral("призыв")) || t.contains(QStringLiteral("спавн"))) {
        a += QStringLiteral("• Призыв: event_type=4/6, action_type=12 (SUMMON_CREATURE), action_param1=ENTRY, param2=тип, target=8 (POSITION).\n"); any = true;
    }
    if (t.contains("waypoint") || t.contains(QStringLiteral("траектор")) || t.contains(QStringLiteral("маршрут")) || t.contains(QStringLiteral("патрул")) || t.contains(QStringLiteral("ходит"))) {
        a += QStringLiteral("• Патруль: creature.MovementType=2, spawndist=0, creature_addon.path_id=<PATH>, точки в waypoint_data (id=path_id).\n");
        a += QStringLiteral("  SAI: event 39/40 (WAYPOINT_START/REACHED) → action 53/54/55 (WP_START/PAUSE/STOP), 59 (SET_RUN).\n"); any = true;
    }
    if (t.contains("spellhit") || t.contains(QStringLiteral("попало")) || t.contains(QStringLiteral("заклинание по"))) {
        a += QStringLiteral("• Реакция на заклинание: event_type=8 (SPELLHIT), event_param1=SPELLID, action_type=11 (CAST) или 1 (TALK), target=7 (ACTION_INVOKER).\n"); any = true;
    }
    if (t.contains("gossip") || t.contains(QStringLiteral("диалог")) || t.contains(QStringLiteral("меню"))) {
        a += QStringLiteral("• Диалог: event_type=62 (GOSSIP_SELECT, id сверьте), action_type=1 (TALK) или 11 (CAST), target=7. Меню — gossip_menu + creature_template.gossip_menu_id.\n"); any = true;
    }
    if (t.contains("phase") || t.contains(QStringLiteral("фаз")) || t.contains(QStringLiteral("этап"))) {
        a += QStringLiteral("• Фазы: action 30 (SET_PHASE; INC_PHASE/RANDOM_PHASE id сверяйте); у строк event_phase_mask=бит фазы (1,2,4…). Цепочка — через link.\n"); any = true;
    }
    if (t.contains("object") || t.contains(QStringLiteral("объект")) || t.contains(QStringLiteral("gameobject")) || t.contains(QStringLiteral("двер")) || t.contains(QStringLiteral("сундук"))) {
        a += QStringLiteral("• Объект (source_type=1): event 73 (SPELL_CLICK) / 62 (GOSSIP), action 12 (SUMMON), 1 (TALK); ACTIVATE_GOBJECT id сверяйте по enum.\n"); any = true;
    }
    if (t.contains(QStringLiteral("кредит")) || t.contains(QStringLiteral("засчит")) || t.contains(QStringLiteral("используй"))
        || (t.contains("spellhit") && (t.contains(QStringLiteral("квест")) || t.contains("quest")))) {
        a += QStringLiteral("• Каст засчитывает квест (quest credit): NPC AIName='SmartAI'; smart_scripts: event_type=8 (SPELLHIT), event_param1=<ID_спелла>, event_flags=1, action_type=11 (CAST), action_param1=<credit-спелл>, target_type=7 (ACTION_INVOKER). Credit-спелл = спелл с эффектом Kill Credit: Effect=134 (misc=entry NPC) или 90 (персональный); ищи в hotfixes.spell_effect по misc-колонке=entry NPC (имя — через SHOW COLUMNS). Если его нет — создай (hotfixes.spell+spell_effect, Effect=134, EffectMiscValue1=entry) либо обход: DELETE FROM quest_objectives WHERE QuestID=<id>.\n"); any = true;
    }
    if (!any)
        a += QStringLiteral("Опишите задачу словами (например: «NPC кастует X при аггро и призывает Y на 50% HP») — подберу события/действия.\n");
    a += QStringLiteral("\nФормат строки: (entryorguid, source_type, id, link, event_type, event_phase_mask, event_chance, event_flags, event_param1..5, action_type, action_param1..6, target_type, target_param1..3, target_x/y/z/o, comment).\n");
    a += QStringLiteral("После правок: .reload smart_scripts (или рестарт). AIName в creature_template должен быть 'SmartAI'.\n");
    return a;
}

QString JarvisEngine::wdbxHowto() const {
    QString a;
    a += QStringLiteral("===== Как пользоваться WDBX Editor (патч клиентских DB2) =====\n");
    a += QStringLiteral("1) Запусти WDBX Editor (открывает DBC/DB2/WDB/ADB Legion).\n");
    a += QStringLiteral("2) File → Open → выбери нужный .db2 из папки клиента DBFilesClient.\n");
    a += QStringLiteral("3) Добавь/измени строки; ID обязаны совпадать с world/hotfixes.\n");
    a += QStringLiteral("4) File → Save; при необходимости обнови DBCache.bin. 5) Перезапусти клиент.\n\n");
    a += QStringLiteral("Какие DB2 править под задачу:\n");
    a += QStringLiteral("• Новый/правка предмета: item.db2, item-sparse.db2 (ItemLevel, качество, статы), item-effect.db2, ItemDisplayInfo.db2 (иконка).\n");
    a += QStringLiteral("• Новый спелл: spell.db2, spell-effect.db2, spell-aura-options.db2.\n");
    a += QStringLiteral("• Привязка NPC→квестовый предмет (подсветка, 0/N): CreatureQuestItem.db2; если файла нет — серверный обход kill-цель (Type=0).\n");
    a += QStringLiteral("• Статы: item-sparse.db2 (ItemStatValue1..10 + StatModifierBonusStat1..10; 4=Agility,5=Int,7=Stamina,32=Crit,36=Haste,40=Vers,49=Mastery).\n\n");
    a += QStringLiteral("В Studio есть помощник: «Редактор контента → WDBX / патч клиента» — соберёт инструкцию под задачу и папку клиента.\n");
    return a;
}

QString JarvisEngine::analyzeSmartAi(const QString &task, const QString &script, const QString &error, const QString &core) const {
    const QString t = (task + "\n" + script + "\n" + error).toLower();
    QString a;
    a += "Разбор скрипта / SmartAI:\n";
    if (t.contains("event_param5") || t.contains("even_param5") || t.contains("unknown column")) {
        a += "• SQL: колонка не из вашей схемы. Trinity 3.3.5a: smart_scripts без event_param5 (param1–4). Не вставляйте дамп 7.3.5/Nordrassil в 3.3.5a.\n";
    }
    if (t.contains("smart_scripts") || t.contains("event_type") || t.contains("action_type") || t.contains("sai")) {
        a += "• source_type 0 = creature_template (entry), 1 = GO template. Для одного спавна используют отрицательный guid в entryorguid.\n";
        a += "• AIName в creature_template должен быть SmartAI, иначе smart_scripts не исполняются.\n";
        a += "• После правок: reload smart_scripts (или рестарт ядра).\n";
    }
    const bool wantPath = t.contains("траектор") || t.contains("waypoint") || t.contains("вейпоинт")
        || t.contains("ходил") || t.contains("бегал") || t.contains("не бег") || t.contains("movementtype")
        || t.contains("path_id") || t.contains("маршрут");
    if (wantPath) {
        a += "\nNPC ходит по заданной траектории, а не бегает (Trinity 3.3.5a):\n";
        a += "1. creature (спавн guid этого NPC):\n";
        a += "   MovementType = 2 (WAYPOINT), spawndist = 0 (иначе бродит случайно, тип 1).\n";
        a += "   MovementType 0 = стоит, 1 = random, 2 = waypoint.\n";
        a += "2. Назначить path_id:\n";
        a += "   INSERT/UPDATE creature_addon SET guid=<GUID>, path_id=<PATH>;\n";
        a += "   Часто path_id = guid*10, главное — уникальный id.\n";
        a += "3. Точки в waypoint_data (id = path_id):\n";
        a += "   id, point, position_x, position_y, position_z, orientation, delay, move_type, action, action_chance, wpguid.\n";
        a += "   point = 1,2,3… по порядку. delay — пауза в мс.\n";
        a += "4. В игре: .npc info (guid), .wp add / .wp load PATH, либо SQL выше.\n";
        a += "5. SmartAI не обязателен для простого патруля. SAI action 53 (WP start) — если путь стартует по событию.\n";
        a += "6. После SQL: .reload creature_template и респаун NPC (.npc add / рестарт грида) или рестарт ядра.\n";
        if (core.contains("7.3.5") || core.contains("Nordrassil", Qt::CaseInsensitive))
            a += "На 7.3.5/Nordrassil имена полей addon/waypoint могут отличаться — сверьте DESCRIBE waypoint_data и creature_addon.\n";
    }
    if (!script.trimmed().isEmpty() && script.contains("smart_scripts", Qt::CaseInsensitive)) {
        a += "\nВставленный SmartAI: смотрите event_type/action_type по документации вашей ветки; пустые нули — заготовка, не скрипт.\n";
    }
    a += "\n" + smartAiReference(core);
    a += "\n" + smartAiRecipes(task + " " + error, core);
    a += "\n" + tableGuide(t);
    return a;
}

// ---- Main analysis ---------------------------------------------------------
JarvisResult JarvisEngine::analyze(const QString &task, const QString &script,
                                   const QString &error, const QString &core,
                                   const QString &knowledge, const QString &project,
                                   const QString &schema) const {
    JarvisResult r;
    const QString full = task + "\n" + script + "\n" + error;

    // 1. Understand words
    const auto tokens = tokenize(full);
    QStringList meaningful;
    for (const auto &w : tokens) {
        const auto s = stem(w);
        if (!kStopwords.contains(w) && !kStopwords.contains(s) && s.size() > 2 && !meaningful.contains(s))
            meaningful << s;
    }
    r.reasoning << "Разобрано слов (после стемминга): " + QString::number(meaningful.size());
    if (!meaningful.isEmpty())
        r.reasoning << "Ключевые слова: " + meaningful.mid(0, 12).join(", ") + (meaningful.size() > 12 ? " …" : "");

    // 2. Understand topic
    const auto intent = detectIntent(full, core);
    r.reasoning << "Определена тема: «" + intent.label + "» (ядро: " + core + ")";

    // 3. SQL / compile triage
    const QString sqlFix = triageSql(full.toLower());
    const QString cmpFix = triageCompile(full.toLower());
    const QString srvFix = triageServer(full.toLower());
    const auto logHits = extractLogHits(full);
    if (!sqlFix.isEmpty()) r.reasoning << "Найдена типичная SQL-ошибка — есть готовый разбор.";
    if (!cmpFix.isEmpty()) r.reasoning << "Найдена типичная ошибка компиляции — есть готовый разбор.";
    if (!srvFix.isEmpty()) r.reasoning << "Найдены признаки лога ядра/реалма — разбираю ошибки запуска и зависания.";
    if (!logHits.isEmpty()) r.reasoning << "Строк ERROR/FATAL/assert в логе: " + QString::number(logHits.size());

    // 4. Knowledge base retrieval (ranked)
    if (!knowledge.isEmpty()) r.reasoning << "В локальной базе знаний есть релевантные заметки — использую их.";

    // 5. Context hints
    if (!project.isEmpty()) r.reasoning << "Загружен контекст исходников проекта — учитываю.";
    if (!schema.isEmpty()) r.reasoning << "Загружен снимок схемы MySQL — сверяю имена полей.";

    // 6. Entity IDs + Wowhead links (opened via browser)
    const auto ids = extractIds(full);
    const bool hasEntity = !intent.wowheadPrefix.isEmpty() && !ids.isEmpty();
    if (hasEntity) r.reasoning << "Обнаружены идентификаторы: " + ids.join(", ") + " — готовлю ссылки Wowhead.";
    else r.reasoning << "Идентификаторы сущности не найдены — ссылки Wowhead не формирую.";

    // ---- Compose answer ----
    QString a;
    a += "Джарвис (офлайн-режим): локальный анализ без отправки данных в сеть.\n\n";
    a += "Тема: " + intent.label + ".\n";

    if (!sqlFix.isEmpty()) a += "\nSQL: " + sqlFix + "\n";
    if (!cmpFix.isEmpty()) a += "\nКомпиляция: " + cmpFix + "\n";
    if (!srvFix.isEmpty()) a += "\nЯдро / реалм:\n" + srvFix + "\n";
    if (!logHits.isEmpty()) {
        a += "\nПодозрительные строки лога:\n";
        for (const auto &h : logHits) a += "• " + h + "\n";
    }
    if (intent.label.contains(QString::fromUtf8("ядра")))
        a += "\nЕсли ядро зависло после входа в игру: последняя строка лога ДО зависания важнее кода рестарта. Код рестарта часто «port already in use» — это следствие, убейте старый worldserver.exe.\n";

    const QString smart = analyzeSmartAi(task, script, error, core);
    const QString lowFull = full.toLower();
    const bool wantScript = intent.label.contains(QString::fromUtf8("скрипт"))
        || intent.label.startsWith("NPC")
        || lowFull.contains("smart") || lowFull.contains("waypoint")
        || lowFull.contains(QString::fromUtf8("траектор")) || lowFull.contains(QString::fromUtf8("ходил"))
        || lowFull.contains(QString::fromUtf8("бегал")) || !script.trimmed().isEmpty();
    if (wantScript) {
        r.reasoning << "Разобран SmartAI/скрипт и таблицы движения NPC.";
        a += "\n" + smart + "\n";
    } else if (lowFull.contains("таблиц") || lowFull.contains("table")) {
        a += "\n" + tableGuide(full) + "\n";
    }

    // Анализ ядра: что умеет и чем его SmartAI/схема отличается от других веток.
    const bool wantCore = lowFull.contains(QStringLiteral("ядро")) || lowFull.contains("core")
        || lowFull.contains("nordrassil") || lowFull.contains("trinity") || lowFull.contains("cypher")
        || lowFull.contains(QStringLiteral("определи")) || lowFull.contains(QStringLiteral("какое ядро"))
        || lowFull.contains(QStringLiteral("проанализируй ядро"));
    if (wantCore) {
        r.reasoning << QStringLiteral("Выполнен анализ ядра и его SmartAI-схемы.");
        a += "\n" + coreProfile(core) + "\n";
    }

    // WDBX / патч клиентских DB2
    const bool wantWdbx = lowFull.contains("wdbx") || lowFull.contains("dbcache") || lowFull.contains("creaturequestitem")
        || lowFull.contains(QStringLiteral("патч клиент")) || lowFull.contains("db2");
    if (wantWdbx) {
        r.reasoning << QStringLiteral("Добавлена инструкция по WDBX и патчу клиентских DB2.");
        a += "\n" + wdbxHowto() + "\n";
    }

    if (!knowledge.isEmpty()) {
        // Show a compact excerpt of the most relevant notes (already ranked by search).
        a += "\nИз локальной базы знаний (проверяйте по актуальной схеме):\n" + knowledge.left(1600) + "\n";
    }

    if (!schema.isEmpty())
        a += "\nСхема БД загружена — сверяйте имена таблиц/полей со снимком схемы (см. «Прочитать схему»).\n";

    // Generic guidance per topic
    if (intent.label.startsWith("NPC") || intent.label.startsWith("скриптинг"))
        a += "\nРекомендация по NPC/SmartAI: определите, работаете вы с template (тип) или spawn (конкретный GUID); проверяйте source_type/event_type/action_type по документации вашей ветки; изменения вносите через точный entryorguid и делайте бэкап.\n";
    if (intent.label.startsWith("предмет"))
        a += "\nРекомендация по предмету: начните с item_template (entry, name, класс/качество), затем loot/vendor/quest-связи; проверяйте поля по схеме вашей сборки.\n";
    if (intent.label.startsWith("квест"))
        a += "\nРекомендация по квесту: проверьте template, objectives, rewards, relations и conditions; точные таблицы зависят от ветки ядра.\n";
    if (intent.label.startsWith("подземелье"))
        a += "\nРекомендация по подземелью/рейду: map/instance, доступ, encounter, spawn и AI боссов; логика в C++ скриптах ядра и SmartAI.\n";

    a += "\nОбщий совет: перед изменениями — резервная копия, тестовая база, минимум прав у SQL-пользователя.\n";

    // Suggested URLs
    if (hasEntity) {
        const QString base = "https://www.wowhead.com/";
        for (const auto &id : ids)
            r.suggestedUrls << base + intent.wowheadPrefix + id;
    }
    if (r.suggestedUrls.isEmpty())
        r.suggestedUrls << "https://www.wowhead.com/search?q=" + QString::fromUtf8(task.toUtf8().toPercentEncoding().left(120));

    if (!r.suggestedUrls.isEmpty()) {
        a += "\nСсылки (откроются в браузере):\n";
        for (const auto &u : r.suggestedUrls) a += "• " + u + "\n";
    }

    r.answer = a;
    return r;
}

JarvisResult JarvisEngine::analyzeServerLogs(const QString &worldLog, const QString &realmLog,
                                            const QString &fileDump, const QString &core) const {
    const QString task = QString::fromUtf8(
        "Проанализируй логи ядра (worldserver) и реалма (bnetserver/authserver). "
        "Ядро зависло после входа в игру, при перезапуске была ошибка. "
        "Найди ERROR/FATAL/assert, причину зависания и ошибку рестарта (часто порт/процесс не отпущен). Ядро: ") + core;
    QString blob;
    blob += QString::fromUtf8("===== КОНСОЛЬ ЯДРА =====\n") + worldLog.right(24000) + "\n";
    blob += QString::fromUtf8("===== КОНСОЛЬ РЕАЛМА =====\n") + realmLog.right(12000) + "\n";
    if (!fileDump.isEmpty())
        blob += QString::fromUtf8("===== ФАЙЛЫ LOGS РЯДОМ С EXE =====\n") + fileDump.right(28000);
    return analyze(task, blob, blob, core, QString(), QString(), QString());
}
