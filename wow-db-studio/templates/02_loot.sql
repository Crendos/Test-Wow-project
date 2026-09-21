-- =====================================================================
--  02_loot.sql — шаблоны лута для Nordrassil Core 7.3.5
--  Семантика полей СВЕРЕНА С ИСХОДНИКАМИ ЯДРА (src/server/game/Loot/LootMgr.cpp).
--  У этого ядра логика лута ОТЛИЧАЕТСЯ от «классического» TrinityCore —
--  см. раздел «ОСОБЕННОСТИ ЯДРА» ниже. Не копируй знания с форумов по TC!
-- =====================================================================
--  ОСОБЕННОСТИ ЯДРА (проверено по LootMgr.cpp):
--
--  1) Колонка `item`:
--       >= 0  → обычный предмет (id предмета)
--       <  0  → ВАЛЮТА (id = |item|).  [LootMgr.cpp:135]
--
--  2) Колонка `ChanceOrQuestChance`:
--       chance       = |ChanceOrQuestChance|          [LootMgr.cpp:270]
--       needs_quest  = (ChanceOrQuestChance < 0)      [LootMgr.cpp:271]
--     Т.е. знак «-» = квестовый дроп, само значение — процент.
--       chance >= 100 → падает всегда                 [LootMgr.cpp:280]
--
--  3) Колонка `groupid`  (ВНИМАНИЕ — не как в обычном TC!):
--       groupid = 0 → независимая строка (ролл каждой строки отдельно)
--       groupid > 0 → группа: из группы выпадает РОВНО ОДИН предмет.
--                     Специального поведения для groupid>8 в этом ядре НЕТ.
--                     [LootMgr.cpp: AddEntry + LootGroup::Process → return после 1 предмета]
--       Чтобы строка попала в группу, нужно groupid>0 И mincountOrRef>0.
--       [LootMgr.cpp: AddEntry, строка `item.group > 0 && item.mincountOrRef > 0`]
--
--  4) Колонка `shared` (только creature_loot_template) — ПЕРЕОПРЕДЕЛЕНА ядром:
--       shared=1 → предмет проходит доп. проверку 0.5% (roll_chance_f(0.5f)),
--       т.е. это НЕ «общий на группу», а «очень редкий дроп».
--       [LootMgr.cpp: Process(), ветка `if(item->shared)`]
--       Для обычного лута ставь shared=0.
--
--  5) Колонка `mincountOrRef`:
--       > 0  → мин. количество
--       <  0 → ссылка на reference_loot_template (id = |значение|)  [LootMgr.cpp:283]
--
--  6) Колонка `maxcount` для предмета должна быть < 255, иначе строка скипается.
--       [LootMgr.cpp: проверка maxcount > uint8::max]
--
--  7) Ограничение дублей внутри одного лута (LootMgr.cpp: Process):
--       надеваемые предметы (InventoryType != 0) → не более 1 шт.
--       ненадеваемые (InventoryType == 0)        → не более 3 шт.
--
--  8) Классификационная маска: только zone_loot_template имеет
--     колонку ClassificationMask (сверяется с loot._ClassificationMask).
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки
-- ---------------------------------------------------------------------
SELECT entry FROM `creature_loot_template` WHERE entry = @LOOT_ENTRY;
SELECT * FROM `creature_loot_template` WHERE entry = @LOOT_ENTRY ORDER BY groupid, item;
SELECT entry, name, lootid, pickpocketloot, skinloot FROM `creature_template` WHERE entry = @MOB;
SELECT entry FROM `reference_loot_template` WHERE entry = @REF_ENTRY;


-- ---------------------------------------------------------------------
-- 1. Простой независимый дроп (groupid = 0)
-- ---------------------------------------------------------------------
DELETE FROM `creature_loot_template` WHERE entry = @LOOT_ENTRY AND item = @ITEM;
INSERT INTO `creature_loot_template`
(`entry`, `item`, `ChanceOrQuestChance`, `lootmode`, `groupid`, `mincountOrRef`, `maxcount`, `shared`)
VALUES
(@LOOT_ENTRY, @ITEM, 25, 0, 0, 1, 1, 0);   -- 25%, 1 шт., не квестовый, shared=0

