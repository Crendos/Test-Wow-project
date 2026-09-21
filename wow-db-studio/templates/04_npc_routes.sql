-- =====================================================================
--  04_npc_routes.sql — маршруты НПЦ, Nordrassil Core 7.3.5
--  Колонки сверены с дампом legion_world.sql
-- =====================================================================
--  В этом ядре ТРИ независимые системы маршрутов. Выбери одну:
--
--  A) SmartAI-путь  → таблица `waypoints` (ключ = path ID) + SMART_ACTION_WP_START
--     Удобно: всё в smart_scripts, есть .reload smart_scripts.
--     Но сама таблица `waypoints` перезагружается только через
--     `.reload all scripts` или рестарт (прямой .reload waypoints НЕТ).
--
--  B) Путь по GUID  → таблица `waypoint_data` (ключ = GUID существа)
--     + creature_addon.path_id (или creature_template_addon.path_id)
--     Есть .reload waypoint_data, но path_id в creature_addon — только рестарт.
--
--  C) C++-скрипт    → таблица `script_waypoint` (ключ = entry НПЦ) + ScriptName
--     Есть .reload script_waypoint, но нужен C++-код в ядре.
--
--  Ниже — системы A и B (чисто на SQL, без кода).
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки
-- ---------------------------------------------------------------------
-- Найти GUID существа в мире:
SELECT guid, id, map, position_x, position_y, position_z, MovementType
FROM `creature` WHERE id = @MOB AND map = @MAP LIMIT 20;

-- Текущий path у существа:
SELECT guid, path_id FROM `creature_addon` WHERE guid = @GUID;
-- Текущий path у шаблона:
SELECT entry, path_id FROM `creature_template_addon` WHERE entry = @MOB;

-- Свободен ли path ID?
SELECT entry FROM `waypoints` WHERE entry = @PATH_ID;
SELECT id FROM `waypoint_data` WHERE id = @GUID;


-- =====================================================================
--  СИСТЕМА A — SmartAI-путь (рекомендуется для скриптуемых НПЦ)
-- =====================================================================

-- ---------------------------------------------------------------------
-- A1. Сам путь: таблица `waypoints`
--     entry = path ID (НЕ entry моба!), pointid = номер точки с 1
--     Колонки: entry, pointid, position_x, position_y, position_z, point_comment
-- ---------------------------------------------------------------------
DELETE FROM `waypoints` WHERE entry = @PATH_ID;
INSERT INTO `waypoints` (`entry`, `pointid`, `position_x`, `position_y`, `position_z`, `point_comment`) VALUES
(@PATH_ID, 1, @X1, @Y1, @Z1, 'точка 1'),
(@PATH_ID, 2, @X2, @Y2, @Z2, 'точка 2'),
(@PATH_ID, 3, @X3, @Y3, @Z3, 'точка 3'),
(@PATH_ID, 4, @X4, @Y4, @Z4, 'точка 4');

-- ---------------------------------------------------------------------
-- A2. Убедиться, что у моба AIName = SmartAI
-- ---------------------------------------------------------------------
UPDATE `creature_template` SET AIName = 'SmartAI' WHERE entry = @MOB AND AIName = '';
-- (в дампе 13 672 моба уже на SmartAI; 2 записи с опечаткой 'SmarAI' — их ядро не видит)

-- ---------------------------------------------------------------------
-- A3. Запустить путь по SmartAI
--     SMART_ACTION_WP_START = 53
--     Параметры (SmartScript.cpp:1598): run(0/1), pathID, canRepeat, quest,
--                                       despawnTime, reactState
--     Событие: SMART_EVENT_UPDATE_IC = 0 (в бою) или UPDATE_OOC = 1 (вне боя)
--              или SMART_EVENT_RESET = 25 / JUST_CREATED / AGGRO = 4
--     Target: SMART_TARGET_SELF = 1
-- ---------------------------------------------------------------------
-- Пример: НПЦ ходит по маршруту вне боя, по кругу (canRepeat=1), пешком (run=0)
DELETE FROM `smart_scripts` WHERE entryorguid = @MOB AND source_type = 0;
INSERT INTO `smart_scripts`
(`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`,
 `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`,
 `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`,
 `action_param5`, `action_param6`,
 `target_type`, `target_param1`, `target_param2`, `target_param3`,
 `target_x`, `target_y`, `target_z`, `target_o`, `comment`)
VALUES
(@MOB, 0, 0, 0, 1, 0, 100, 0, 0, 0, 0, 0,   -- event: UPDATE_OOC, без параметров
 53, 0, @PATH_ID, 1, 0, 0, 0,                -- WP_START: run=0, path=@PATH_ID, repeat=1
 1, 0, 0, 0, 0, 0, 0, 0, 'Старт маршрута вне боя');

-- ---------------------------------------------------------------------
-- A4. Действия на точках маршрута (опционально)
--     SMART_EVENT_WAYPOINT_REACHED = 40, параметры: pointID (0=любая), pathID
--     Пример: на точке 2 — сказать реплику и постоять 5 сек
-- ---------------------------------------------------------------------
INSERT INTO `smart_scripts`
(`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`,
 `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`,
 `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`,
 `action_param5`, `action_param6`,
 `target_type`, `target_param1`, `target_param2`, `target_param3`,
 `target_x`, `target_y`, `target_z`, `target_o`, `comment`)
VALUES
(@MOB, 0, 1, 0, 40, 0, 100, 0, 2, @PATH_ID, 0, 0,  -- на точке 2 маршрута @PATH_ID
 1, 0, 5000, 0, 0, 0, 0,                            -- TALK: groupID=0 из creature_text
 1, 0, 0, 0, 0, 0, 0, 0, 'Реплика на точке 2'),
