-- =====================================================================
--  05_dungeon_raid.sql — подземелья и рейды, Nordrassil Core 7.3.5
--  Колонки сверены с дампом legion_world.sql
-- =====================================================================
--  Механики боссов в этом ядре делаются ТРЕМЯ способами (по возрастанию сложности):
--    1) SmartAI (smart_scripts) — чисто на SQL, без кода. Рекомендуем начинать тут.
--    2) DB-скрипты ядра: creature_ai_instance / creature_ai_instance_door,
--       spell_scripts, event_scripts.
--    3) C++-скрипт: instance_template.script + ScriptName + код в src/server/scripts.
--
--  Ниже — способ 1 (SmartAI) + регистрация инстанса/босса на SQL.
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки
-- ---------------------------------------------------------------------
SELECT * FROM `instance_template` WHERE map = @MAP;
SELECT * FROM `instance_encounters` WHERE lastEncounterDungeon = @MAP OR creditEntry = @BOSS;
SELECT * FROM `access_requirement` WHERE mapId = @MAP;
SELECT guid, id, position_x, position_y, position_z FROM `creature` WHERE id = @BOSS AND map = @MAP;


-- ---------------------------------------------------------------------
-- 1. Инстанс: instance_template
--    Колонки: map, parent, script, allowMount, bonusChance
--    script = имя C++-скрипта (пусто, если скриптуем через SmartAI/DB)
-- ---------------------------------------------------------------------
-- DELETE FROM `instance_template` WHERE map = @MAP;
-- INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`, `bonusChance`)
-- VALUES (@MAP, 0, '', 0, 0);


-- ---------------------------------------------------------------------
-- 2. Босс/энкаунтер: instance_encounters
--    Колонки: entry, difficulty, creditType, creditEntry, lastEncounterDungeon, comment
--    creditType: 0 = убить существо (creature), 1 = spell
--    creditEntry: entry существа/спелла, дающего «зачёт»
--    difficulty: 0=нормал, 1=героик, ... (см. Difficulty.db2)
-- ---------------------------------------------------------------------
-- DELETE FROM `instance_encounters` WHERE entry = @ENCOUNTER_ID AND difficulty = @DIFF;
-- INSERT INTO `instance_encounters`
-- (`entry`, `difficulty`, `creditType`, `creditEntry`, `lastEncounterDungeon`, `comment`)
-- VALUES (@ENCOUNTER_ID, @DIFF, 0, @BOSS, @MAP, 'Мой босс');


-- ---------------------------------------------------------------------
-- 3. Требования на вход: access_requirement
--    Колонки: mapId, difficulty, dungeonId, level_min, level_max, item_level,
--             item, item2, quest_done_A, quest_done_H, completed_achievement,
--             completed_achievement_A, quest_failed_text, comment
-- ---------------------------------------------------------------------
-- DELETE FROM `access_requirement` WHERE mapId = @MAP AND difficulty = @DIFF AND dungeonId = 0;
-- INSERT INTO `access_requirement`
-- (`mapId`,`difficulty`,`dungeonId`,`level_min`,`level_max`,`item_level`,`item`,`item2`,
--  `quest_done_A`,`quest_done_H`,`completed_achievement`,`completed_achievement_A`,
--  `quest_failed_text`,`comment`)
-- VALUES (@MAP, @DIFF, 0, 110, 0, 900, 0, 0, 0, 0, 0, 0, NULL, 'Вход в мой данж');


-- ---------------------------------------------------------------------
-- 4. Механика босса на SmartAI (способ 1, чисто SQL)
--    source_type = 0 (creature). entryorguid = entry босса.
--
--    Типичный набор событий/действий (ID сверены с SmartScriptMgr.h):
--      События:  AGGRO=4, HEALT_PCT=2 (в ядре опечатка: SMART_EVENT_HEALT_PCT), MANA_PCT=3, SPELLHIT=8, DEATH=6,
--                UPDATE_IC=0, RESET=25, WAYPOINT_REACHED=40
--      Действия: CAST=11, SUMMON_CREATURE=12, TALK=1, ADD_AURA=75,
--                SET_INVINCIBILITY, EVADE=24, SET_PHASE=30/RANDOM_PHASE=30,
--                ATTACK_START=49, SET_REACT_STATE=8, KILL_UNIT=51
--      Цели:     SELF=1, VICTIM=2, HOSTILE_RANDOM=5, HOSTILE_RANDOM_NOT_TOP=6,
--                ACTION_INVOKER=7, POSITION=8
-- ---------------------------------------------------------------------

-- 4a. При аггро — сказать реплику
INSERT INTO `smart_scripts`
(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
 `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,
 `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,
 `action_param5`,`action_param6`,
 `target_type`,`target_param1`,`target_param2`,`target_param3`,
 `target_x`,`target_y`,`target_z`,`target_o`,`comment`)
VALUES
(@BOSS, 0, 0, 0, 4, 0, 100, 0, 0, 0, 0, 0,        -- AGGRO
 1, 0, 0, 0, 0, 0, 0,                              -- TALK groupID=0
 1, 0, 0, 0, 0, 0, 0, 0, 'Босс: реплика при аггро');

-- 4b. На 70% HP — призвать помощников и кастовать AoE
INSERT INTO `smart_scripts`
(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
 `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,
 `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,
 `action_param5`,`action_param6`,
 `target_type`,`target_param1`,`target_param2`,`target_param3`,
 `target_x`,`target_y`,`target_z`,`target_o`,`comment`)
VALUES
(@BOSS, 0, 1, 0, 2, 0, 100, 1, 0, 70, 0, 0,        -- HEALT_PCT 0..70 (event_type=2), flag=1 (один раз)
 12, @ADD_ENTRY, 1, 0, 1, 0, 0,                     -- SUMMON_CREATURE: entry, тип, длительность
 1, 0, 0, 0, 0, 0, 0, 0, 'Босс: призыв adds на 70%'),
(@BOSS, 0, 2, 0, 2, 0, 100, 1, 0, 70, 0, 0,
 11, @AOE_SPELL, 0, 0, 0, 0, 0,                     -- CAST AoE
 2, 0, 0, 0, 0, 0, 0, 0, 'Босс: AoE на 70%');
-- event_flags=1 → SMART_EVENT_FLAG_ONLY_ONCE (сработает один раз за бой)

-- 4c. Таймерная ротация спеллов в бою (UPDATE_IC)
INSERT INTO `smart_scripts`
(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
 `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,
 `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,
 `action_param5`,`action_param6`,
 `target_type`,`target_param1`,`target_param2`,`target_param3`,
 `target_x`,`target_y`,`target_z`,`target_o`,`comment`)
VALUES
(@BOSS, 0, 3, 0, 0, 0, 100, 0, 5000, 8000, 12000, 15000,  -- UPDATE_IC: initial 5-8с, repeat 12-15с
 11, @SPELL_1, 0, 0, 0, 0, 0,                        -- CAST
 2, 0, 0, 0, 0, 0, 0, 0, 'Босс: спелл1 по таймеру');


-- ---------------------------------------------------------------------
-- 5. Волны призыва: creature_summon_groups
--    Колонки: summonerId, id, summonerType, groupId, entry, position_x, position_y,
--             position_z, orientation, count, actionType, distance, summonType, summonTime
--    summonerType: 0 = creature, 1 = gameobject
--    Вызывается из SmartAI действием SUMMON_CREATURE с типом «from summon group»
--    или C++-скриптом.
-- ---------------------------------------------------------------------
-- DELETE FROM `creature_summon_groups` WHERE summonerId = @BOSS AND groupId = 1;
-- INSERT INTO `creature_summon_groups`
-- (`summonerId`,`id`,`summonerType`,`groupId`,`entry`,`position_x`,`position_y`,
--  `position_z`,`orientation`,`count`,`actionType`,`distance`,`summonType`,`summonTime`)
-- VALUES
-- (@BOSS, 0, 0, 1, @ADD_ENTRY, @AX, @AY, @AZ, 0, 3, 0, 0, 1, 30000),
-- (@BOSS, 1, 0, 1, @ADD_ENTRY, @BX, @BY, @BZ, 0, 3, 0, 0, 1, 30000);


-- ---------------------------------------------------------------------
-- 6. DB-скрипты ядра (способ 2)
--    creature_ai_instance:       entry, bossid, bossidtoactivete, instanceId, comments
--    creature_ai_instance_door:  instanceId, gobId, bossId, doortype, boundary, comments
--    (специфика Nordrassil — ядро читает эти таблицы)
-- ---------------------------------------------------------------------
-- INSERT INTO `creature_ai_instance`
-- (`entry`,`bossid`,`bossidtoactivete`,`instanceId`,`comments`)
-- VALUES (@BOSS, @BOSS_ID, @NEXT_BOSS_ID, @INSTANCE_ID, 'активация следующего босса');


-- ---------------------------------------------------------------------
-- 7. Двери / объекты инстанса
--    gameobject_template + gameobject (спавн). Состояние двери меняется
--    SmartAI-действием на gameobject (source_type=1) или C++-скриптом.
-- ---------------------------------------------------------------------
-- UPDATE `gameobject` SET state = 1 WHERE guid = @DOOR_GUID;  -- 0=ready,1=locked,...


-- ---------------------------------------------------------------------
-- 8. Группы мобов (формации) внутри данжа
--    creature_formations: leaderGUID, memberGUID, dist, angle, groupAI
-- ---------------------------------------------------------------------
-- INSERT INTO `creature_formations` (`leaderGUID`,`memberGUID`,`dist`,`angle`,`groupAI`)
-- VALUES (@LEADER, @MEMBER, 2.0, 0, 0);


-- ---------------------------------------------------------------------
-- Применить  (команды сверены с cs_reload.cpp)
-- ---------------------------------------------------------------------
--   .reload smart_scripts          ← механики боссов (способ 1)
--   .reload creature_summon_groups ← волны призыва
--   .reload access_requirement     ← требования на вход
--   .reload conditions
--
-- НЕТ прямой команды .reload для:
--   instance_template, instance_encounters, creature_ai_instance(_door),
--   creature_formations, gameobject (спавны)  → нужен РЕСТАРТ worldserver.
--
-- Совет: логику босса сначала обкатай на SmartAI. Если не хватает возможностей
-- SmartAI (сложные фазы, кастомные триггеры) — переходи на C++-скрипт
-- (instance_template.script + ScriptName).