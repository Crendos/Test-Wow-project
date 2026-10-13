-- Playerbots MVP-фаза 0: таблицы в world-БД
-- (названия таблиц/столбцов соответствуют src/bot/PlayerbotMgr.cpp как есть)

-- Состав ротации: откуда менеджер берёт окно онлайн-ботов.
-- guid/name дублируют characters; classId — для будущей балансировки.
CREATE TABLE IF NOT EXISTS `playerbots_rotation` (
  `rotation_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `guid` BIGINT UNSIGNED NOT NULL,
  `name` VARCHAR(12) NOT NULL,
  `classId` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`rotation_id`),
  UNIQUE KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Персональная полная ротация бота. Старые три колонки сохранены для совместимости.
-- `priority` в этой legacy-таблице хранится обратно: меньшее DB-значение шло первым;
-- runtime декодирует его в BotKnowledge.priority (больше = выше). Автоснимок через
-- .playerbots spells sync сохраняет kind/приоритет/HP-условия/aura/burst без потерь.
-- Старые INSERT только с (name,spellid,priority) остаются допустимы и читаются как Damage.
CREATE TABLE IF NOT EXISTS `playerbots_combat_spells` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `name` VARCHAR(12) NOT NULL,
  `spellid` INT UNSIGNED NOT NULL,
  `priority` SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,             -- BotKnowledge::Kind (0..7)
  `self_hp_max` TINYINT UNSIGNED NOT NULL DEFAULT 100,
  `target_hp_min` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `target_hp_max` TINYINT UNSIGNED NOT NULL DEFAULT 100,
  `maintain_aura` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
  `burst` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Пример: охотник-питомец и один бой-скилл
-- INSERT INTO playerbots_rotation (guid,name,classId) VALUES (90001,'Testbotone',3);
-- INSERT INTO playerbots_combat_spells (name,spellid,priority) VALUES ('Testbotone',3044,1); -- Arcane Shot