(@MOB, 0, 2, 0, 40, 0, 100, 0, 2, @PATH_ID, 0, 0,
 54, 5000, 0, 0, 0, 0, 0,                           -- WP_PAUSE 5000 мс
 1, 0, 0, 0, 0, 0, 0, 0, 'Пауза на точке 2');

-- Реплика для TALK (creature_text):
-- Entry, GroupID, ID, Text, Type, Language, Probability, Emote, Duration, Sound,
-- BroadcastTextID, MinTimer, MaxTimer, SpellID, comment
-- DELETE FROM `creature_text` WHERE Entry = @MOB AND GroupID = 0;
-- INSERT INTO `creature_text`
-- (`Entry`,`GroupID`,`ID`,`Text`,`Type`,`Language`,`Probability`,`Emote`,`Duration`,
--  `Sound`,`BroadcastTextID`,`MinTimer`,`MaxTimer`,`SpellID`,`comment`)
-- VALUES (@MOB, 0, 0, 'Стой, кто идёт?', 12, 0, 100, 0, 0, 0, 0, 0, 0, 0, 'реплика маршрута');


-- =====================================================================
--  СИСТЕМА B — путь по GUID (waypoint_data)
-- =====================================================================

-- ---------------------------------------------------------------------
-- B1. Точки пути: таблица `waypoint_data`
--     id = GUID существа, point = номер с 1
--     Колонки: id, point, position_x, position_y, position_z, orientation,
--              delay, delay_chance, move_flag, speed, action, action_chance,
--              entry, wpguid
--     move_flag: 0 = идти, 1 = бежать, 256 = лететь (проверь по ядру)
--     action: ID из waypoint_scripts (срабатывает на точке)
-- ---------------------------------------------------------------------
DELETE FROM `waypoint_data` WHERE id = @GUID;
INSERT INTO `waypoint_data`
(`id`, `point`, `position_x`, `position_y`, `position_z`, `orientation`,
 `delay`, `delay_chance`, `move_flag`, `speed`, `action`, `action_chance`, `entry`, `wpguid`)
VALUES
(@GUID, 1, @X1, @Y1, @Z1, 0, 0, 0, 0, 0, 0, 100, 0, 0),
(@GUID, 2, @X2, @Y2, @Z2, 0, 3000, 0, 0, 0, 0, 100, 0, 0),  -- пауза 3 сек на точке 2
(@GUID, 3, @X3, @Y3, @Z3, 0, 0, 0, 0, 0, 0, 100, 0, 0),
(@GUID, 4, @X4, @Y4, @Z4, 0, 0, 0, 0, 0, 0, 100, 0, 0);

-- ---------------------------------------------------------------------
-- B2. Привязать путь к существу
--     creature_addon: guid, path_id, mount, bytes1, bytes2, emote, auras
--     path_id = GUID (для waypoint_data путь ищется по id=GUID)
-- ---------------------------------------------------------------------
DELETE FROM `creature_addon` WHERE guid = @GUID;
INSERT INTO `creature_addon` (`guid`, `path_id`, `mount`, `bytes1`, `bytes2`, `emote`, `auras`)
VALUES (@GUID, @GUID, 0, 0, 0, 0, '');

-- ---------------------------------------------------------------------
-- B3. Включить движение по пути в спавне
--     creature.MovementType = 2 (путь). Также есть currentwaypoint.
-- ---------------------------------------------------------------------
UPDATE `creature` SET MovementType = 2, currentwaypoint = 1 WHERE guid = @GUID;

-- ---------------------------------------------------------------------
-- B4. Действия на точках пути (опционально): waypoint_scripts
--     Колонки: id, delay, command, datalong, datalong2, dataint, x, y, z, o, guid
--     Привязка: waypoint_data.action = id из waypoint_scripts
-- ---------------------------------------------------------------------
-- DELETE FROM `waypoint_scripts` WHERE id = @WP_SCRIPT;
-- INSERT INTO `waypoint_scripts`
-- (`id`,`delay`,`command`,`datalong`,`datalong2`,`dataint`,`x`,`y`,`z`,`o`,`guid`)
-- VALUES (@WP_SCRIPT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);


-- =====================================================================
--  Дополнительно: группы и сопровождение
-- =====================================================================

-- Группы существ (формации): creature_formations
-- leaderGUID, memberGUID, dist, angle, groupAI
-- INSERT INTO `creature_formations` (`leaderGUID`,`memberGUID`,`dist`,`angle`,`groupAI`)
-- VALUES (@LEADER_GUID, @MEMBER_GUID, 3.0, 0, 0);

-- Сопровождение (эскорт) — через SMART_ACTION_WP_START с quest и SMART_ACTION_FOLLOW.
-- SMART_ACTION_FOLLOW = 29: distance, angle, endCreatureEntry, credit, creditType


-- ---------------------------------------------------------------------
-- Применить  (команды сверены с cs_reload.cpp)
-- ---------------------------------------------------------------------
--   .reload smart_scripts        ← система A (скрипты)
--   .reload all scripts          ← перезагрузит и таблицу `waypoints` (система A)
--   .reload waypoint_data        ← система B (точки)
--   .reload waypoint_scripts     ← действия на точках
--   .reload script_waypoint      ← система C (если используешь)
--   .reload creature_template    ← если менял AIName
--
-- НЕТ прямой команды .reload для:
--   `waypoints` (только .reload all scripts или рестарт),
--   `creature` (спавны), `creature_addon`, `creature_template_addon`,
--   `creature_formations`  → после их правки нужен РЕСТАРТ worldserver.