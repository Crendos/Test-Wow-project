-- ============================================================================
-- dberrors_cleanup.sql — починка DBErrors из head100 / unique / tail100
-- Разбор: logs_analysis/DBERRORS_ANALYSIS.md
--
-- ДВЕ базы: ЧАСТЬ 1 = hotfixes, ЧАСТИ 2+ = world. Выполнять раздельно!
-- Каждый блок: сначала SELECT-проверка (сколько удалится), потом действие.
-- Повтор безопасен (DELETE/UPDATE идемпотентны).
--
-- Схемы сверены с TrinityCore master (ObjectMgr.cpp, SmartScriptMgr.cpp,
-- SpellMgr.cpp, BattlegroundMgr.cpp, PoolMgr.cpp, sql/base/dev/*.sql).
-- ВАЖНО: колонка smart_scripts для линков называется `link` (не event_links).
-- ============================================================================

-- ############################################################################
-- ЧАСТЬ 1 — hotfixes-база (где BroadcastText / Hotfix)
-- Ошибка head100 (100×):
--   Hotfix locale ... BroadcastText.db2 references row that does not exist N locale ruRU
-- Причина: в broadcast_text_locale есть ruRU-строки для ID, которых нет
-- в broadcast_text (неполный мерж локалей пака).
-- ############################################################################

-- 1.1 Проверка: сколько orphan-строк
SELECT COUNT(*) AS orphan_locale_rows
  FROM broadcast_text_locale
 WHERE ID NOT IN (SELECT ID FROM broadcast_text);

-- (если в таблице есть колонка Locale — можно сузить:)
-- SELECT COUNT(*) FROM broadcast_text_locale
--  WHERE Locale = 'ruRU'
--    AND ID NOT IN (SELECT ID FROM broadcast_text);

-- 1.2 Чистка
DELETE FROM broadcast_text_locale
 WHERE ID NOT IN (SELECT ID FROM broadcast_text);

-- 1.3 Проверка: 0
SELECT COUNT(*) AS leftover
  FROM broadcast_text_locale
 WHERE ID NOT IN (SELECT ID FROM broadcast_text);


-- ############################################################################
-- ЧАСТЬ 2 — world-база
-- ############################################################################

-- ======================== 2a. SmartAI: нет AIName ==========================
-- unique (сотни типов, суммарно ~миллионы строк лога):
--   SmartAIMgr::LoadSmartAIFromDB: Creature entry (N) is not using SmartAI
-- КОД (SmartScriptMgr.cpp): строка smart_scripts ЕСТЬ для entry,
--   но creature_template.AIName != 'SmartAI' → скрипты пропускаются.
-- Т.е. мёртвые SAI-строки: они и так НЕ работают.
--
-- Два варианта (выбрать ОДИН):
--   A) СОХРАНИТЬ поведение как сейчас (рекомендуется): удалить мёртвые
--      smart_scripts — лог тихий, поведение не меняется.
--   B) ВКЛЮЧИТЬ SAI: поставить AIName='SmartAI' — включит эти скрипты
--      (МОЖЕТ изменить поведение NPC — только если контент нужен!).

-- 2a-Проверка A: мёртвые строки SAI у существ (не привязаны к SmartAI)
SELECT COUNT(*) AS dead_creature_sai_rows
  FROM smart_scripts ss
 WHERE ss.source_type = 0
   AND ss.entryorguid > 0
   AND NOT EXISTS (
         SELECT 1 FROM creature_template ct
          WHERE ct.Entry = ss.entryorguid
            AND ct.AIName = 'SmartAI');

-- 2a-Вариант A (DELETE) — рекомендуется:
DELETE ss
  FROM smart_scripts ss
 WHERE ss.source_type = 0
   AND ss.entryorguid > 0
   AND NOT EXISTS (
         SELECT 1 FROM creature_template ct
          WHERE ct.Entry = ss.entryorguid
            AND ct.AIName = 'SmartAI');

-- 2a-Аналогично GameObjects:
--   "GameObject entry (N) is not using SmartGameObjectAI"
SELECT COUNT(*) AS dead_gob_sai_rows
  FROM smart_scripts ss
 WHERE ss.source_type = 1
   AND ss.entryorguid > 0
   AND NOT EXISTS (
         SELECT 1 FROM gameobject_template gt
          WHERE gt.entry = ss.entryorguid
            AND gt.AIName = 'SmartGameObjectAI');

DELETE ss
  FROM smart_scripts ss
 WHERE ss.source_type = 1
   AND ss.entryorguid > 0
   AND NOT EXISTS (
         SELECT 1 FROM gameobject_template gt
          WHERE gt.entry = ss.entryorguid
            AND gt.AIName = 'SmartGameObjectAI');

-- 2a-Вариант B (вместо DELETE, ВМЕСТО него — не выполнять оба!):
-- UPDATE creature_template ct
--    SET ct.AIName = 'SmartAI'
--  WHERE ct.Entry IN (
--          SELECT DISTINCT ss.entryorguid FROM smart_scripts ss
--           WHERE ss.source_type = 0 AND ss.entryorguid > 0)
--    AND ct.AIName <> 'SmartAI';
-- UPDATE gameobject_template gt
--    SET gt.AIName = 'SmartGameObjectAI'
--  WHERE gt.entry IN (
--          SELECT DISTINCT ss.entryorguid FROM smart_scripts ss
--           WHERE ss.source_type = 1 AND ss.entryorguid > 0)
--    AND gt.AIName <> 'SmartGameObjectAI';

-- ======================== 2b. Битые event link (tail) ======================
-- "Entry N SourceType S, Event E, Link Event L not found or invalid"
-- Колонка `link` ссылается на id события, которого нет в той же группе.
-- События с битым link не срабатывают (ядру молчит).
-- Проверка: битые линки
SELECT ss.entryorguid, ss.source_type, ss.id, ss.event_type, ss.link, ss.comment
  FROM smart_scripts ss
 WHERE ss.link <> 0
   AND NOT EXISTS (
         SELECT 1 FROM smart_scripts tgt
          WHERE tgt.entryorguid = ss.entryorguid
            AND tgt.source_type = ss.source_type
            AND tgt.id = ss.link)
 ORDER BY ss.source_type, ss.entryorguid;

-- Чистка (снимает ВСЕ "Link Event not found" в tail):
-- вар. 1 — только отключить линк (событие останется, link не сработает):
UPDATE smart_scripts ss
   SET ss.link = 0
 WHERE ss.link <> 0
   AND NOT EXISTS (
         SELECT 1 FROM smart_scripts tgt
          WHERE tgt.entryorguid = ss.entryorguid
            AND tgt.source_type = ss.source_type
            AND tgt.id = ss.link);
-- вар. 2 (жёстче) — удалить события с битым линком:
-- DELETE ss FROM smart_scripts ss
--  WHERE ss.link <> 0
--    AND NOT EXISTS (SELECT 1 FROM smart_scripts tgt
--                     WHERE tgt.entryorguid = ss.entryorguid
--                       AND tgt.source_type = ss.source_type
--                       AND tgt.id = ss.link);

-- ======================== 2c. GO 204019 (tail особняк) =====================
-- "Entry 204019 SourceType 1, Event 1, Link Event 2 not found"
-- 204019 = ID спелла Благословенный молот; здесь это gameobject.
-- Наш молот = AT 6006 + ScriptName 'at_pal_blessed_hammer' (пар.8) —
-- smart_scripts для него НЕ нужен.
SELECT entryorguid, source_type, id, link, event_type, action_type, comment
  FROM smart_scripts
 WHERE entryorguid = 204019 AND source_type = 1;
-- Если строка не нужна (обычно мусор коллизии ID):
DELETE FROM smart_scripts
 WHERE entryorguid = 204019 AND source_type = 1;
-- (битый линк 204019 уйдёт и из 2b, если link=2 указывал на id=2)

-- ======================== 2d. SMART_EVENT objective id 0 ===================
-- unique [391765×]: SMART_EVENT_QUEST_OBJ_COMPLETION using invalid objective id 0
-- КОД: event_type=48 (SMART_EVENT_QUEST_OBJ_COMPLETION),
--      objective id в event_param1; id=0 невалиден → событие всегда skip.
SELECT entryorguid, source_type, id, event_type, event_param1, comment
  FROM smart_scripts
 WHERE event_type = 48
   AND event_param1 = 0;
-- Отключить/удалить битые события (objective=0 никогда не сработают):
DELETE FROM smart_scripts
 WHERE event_type = 48
   AND event_param1 = 0;

-- ======================== 2e. equipment_id без шаблона =====================
-- [352×] Entry 229979 и [204×] Entry 9100727: equipment_id=1,
--   нет строки в creature_equip_template → ядро и так ставит 0.
-- Фикс: выровнять БД с тем, что ядро уже делает at runtime.
SELECT c.guid, c.id, c.equipment_id
  FROM creature c
 WHERE c.id IN (229979, 9100727)
   AND c.equipment_id <> 0;

UPDATE creature
   SET equipment_id = 0
 WHERE id IN (229979, 9100727)
   AND equipment_id <> 0;
-- Альтернатива (если экипировка нужна): INSERT в creature_equip_template.

-- ======================== 2f. pool_creature без pool_template =============
-- [294×] pool id (369) is not in pool_template
SELECT * FROM pool_creature WHERE pool_id = 369;
SELECT * FROM pool_template WHERE entry = 369; -- пусто → удалить сирот

DELETE FROM pool_creature WHERE pool_id = 369;
-- (если есть pool_gameobject/pool_pool на 369 — тоже:)

-- ======================== 2g. spell_totem_model 157153 =====================
-- [210×] SpellID 157153 not found in dbc — спелла нет в клиенте 12.1 → skip
SELECT * FROM spell_totem_model WHERE SpellID = 157153;
DELETE FROM spell_totem_model WHERE SpellID = 157153;

-- ======================== 2h. ui_map_quest_line questline 231 ==============
-- [187×] references empty or non-existing questline 231 — UI-карта, безвредно
SELECT * FROM ui_map_quest_line WHERE QuestLineId = 231;
DELETE FROM ui_map_quest_line WHERE QuestLineId = 231;

-- ======================== 2i. playercreateinfo_item 40582 ==================
-- [168×] Item 40582 ... removed from original create info not found in db2
-- КОД: amount < 0 означает «удалить стартовый предмет», но предмета нет
-- в DB2-стартовом наборе → строка-призрак.
SELECT * FROM playercreateinfo_item WHERE itemid = 40582;
DELETE FROM playercreateinfo_item WHERE itemid = 40582 AND amount < 0;

-- ======================== 2j. BG script mapid 618 ==========================
-- tail: BattlegroundMgr::LoadBattlegroundScriptTemplate: bad mapid 618
-- Таблица: battleground_scripts (MapId, BattlemasterListId, ScriptName)
SELECT * FROM battleground_scripts WHERE MapId = 618;
DELETE FROM battleground_scripts WHERE MapId = 618;

-- ############################################################################
-- ЧАСТЬ 3 — проверки ПОСЛЕ выполнения + одного рестарта worldserver
-- ############################################################################
-- DBErrors.log (свежий, после удаления старого файла!):
--   findstr /i "ProcFlags" DBErrors.log          → строка 432929 (класс-фикс жив)
--   findstr /i "BroadcastText" DBErrors.log       → 0
--   findstr /i "is not using SmartAI" DBErrors.log→ 0
--   findstr /i "Link Event" DBErrors.log          → 0
--   findstr /i "objective id" DBErrors.log        → 0
--   findstr /i "equipment_id" DBErrors.log        → 0
--   findstr /i "pool_creature" DBErrors.log       → 0
--   findstr /i "totem_model" DBErrors.log         → 0
--   findstr /i "questline" DBErrors.log           → 0
--   findstr /i "create info" DBErrors.log         → 0
--   findstr /i "bad mapid" DBErrors.log           → 0
-- Перед рестартом: удалить/переименовать старый DBErrors.log (append-only).
