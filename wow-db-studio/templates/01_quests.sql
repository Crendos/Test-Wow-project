-- =====================================================================
--  01_quests.sql — шаблоны заданий для Nordrassil Core 7.3.5
--  Все имена колонок сверены с дампом legion_world.sql
-- =====================================================================
--  Плейсхолдеры:  @QUEST      — ID квеста (свободный, проверь!)
--                 @NPC_START  — entry НПЦ, который выдаёт квест
--                 @NPC_END    — entry НПЦ, который принимает квест
--                 @MOB        — entry моба-цели
--                 @ITEM       — entry предмета-цели
--                 @LEVEL      — уровень квеста
--  Перед применением:  mysqldump legion_world quest_template quest_objectives
--                      quest_template_addon quest_template_locale > backup.sql
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки до начала
-- ---------------------------------------------------------------------
SELECT 'ЗАНЯТ quest_template' AS chk, ID FROM `quest_template`    WHERE ID = @QUEST
UNION ALL SELECT 'ЗАНЯТ quest_objectives', ID      FROM `quest_objectives`   WHERE QuestID = @QUEST
UNION ALL SELECT 'ЗАНЯТ addon',            ID      FROM `quest_template_addon` WHERE ID = @QUEST;

-- Найти свободный ID квеста выше текущего максимума:
SELECT MAX(ID) + 10 AS suggested_quest_id FROM `quest_template`;


-- ---------------------------------------------------------------------
-- 1. Тело квеста
--    ВНИМАНИЕ: в Legion колонки LogTitle / LogDescription / QuestDescription
--    в quest_template хранят ID из broadcast_text, а не текст!
--    Текст пишется в quest_template_locale (см. шаг 4).
-- ---------------------------------------------------------------------
DELETE FROM `quest_template` WHERE ID = @QUEST;
INSERT INTO `quest_template`
(`ID`, `QuestType`, `QuestLevel`, `QuestMaxScalingLevel`, `MinLevel`, `QuestSortID`,
 `SuggestedGroupNum`, `RewardNextQuest`, `RewardXPDifficulty`, `RewardXPMultiplier`,
 `RewardMoney`, `RewardMoneyDifficulty`, `RewardMoneyMultiplier`, `RewardBonusMoney`,
 `RewardSpell`, `RewardArtifactXP`, `RewardArtifactXPMultiplier`,
 `Flags`, `FlagsEx`,
 `RewardItem1`, `RewardAmount1`,
 `RewardChoiceItemID1`, `RewardChoiceItemQuantity1`,
 `RewardChoiceItemID2`, `RewardChoiceItemQuantity2`,
 `RewardFactionID1`, `RewardFactionValue1`, `RewardFactionOverride1`,
 `RewardCurrencyID1`, `RewardCurrencyQty1`,
 `POIContinent`, `POIx`, `POIy`, `POIPriority`,
 `PortraitGiver`, `PortraitTurnIn`,
 `TimeAllowed`, `AllowableRaces`, `Expansion`, `VerifiedBuild`)
VALUES
(@QUEST,        -- ID
 2,             -- QuestType: 2 = обычный (значение по умолчанию в этой базе)
 @LEVEL,        -- QuestLevel (-1 = брать уровень НПЦ)
 -1,            -- QuestMaxScalingLevel (-1 = без масштабирования)
 @LEVEL - 5,    -- MinLevel
 0,             -- QuestSortID (0 = без категории; >0 — зона/линейка)
 0,             -- SuggestedGroupNum (0 = соло, 3 = группа)
 0,             -- RewardNextQuest (ID следующего квеста цепочки, 0 = нет)
 5,             -- RewardXPDifficulty (индекс награды опытом)
 1,             -- RewardXPMultiplier
 15400,         -- RewardMoney (медь)
 1,             -- RewardMoneyDifficulty
 1,             -- RewardMoneyMultiplier
 0,             -- RewardBonusMoney
 0,             -- RewardSpell
 0, 0,          -- RewardArtifactXP, RewardArtifactXPMultiplier
 0,             -- Flags
 0,             -- FlagsEx
 0, 0,          -- RewardItem1, RewardAmount1
 0, 0,          -- RewardChoiceItemID1, Quantity1
 0, 0,          -- RewardChoiceItemID2, Quantity2
 0, 0, 0,       -- RewardFactionID1, Value1, Override1
 0, 0,          -- RewardCurrencyID1, Qty1
 0, 0, 0, 0,    -- POI (континент, X, Y, приоритет)
 0, 0,          -- PortraitGiver / PortraitTurnIn (creature display id)
 0,             -- TimeAllowed (0 = без таймера)
 0,             -- AllowableRaces (0 = все расы)
 0,             -- Expansion
 0);            -- VerifiedBuild


