-- ---------------------------------------------------------------------------
-- playerbots v6: спеллы героических деревьев для `.playerbots create hero=<tree>`
-- Загружается в World DB (worldserver загружает world-базу; ключевая таблица
-- модуля — playerbots_hero_talents). Формат:
--   class  — CLASS_* (2 = PALADIN)
--   tree   — ключ команды hero= (нижний регистр: templar/herald/lightsmith/...)
--   spell_id — Spell ID с wowhead (https://www.wowhead.com → поиск по имени →
--              "Spell ID"; брать вариант типа Talent с нужными классами)
--
-- ВАЖНО: ядро TrinityCore НЕ реализует систему геро-талантов (HeroTalents).
-- Мы лишь УЧИМ spells этого дерева персонажу: он получает их в знания AI
-- (.playerbots book), кулдауны/расход маны учитываются, базовые эффекты
-- аур срабатывают, но «фишка» дерева (вызов Empyrean Hammers и т.п.)
-- требует spell-скриптов в core — их там пока нет. Роль дерева здесь —
-- корректный профиль персонажа + расширение ротации известными spell_id.
-- Спелл, отсутствующий в DBC клиента (другая версия патча), пропускается
-- с пометкой в «Замечания» create — можно смело держать кандидатов нескольких
-- версий: лишние просто не обучатся.
-- ---------------------------------------------------------------------------

DROP TABLE IF EXISTS `playerbots_hero_talents`;
CREATE TABLE `playerbots_hero_talents` (
  `class` tinyint unsigned NOT NULL COMMENT 'CLASS_* (2=PALADIN)',
  `tree`  varchar(32) NOT NULL COMMENT 'ключ hero= (templar/herald/lightsmith/...)',
  `spell_id` int unsigned NOT NULL COMMENT 'Spell ID (wowhead)',
  PRIMARY KEY (`class`,`tree`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='playerbots: спеллы героических деревьев (.playerbots create hero=)';

-- Paladin, Midnight 12.1. Маппинг спеков:
--   templar   = Ret + Prot,  herald = Holy + Ret,  lightsmith = Holy + Prot
-- ID сверены с wowhead (тип Talent, привязка к классам; 12.x-кандидаты — на
-- случай переработки спеллов в Midnight).
INSERT INTO `playerbots_hero_talents` (`class`,`tree`,`spell_id`) VALUES
-- Templar (Ret + Prot)
(2, 'templar', 429826),    -- Hammer of Light (keystone; wowhead spell=429826)
(2, 'templar', 427453),    -- Hammer of Light (кандидат, method.gg 12.1)
(2, 'templar', 1217116),   -- Hammer of Light (12.x-кандидат)
(2, 'templar', 1246643),   -- Hammer of Light (12.x-кандидат)
(2, 'templar', 1235934),   -- Hammer of Light (12.x-кандидат)
(2, 'templar', 425518),    -- Light's Deliverance (capstone, Talent)
(2, 'templar', 431533),    -- Shake the Heavens (Talent)
(2, 'templar', 431687),    -- Higher Calling (Talent)
(2, 'templar', 432463),    -- Hammerfall (Talent)
(2, 'templar', 1224051),   -- Hammerfall (12.x-кандидат)
-- Herald of the Sun (Holy + Ret)
(2, 'herald', 431425),     -- Sun's Avatar (capstone, Talent)
(2, 'herald', 431377),     -- Dawnlight (Talent)
(2, 'herald', 1263782),    -- Walk Into Light (12.1)
(2, 'herald', 431413),     -- Sun Sear
(2, 'herald', 431474),     -- Second Sunrise
-- Lightsmith (Holy + Prot)
(2, 'lightsmith', 433011), -- Blessing of the Forge (capstone, Talent)
(2, 'lightsmith', 1289728) -- Holy Armaments (Talent, 12.x)
;

-- Как дополнить (в т.ч. деревья других классов): открой wowhead по имени
-- ноды дерева, возьми ID варианта типа Talent с нужными классами и вставь
-- строкой выше с тем же class/tree. Полные списки нод деревьев — на
-- wowhead/icy-veins (раздел Hero Talents гайда спека).
