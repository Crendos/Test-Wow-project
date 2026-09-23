-- Playerbots v3 «классовое знание»: ручные правила применения спелов (per-class).
-- Если таблицы для класса НЕТ — бот сам построит ротацию из своего спелбукка
-- (авто-классификация эффектов SpellInfo: Damage/Heal/SelfBuff/Defensive/Interrupt).

CREATE TABLE IF NOT EXISTS `playerbots_class_knowledge` (
  `id`            INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `class_id`      TINYINT UNSIGNED NOT NULL,               -- 1 Warrior, 2 Paladin, 3 Hunter, ... (Enum Classes)
  `spellid`       INT UNSIGNED NOT NULL,
  `kind`          TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- 0=Damage, 1=Heal, 2=SelfBuff, 4=Defensive, 5=Interrupt
  `priority`      SMALLINT UNSIGNED NOT NULL DEFAULT 100,  -- больше = раньше в цикле
  `self_hp_max`   TINYINT UNSIGNED NOT NULL DEFAULT 100,   -- % HP бота, ниже которого спел доступен (для Defensive/Heal)
  `target_hp_min` TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- % HP цели (для execute-подобных)
  `target_hp_max` TINYINT UNSIGNED NOT NULL DEFAULT 100,
  `maintain_aura` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,  -- self-buff: кастуем, пока ауры нет у бота
  PRIMARY KEY (`id`),
  KEY `class_id` (`class_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Примеры (ID проверяйте по spell DBC вашего билда — двигатель сам отбрасывает невалидные):
-- Mage (Fire-набор):
-- INSERT INTO playerbots_class_knowledge (class_id, spellid, kind, priority) VALUES
--   (8,  133,  0, 100),   -- Fireball (спам-филлер)
--   (8,  108853, 0, 120), -- Fire Blast (инстант)
--   (8,  45438, 4, 500),  -- Ice Block (в примерах: kind=4 Defensive → self_hp_max=35)
--   (8,  543,  2, 40);    -- Mage Armor (self-buff, maintain_aura=1 в бою)
-- UPDATE playerbots_class_knowledge SET self_hp_max=35 WHERE spellid=45438;
-- UPDATE playerbots_class_knowledge SET maintain_aura=1 WHERE kind IN (2);

-- Paladin (ближний набор):
-- INSERT INTO playerbots_class_knowledge (class_id, spellid, kind, priority) VALUES
--   (2, 35395, 0, 140),   -- Crusader Strike
--   (2, 20271, 0, 110),   -- Judgment
--   (2,  642,  4, 500);   -- Divine Shield (Defensive)
-- UPDATE playerbots_class_knowledge SET self_hp_max=35 WHERE spellid=642;