-- Квестовый предмет (падает только при активном квесте):
INSERT INTO `creature_loot_template`
(`entry`, `item`, `ChanceOrQuestChance`, `lootmode`, `groupid`, `mincountOrRef`, `maxcount`, `shared`)
VALUES
(@LOOT_ENTRY, @QUEST_ITEM, -100, 0, 0, 1, 1, 0);  -- «-» = квестовый, 100%

-- Валюта (отрицательный item):
-- INSERT INTO `creature_loot_template`
-- (`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`,`shared`)
-- VALUES (@LOOT_ENTRY, -@CURRENCY_ID, 100, 0, 0, 1, 5, 0);   -- 1..5 единиц валюты


-- ---------------------------------------------------------------------
-- 2. Привязать таблицу лута к мобу
-- ---------------------------------------------------------------------
UPDATE `creature_template` SET lootid = @LOOT_ENTRY WHERE entry = @MOB;
-- Если lootid уже задан и общий для нескольких мобов — лучше добавить строки
-- в существующий entry, чем менять lootid.


-- ---------------------------------------------------------------------
-- 3. Группа «РОВНО ОДИН из N» (groupid > 0)
--    Из группы всегда выпадает один предмет. Веса = ChanceOrQuestChance.
--    ВАЖНО: у каждой строки группы groupid>0 И mincountOrRef>0, иначе
--    ядро не положит её в группу (см. особенность 3).
--    Строка-заглушка с item=0 НЕ нужна.
-- ---------------------------------------------------------------------
DELETE FROM `creature_loot_template` WHERE entry = @LOOT_ENTRY AND groupid = 1;
INSERT INTO `creature_loot_template`
(`entry`, `item`, `ChanceOrQuestChance`, `lootmode`, `groupid`, `mincountOrRef`, `maxcount`, `shared`)
VALUES
(@LOOT_ENTRY, @ITEM_A, 60, 0, 1, 1, 1, 0),
(@LOOT_ENTRY, @ITEM_B, 30, 0, 1, 1, 1, 0),
(@LOOT_ENTRY, @ITEM_C, 10, 0, 1, 1, 1, 0);
-- Веса 60/30/10: шанс каждого = вес / сумма_весов, при условии что группа сработала.
-- Сумма не обязана быть 100.


-- ---------------------------------------------------------------------
-- 4. Общая группа через reference_loot_template
--    (колонки: entry, item, ChanceOrQuestChance, lootmode, groupid,
--     mincountOrRef, maxcount — БЕЗ shared/ClassificationMask)
-- ---------------------------------------------------------------------
DELETE FROM `reference_loot_template` WHERE entry = @REF_ENTRY;
INSERT INTO `reference_loot_template`
(`entry`, `item`, `ChanceOrQuestChance`, `lootmode`, `groupid`, `mincountOrRef`, `maxcount`)
VALUES
(@REF_ENTRY, @ITEM_A, 50, 0, 0, 1, 1),
(@REF_ENTRY, @ITEM_B, 30, 0, 0, 1, 1),
(@REF_ENTRY, @ITEM_C, 20, 0, 0, 1, 3);   -- 1..3 шт.

-- Подключить к дропу моба (отрицательное mincountOrRef = ссылка, шанс в строке):
INSERT INTO `creature_loot_template`
(`entry`, `item`, `ChanceOrQuestChance`, `lootmode`, `groupid`, `mincountOrRef`, `maxcount`, `shared`)
VALUES
(@LOOT_ENTRY, 0, 100, 0, 0, -@REF_ENTRY, 1, 0);


-- ---------------------------------------------------------------------
-- 5. Лут с объектов (сундуки, руда, травы)
--    Колонки: entry, item, ChanceOrQuestChance, lootmode, groupid,
--             mincountOrRef, maxcount  (БЕЗ shared — его нет в этой таблице!)
--    Привязка: gameobject_template.data1 = loot entry.
-- ---------------------------------------------------------------------
-- DELETE FROM `gameobject_loot_template` WHERE entry = @GO_LOOT;
-- INSERT INTO `gameobject_loot_template`
-- (`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`)
-- VALUES (@GO_LOOT, @ITEM, 100, 0, 0, 1, 1);


