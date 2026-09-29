-- ---------------------------------------------------------------------------
-- playerbots v6: спеллы героических деревьев для `.playerbots create hero=<tree>`
-- Загружается в World DB (worldserver загружает world-базу; ключевая таблица
-- модуля — playerbots_hero_talents). Формат:
--   class  — CLASS_* (2 = PALADIN)
--   tree   — ключ команды hero= (нижний регистр, напр. templar)
--   spell_id — Spell ID с wowhead (https://www.wowhead.com → поиск по имени → "Spell ID")
--
-- ВАЖНО: ядро TrinityCore НЕ реализует систему геро-талантов (HeroTalents).
-- Мы лишь УЧИМ spells этого дерева персонажу: он получает их в знания AI
-- (.playerbots book), кулдауны/расход маны учитываются, базовые эффекты
-- аур срабатывают, но «фишка» дерева (вызов Empyrean Hammers и т.п.)
-- требует spell-скриптов в core — их там пока нет. Роль дерева здесь —
-- корректный профиль персонажа + расширение ротации известными spell_id.
-- ---------------------------------------------------------------------------

DROP TABLE IF EXISTS `playerbots_hero_talents`;
CREATE TABLE `playerbots_hero_talents` (
  `class` tinyint unsigned NOT NULL COMMENT 'CLASS_* (2=PALADIN)',
  `tree`  varchar(32) NOT NULL COMMENT 'ключ hero= (templar/...)',
  `spell_id` int unsigned NOT NULL COMMENT 'Spell ID (wowhead)',
  PRIMARY KEY (`class`,`tree`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='playerbots: спеллы героических деревьев (.playerbots create hero=)';

-- Paladin / Templar (Midnight 12.x) — подтвержденный стартовый набор:
INSERT INTO `playerbots_hero_talents` (`class`,`tree`,`spell_id`) VALUES
(2, 'templar', 429826);  -- Hammer of Light — keystone-актива дерева (wowhead spell=429826)

-- Как дополнить: открой wowhead по имени спелла дерева (Light's Guidance,
-- Shake the Heavens, Hammerfall, Light's Deliverance, Higher Calling,
-- Undisputed Ruling, Templar's Watch, Bonds of Fellowship, ...) и вставь
-- его Spell ID строкой выше с тем же class/tree.