-- ---------------------------------------------------------------------
-- 2. Цели квеста
--    Типы (QuestDef.h, строки 117-134 ядра):
--      0 = MONSTER          1 = ITEM              2 = GAMEOBJECT
--      3 = TALKTO           4 = CURRENCY          5 = LEARNSPELL
--      6 = MIN_REPUTATION   7 = MAX_REPUTATION    8 = MONEY
--      9 = PLAYERKILLS     10 = AREATRIGGER      11 = PET_TRAINER_DEFEAT
--     12 = DEFEATBATTLEPET 13 = PET_BATTLE_VICTORIES
--     14 = COMPLETE_CRITERIA_TREE 15 = TASK_IN_ZONE
--     16 = HAVE_CURRENCY   17 = OBTAIN_CURRENCY
--    Флаги (QuestDef.h, строки 344-350):
--      0x01 TRACKED_ON_MINIMAP   0x02 SEQUENCED (строго по порядку)
--      0x04 OPTIONAL             0x08 HIDDEN
--      0x10 HIDE_ITEM_GAINS      0x20 PROGRESS_COUNTS_ITEMS_IN_INVENTORY
--      0x40 PART_OF_PROGRESS_BAR
--    ID в quest_objectives — глобально уникальный (не = ID квеста).
-- ---------------------------------------------------------------------
DELETE FROM `quest_objectives` WHERE QuestID = @QUEST;

-- 2a. Цель «убить N мобов»
INSERT INTO `quest_objectives`
(`ID`, `QuestID`, `Type`, `StorageIndex`, `ObjectID`, `Amount`, `Flags`, `Flags2`, `TaskStep`, `Description`, `VerifiedBuild`)
VALUES
(@QUEST * 10 + 0, @QUEST, 0, 0, @MOB, 8, 0, 0, 0, 'Убить существ: $amount', 0);

-- 2b. Цель «собрать N предметов» (с мобов)
INSERT INTO `quest_objectives`
(`ID`, `QuestID`, `Type`, `StorageIndex`, `ObjectID`, `Amount`, `Flags`, `Flags2`, `TaskStep`, `Description`, `VerifiedBuild`)
VALUES
(@QUEST * 10 + 1, @QUEST, 1, 1, @ITEM, 6, 0x20, 0, 0, 'Собрать предмет: $amount', 0);

-- 2c. Цель «поговорить с НПЦ»
-- INSERT INTO `quest_objectives` (`ID`,`QuestID`,`Type`,`StorageIndex`,`ObjectID`,`Amount`,`Flags`,`Flags2`,`TaskStep`,`Description`,`VerifiedBuild`)
-- VALUES (@QUEST*10+2, @QUEST, 3, 2, @NPC_END, 1, 0, 0, 0, 'Поговорить с кем-то', 0);


-- ---------------------------------------------------------------------
-- 3. Цепочка, классы, репутация
-- ---------------------------------------------------------------------
DELETE FROM `quest_template_addon` WHERE ID = @QUEST;
INSERT INTO `quest_template_addon`
(`ID`, `MaxLevel`, `AllowableClasses`, `SourceSpellID`, `PrevQuestID`, `NextQuestID`,
 `ExclusiveGroup`, `RewardMailTemplateID`, `RewardMailDelay`, `RewardMailTitle`,
 `RequiredSkillID`, `RequiredSkillPoints`,
 `RequiredMinRepFaction`, `RequiredMaxRepFaction`, `RequiredMinRepValue`, `RequiredMaxRepValue`,
 `ProvidedItemCount`, `SpecialFlags`)