-- ---------------------------------------------------------------------
-- 6. Содержимое предмета-контейнера
-- ---------------------------------------------------------------------
-- DELETE FROM `item_loot_template` WHERE entry = @CONTAINER_ITEM;
-- INSERT INTO `item_loot_template`
-- (`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`)
-- VALUES (@CONTAINER_ITEM, @ITEM, 100, 0, 0, 1, 5);


-- ---------------------------------------------------------------------
-- 7. Таблицы, специфичные для Nordrassil (ядро их читает — проверено)
-- ---------------------------------------------------------------------

-- 7a. Зональный лут (есть ClassificationMask):
--     entry, item, ChanceOrQuestChance, lootmode, groupid,
--     mincountOrRef, maxcount, ClassificationMask
-- INSERT INTO `zone_loot_template`
-- (`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`,`ClassificationMask`)
-- VALUES (@ZONE_LOOT, @ITEM, 50, 0, 0, 1, 1, 0);

-- 7b. Мировой лут: entry, item, ChanceOrQuestChance, lootmode, groupid,
--                  mincountOrRef, maxcount
-- INSERT INTO `world_loot_template`
-- (`entry`,`item`,`ChanceOrQuestChance`,`lootmode`,`groupid`,`mincountOrRef`,`maxcount`)
-- VALUES (@WORLD_LOOT, @ITEM, 100, 0, 0, 1, 1);

-- 7c. Персональный лут (Legion):
--     entry, type, chance, lootspellId, bonusspellId, cooldownid, cooldowntype,
--     goEntry, respawn, comment
-- INSERT INTO `personal_loot_template`
-- (`entry`,`type`,`chance`,`lootspellId`,`bonusspellId`,`cooldownid`,`cooldowntype`,
--  `goEntry`,`respawn`,`comment`)
-- VALUES (@PL_ENTRY, 0, 100, @LOOT_SPELL, @BONUS_SPELL, 0, 0, 0, 0, 'мой персональный лут');

-- 7d. Бонусы к предмету из лута:
--     item_enchantment_bonus: BonusID, chance
--     loot_item_bonus:        ItemID, bonusID, count
-- INSERT INTO `item_enchantment_bonus` (`BonusID`,`chance`) VALUES (@BONUS_ID, 30);
-- INSERT INTO `loot_item_bonus` (`ItemID`,`bonusID`,`count`) VALUES (@ITEM, @BONUS_ID, 1);


-- ---------------------------------------------------------------------
-- 8. Условия дропа
--    conditions: SourceTypeOrReferenceId, SourceGroup, SourceEntry, SourceId,
--                ElseGroup, ConditionTypeOrReference, ConditionTarget,
--                ConditionValue1..3, NegativeCondition, ErrorTextId, ScriptName, Comment
--    Для лута существ SourceTypeOrReferenceId = CONDITION_SOURCE_TYPE_CREATURE_LOOT_TEMPLATE
-- ---------------------------------------------------------------------
-- INSERT INTO `conditions`
-- (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,
--  `ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,
--  `ConditionValue3`,`NegativeCondition`,`ErrorTextId`,`ScriptName`,`Comment`)
-- VALUES (1, @LOOT_ENTRY, @ITEM, 0, 0, @COND_TYPE, 0, @V1, 0, 0, 0, 0, '', 'условие дропа');


-- ---------------------------------------------------------------------
-- 9. Применить  (команды СВЕРЕНЫ с cs_reload.cpp этого ядра)
-- ---------------------------------------------------------------------
--   .reload creature_loot_template
--   .reload reference_loot_template
--   .reload gameobject_loot_template
--   .reload item_loot_template
--   .reload disenchant_loot_template
--   .reload fishing_loot_template
--   .reload milling_loot_template
--   .reload pickpocketing_loot_template
--   .reload prospecting_loot_template
--   .reload mail_loot_template
--   .reload zone_loot_template      ← специфика ядра
--   .reload world_loot_template     ← специфика ядра
--   .reload luck_loot_template      ← специфика ядра
--   .reload conditions
--
--   .reload all loot                ← перезагрузить все лут-таблицы разом
--
-- НЕТ прямой команды .reload для: personal_loot_template, item_enchantment_bonus,
-- loot_item_bonus → после их правки нужен рестарт worldserver.
