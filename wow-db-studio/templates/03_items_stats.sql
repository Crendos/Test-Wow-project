-- =====================================================================
--  03_items_stats.sql — предметы и характеристики, Nordrassil Core 7.3.5
--  Колонки сверены с дампами legion_world.sql и legion_hotfixes.sql
-- =====================================================================
--  ГЛАВНОЕ, ЧТО НУЖНО ПОНЯТЬ ПРО LEGION:
--  Предмет в Legion описывается НЕ одной таблицей. Данные размазаны:
--    * legion_world.item_template        — «серверная» часть (7 865 строк в дампе)
--    * legion_hotfixes.item              — класс/подкласс/иконка/InventoryType (1 317)
--    * legion_hotfixes.item_sparse       — ИМЯ, ItemLevel, RequiredLevel, СОКЕТЫ,
--                                          СТАТЫ (317 строк в дампе)
--    * legion_hotfixes.item_bonus        — бонус-списки ilvl/статов (В ДАМПЕ 0 строк)
--    * legion_hotfixes.item_effect       — спеллы предмета (128)
--    * legion_hotfixes.scaling_stat_distribution / curve / curve_point (0 строк)
--
--  Вывод: новый предмет = правка БД (world + hotfixes) + ПАТЧ КЛИЕНТА
--  (item.db2 / item-sparse.db2 / item-bonus.db2 или DBCache).
--  Одним SQL новый предмет в игре не появится — клиент его не «увидит».
--
--  Для ПРАВКИ существующего предмета часто достаточно item_template + item_sparse.
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки
-- ---------------------------------------------------------------------
SELECT entry, name, class, subclass, Quality, ItemLevel, RequiredLevel, displayid
FROM `item_template` WHERE entry = @ITEM;

SELECT ID, ItemLevel, RequiredLevel, OverallQualityID, InventoryType,
       ItemStatValue1, ItemStatValue2, ItemStatValue3,
       StatModifierBonusStat1, StatModifierBonusStat2, StatModifierBonusStat3,
       SocketType1, SocketType2, SocketType3
FROM `item_sparse` WHERE ID = @ITEM;

-- Свободен ли entry для нового предмета?
SELECT entry FROM `item_template` WHERE entry = @NEW_ITEM;
SELECT ID FROM `item_sparse` WHERE ID = @NEW_ITEM;


-- ---------------------------------------------------------------------
-- 1. ПРАВКА статов существующего предмета (самый частый случай)
--    item_sparse — там живут характеристики.
--    ItemStatValue1..10      — значения статов
--    StatModifierBonusStat1..10 — ТИП стата (ID из ItemStat.db2)
--    StatPercentEditor1..10  — процентное распределение бюджета
-- ---------------------------------------------------------------------
-- Пример: поставить предмету ItemLevel=950, RequiredLevel=110 и два стата
UPDATE `item_sparse`
SET `ItemLevel` = 950,
    `RequiredLevel` = 110,
    `StatModifierBonusStat1` = 5,   -- 5 = Intellect (пример; см. ItemStat.db2)
    `ItemStatValue1` = 500,
    `StatModifierBonusStat2` = 7,   -- 7 = Stamina (пример)
    `ItemStatValue2` = 750
WHERE ID = @ITEM;

-- Типы статов (ItemStat) — СВЕРЯЙ со своим ItemStat.db2, порядок может отличаться:
--   0 = Mana, 1 = Health, 3 = Agility, 4 = Strength, 5 = Intellect,
--   6 = Spirit, 7 = Stamina, 13 = Armor, 14..21 = Resistance,
--   28 = MeleeAttackPower, 29 = RangedAttackPower, 32 = SpellPower,
--   35 = Crit, 36 = Haste, 38 = Versatility, 40 = Mastery, 44 = Leech, 49 = Speed
-- (в 7.3.5 список именно такой, но ПРОВЕРЬ по своему dbc)


-- ---------------------------------------------------------------------
-- 2. ПРАВКА «серверной» части предмета (item_template)
-- ---------------------------------------------------------------------
UPDATE `item_template`
SET `Quality` = 4,            -- 0 poor, 1 common, 2 uncommon, 3 rare, 4 epic, 5 legendary, 7 heirloom
    `BuyPrice` = 1000000,
    `SellPrice` = 250000,
    `maxcount` = 0,           -- 0 = без лимита в инвентаре
    `RequiredLevel` = 110,
    `AllowableClass` = -1,    -- -1 = все классы
    `AllowableRace` = -1      -- -1 = все расы
WHERE entry = @ITEM;


-- ---------------------------------------------------------------------
-- 3. Новый предмет: МИНИМАЛЬНЫЙ набор строк
--    (полноценный предмет всё равно требует клиентского патча)
-- ---------------------------------------------------------------------

-- 3a. item_template (world) — полный список колонок в дампе; заполняем ключевые.
--     Ниже — безопасный INSERT с явным перечнем колонок.
DELETE FROM `item_template` WHERE entry = @NEW_ITEM;
INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyCount`,
 `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`,
 `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `Material`, `sheath`,
 `MaxDurability`, `flagsCustom`, `WDBVerified`)