VALUES
(@QUEST, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
-- AllowableClasses: -1 = все классы, иначе битовая маска
-- PrevQuestID: >0 = нужно выполнить, <0 = нужно активно в логе
-- ExclusiveGroup: квесты одной группы взаимоисключающие


-- ---------------------------------------------------------------------
-- 4. ТЕКСТЫ (ruRU) — именно отсюда клиент берёт читаемый текст
-- ---------------------------------------------------------------------
DELETE FROM `quest_template_locale` WHERE ID = @QUEST AND locale = 'ruRU';
INSERT INTO `quest_template_locale`
(`ID`, `locale`, `LogTitle`, `LogDescription`, `QuestDescription`, `AreaDescription`,
 `PortraitGiverText`, `PortraitGiverName`, `PortraitTurnInText`, `PortraitTurnInName`,
 `QuestCompletionLog`, `VerifiedBuild`)
VALUES
(@QUEST, 'ruRU',
 'Название квеста',
 'Краткое описание в журнале (цель).',
 'Полный текст при выдаче.$b$bНовый абзац — через $b',
 'Текст точки на карте.',
 'Текст портрета выдающего НПЦ', 'Имя выдающего НПЦ',
 'Текст портрета принимающего НПЦ', 'Имя принимающего НПЦ',
 'Текст при сдаче квеста.',
 0);


-- ---------------------------------------------------------------------
-- 5. Кто выдаёт и кто принимает
-- ---------------------------------------------------------------------
DELETE FROM `creature_queststarter` WHERE id = @NPC_START AND quest = @QUEST;
INSERT INTO `creature_queststarter` (`id`, `quest`) VALUES (@NPC_START, @QUEST);

DELETE FROM `creature_questender` WHERE id = @NPC_END AND quest = @QUEST;
INSERT INTO `creature_questender` (`id`, `quest`) VALUES (@NPC_END, @QUEST);

-- Выдача квеста предметом/объектом:
-- INSERT INTO `gameobject_queststarter` (`id`, `quest`) VALUES (@GO, @QUEST);
-- INSERT INTO `gameobject_questender`   (`id`, `quest`) VALUES (@GO, @QUEST);


-- ---------------------------------------------------------------------
-- 6. Эмоции на экранах выдачи/сдачи
-- ---------------------------------------------------------------------
DELETE FROM `quest_details` WHERE ID = @QUEST;
INSERT INTO `quest_details` (`ID`, `Emote1`, `Emote2`, `Emote3`, `Emote4`,
  `EmoteDelay1`, `EmoteDelay2`, `EmoteDelay3`, `EmoteDelay4`, `VerifiedBuild`)
VALUES (@QUEST, 1, 0, 0, 0, 0, 0, 0, 0, 0);

DELETE FROM `quest_offer_reward` WHERE ID = @QUEST;
INSERT INTO `quest_offer_reward` (`ID`, `Emote1`, `Emote2`, `Emote3`, `Emote4`,
  `EmoteDelay1`, `EmoteDelay2`, `EmoteDelay3`, `EmoteDelay4`, `RewardText`, `VerifiedBuild`)
VALUES (@QUEST, 1, 0, 0, 0, 0, 0, 0, 0, 'Отличная работа, $n. Вот твоя награда.', 0);

DELETE FROM `quest_request_items` WHERE ID = @QUEST;
INSERT INTO `quest_request_items` (`ID`, `EmoteOnComplete`, `EmoteOnIncomplete`,
  `EmoteOnCompleteDelay`, `EmoteOnIncompleteDelay`, `CompletionText`, `VerifiedBuild`)
VALUES (@QUEST, 1, 25, 0, 0, 'Ты принёс то, что я просил?', 0);


-- ---------------------------------------------------------------------
-- 7. Точка на карте (жёлтая метка)
-- ---------------------------------------------------------------------
-- DELETE FROM `quest_poi` WHERE QuestID = @QUEST;
-- INSERT INTO `quest_poi` (`QuestID`,`BlobIndex`,`Idx1`,`ObjectiveIndex`,`QuestObjectiveID`,
--   `QuestObjectID`,`MapID`,`WorldMapAreaId`,`Floor`,`Priority`,`Flags`,`WorldEffectID`,
--   `PlayerConditionID`,`WoDUnk1`,`AlwaysAllowMergingBlobs`,`VerifiedBuild`)
-- VALUES (@QUEST, 0, 0, 0, @QUEST*10, @MOB, @MAP, @WM_AREA, 0, 0, 0, 0, 0, 0, 0, 0);
-- DELETE FROM `quest_poi_points` WHERE QuestID = @QUEST;
-- INSERT INTO `quest_poi_points` (`QuestID`,`Idx1`,`Idx2`,`X`,`Y`,`VerifiedBuild`)
-- VALUES (@QUEST, 0, 0, @X, @Y, 0);


-- ---------------------------------------------------------------------
-- 8. Скрипт на выдачу/сдачу (DB-скрипт, не SmartAI)
--    quest_start_scripts / quest_end_scripts:
--    id, delay, command, datalong, datalong2, dataint, x, y, z, o
--    Привязка: quest_template.StartScript / CompleteScript
-- ---------------------------------------------------------------------
-- DELETE FROM `quest_start_scripts` WHERE id = @QUEST;
-- INSERT INTO `quest_start_scripts` (`id`,`delay`,`command`,`datalong`,`datalong2`,`dataint`,`x`,`y`,`z`,`o`)
-- VALUES (@QUEST, 0, 0, 0, 0, 0, 0, 0, 0, 0);


-- ---------------------------------------------------------------------
-- 9. Диалог НПЦ (если квест выдаётся через gossip)
-- ---------------------------------------------------------------------
-- gossip_menu: Entry, TextID, FriendshipFactionID
-- gossip_menu_option: MenuID, OptionIndex, OptionNPC, OptionText, OptionType,
--   OptionNpcflag, OptionNpcflag2, ActionMenuID, ActionPoiID, BoxCoded, BoxMoney,
--   BoxCurrency, BoxText, OptionBroadcastTextID, BoxBroadcastTextID, VerifiedBuild
--
-- DELETE FROM `gossip_menu_option` WHERE MenuID = @MENU AND OptionIndex = 0;
-- INSERT INTO `gossip_menu_option`
-- (`MenuID`,`OptionIndex`,`OptionNPC`,`OptionText`,`OptionType`,`OptionNpcflag`,`OptionNpcflag2`,
--  `ActionMenuID`,`ActionPoiID`,`BoxCoded`,`BoxMoney`,`BoxCurrency`,`BoxText`,
--  `OptionBroadcastTextID`,`BoxBroadcastTextID`,`VerifiedBuild`)
-- VALUES (@MENU, 0, 0, 'У меня есть задание для тебя', 2, 2, 0, 0, 0, 0, 0, 0, '', 0, 0, 0);


-- ---------------------------------------------------------------------
-- 10. Применить  (команды СВЕРЕНЫ с cs_reload.cpp этого ядра)
-- ---------------------------------------------------------------------
-- В консоли worldserver (нужен уровень SEC_ADMINISTRATOR):
--
--   .reload quest_template
--     └ ОДНА эта команда через QuestDataStoreMgr::LoadQuests() перезагружает
--       СРАЗУ: quest_template, quest_details, quest_request_items,
--       quest_offer_reward, quest_template_addon, quest_objectives,
--       quest_visual_effect.  (отдельных .reload для них в ядре НЕТ)
--
--   .reload locales_quest        ← ТЕКСТЫ из quest_template_locale
--   .reload creature_queststarter
--   .reload creature_questender
--   .reload gameobject_queststarter   (если выдаёт объект)
--   .reload gameobject_questender
--   .reload quest_poi                   (если делал точку на карте)
--   .reload quest_start_scripts         (если делал DB-скрипт выдачи)
--   .reload quest_end_scripts           (если делал DB-скрипт сдачи)
--   .reload gossip_menu / .reload gossip_menu_option  (если правил диалог)
--   .reload conditions
--
-- Либо перезапустить worldserver.
-- ВАЖНО: у квеста в quest_template поля текстов — это ID broadcast_text,
-- а читаемый текст берётся из quest_template_locale (ruRU). Не забудь locales_quest.