VALUES
(@NEW_ITEM,
 4,            -- class: 2=Weapon, 4=Armor, 0=Consumable, 3=Gem, 15=Misc
 0,            -- subclass (зависит от class)
 'Мой предмет',
 @DISPLAY_ID,  -- displayid (внешний вид)
 4,            -- Quality
 0,            -- Flags
 1,            -- BuyCount
 0,            -- BuyPrice
 0,            -- SellPrice
 0,            -- InventoryType (0=non-equip, 1=head, 5=chest, 13=weapon, ...)
 -1, -1,       -- AllowableClass, AllowableRace
 100,          -- ItemLevel
 110,          -- RequiredLevel
 0,            -- maxcount
 1,            -- stackable
 -1,           -- Material
 0,            -- sheath
 0,            -- MaxDurability
 0,            -- flagsCustom
 1);           -- WDBVerified

-- 3b. item_sparse (hotfixes) — имя и статы. 107 колонок; ниже только нужные,
--     остальные возьмут значения по умолчанию из определения таблицы.
--     ВНИМАНИЕ: INSERT с частичным списком колонок работает, если у остальных
--     есть DEFAULT. У item_sparse DEFAULT есть не у всех — при ошибке добавь колонки.
-- DELETE FROM `item_sparse` WHERE ID = @NEW_ITEM;
-- INSERT INTO `item_sparse`
-- (`ID`, `ItemLevel`, `RequiredLevel`, `OverallQualityID`, `InventoryType`,
--  `StatModifierBonusStat1`, `ItemStatValue1`,
--  `StatModifierBonusStat2`, `ItemStatValue2`,
--  `SocketType1`, `SocketType2`, `SocketType3`, `ExpansionID`, `VerifiedBuild`)
-- VALUES
-- (@NEW_ITEM, 100, 110, 4, 5,  5, 500,  7, 750,  0, 0, 0, 6, 0);

-- 3c. item (hotfixes) — класс/иконка
-- INSERT INTO `item` (`ID`, `IconFileDataID`, `ClassID`, `SubclassID`,
--   `SoundOverrideSubclass`, `Material`, `InventoryType`, `SheatheType`,
--   `ItemGroupSoundsID`, `VerifiedBuild`)
-- VALUES (@NEW_ITEM, @ICON_FILE_DATA_ID, 4, 0, -1, -1, 5, 0, 0, 0);


-- ---------------------------------------------------------------------
-- 4. Бонус-списки (item_bonus) — как в retail меняют ilvl/статы по bonusID
--    В дампе item_bonus ПУСТА (0 строк) — данные в клиентском DB2.
--    Чтобы использовать на сервере, таблицу нужно заполнить И пропатчить клиент.
--    item_bonus: ID, Value1, Value2, Value3, ParentItemBonusListID, Type, OrderIndex
-- ---------------------------------------------------------------------
-- INSERT INTO `item_bonus`
-- (`ID`,`Value1`,`Value2`,`Value3`,`ParentItemBonusListID`,`Type`,`OrderIndex`,`VerifiedBuild`)
-- VALUES (@BONUS_ID, @VALUE, 0, 0, 0, @TYPE, 0, 0);
-- Type: 1 = item level bonus, 5 = stat bonus, и т.д. (см. ItemBonus.db2)


-- ---------------------------------------------------------------------
-- 5. Спеллы предмета («использовать», проки) — item_effect
--    item_effect: ID, SpellID, CoolDownMSec, CategoryCoolDownMSec, Charges,
--                 SpellCategoryID, ChrSpecializationID, LegacySlotIndex,
--                 TriggerType, ItemID, VerifiedBuild
-- ---------------------------------------------------------------------
-- INSERT INTO `item_effect`
-- (`ID`,`SpellID`,`CoolDownMSec`,`CategoryCoolDownMSec`,`Charges`,`SpellCategoryID`,
--  `ChrSpecializationID`,`LegacySlotIndex`,`TriggerType`,`ItemID`,`VerifiedBuild`)
-- VALUES (@EFFECT_ID, @SPELL_ID, 60000, -1, 0, 0, 0, 0, 0, @ITEM, 0);
-- TriggerType: 0 = по использованию, 1 = при экипировке, 2 = при попадании, ...


-- ---------------------------------------------------------------------
-- 6. Сокеты и камни
--    SocketType1..3 в item_sparse: 1=Meta, 2=Red, 4=Yellow, 8=Blue, ...
--    SocketMatch_enchantment_id — бонус за совпадение цветов
-- ---------------------------------------------------------------------
-- UPDATE `item_sparse`
-- SET SocketType1 = 2, SocketType2 = 4, SocketType3 = 8,
--     SocketMatch_enchantment_id = @SOCKET_BONUS_ENCHANT
-- WHERE ID = @ITEM;


-- ---------------------------------------------------------------------
-- 7. Локализация названия (если нужно русское имя отдельно)
--    item_sparse_locale: ID, locale, ... (текстовые колонки)
-- ---------------------------------------------------------------------
-- Проверь структуру: DESC `item_sparse_locale`;


-- ---------------------------------------------------------------------
-- 8. Применить  (команды сверены с cs_reload.cpp)
-- ---------------------------------------------------------------------
--   .reload item                     ← item_template и связанные
--   .reload item_enchantment_template
--
-- НЕТ прямой команды .reload для: item_template_addon, item_bonus, item_effect,
-- item_sparse (hotfixes) → после их правки нужен РЕСТАРТ worldserver.
--
-- И помни: для НОВОГО предмета обязателен клиентский патч DB2/DBCache,
-- иначе клиент покажет красный «?».