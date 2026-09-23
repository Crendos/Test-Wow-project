-- =============================================================
-- v5: правила босс-механик для ботов (world DB)
-- Таблица `playerbots_boss_rules`: привязка к creature-entry босса.
-- Триггеры: boss_cast | boss_aura | bot_aura | hp_below | always
-- Действия: interrupt | run_from_boss | spread | sidestep |
--           switch_target | use_defensive | dispel_self | use_burst
-- seq: меньше = важнее (за тик исполняется одно правило сверху вниз).
-- cooldown_ms: per-rule антиспам.
-- =============================================================

CREATE TABLE IF NOT EXISTS `playerbots_boss_rules` (
  `boss_entry`   INT UNSIGNED NOT NULL,
  `trigger_type` VARCHAR(16)  NOT NULL,
  `trigger_arg`  INT UNSIGNED NOT NULL DEFAULT 0,
  `action_type`  VARCHAR(16)  NOT NULL,
  `action_arg`   INT UNSIGNED NOT NULL DEFAULT 0,
  `seq`          INT UNSIGNED NOT NULL DEFAULT 100,
  `cooldown_ms`  INT UNSIGNED NOT NULL DEFAULT 1500,
  `comment`      VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`boss_entry`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- -------------------------------------------------------------
-- Murder Row (первый данж Midnight, Quel'Thalas)
-- Босс 234648 — Kystia Manaheart (1/4, остальные в master пока не заскриптованы)
-- Spell-ID и поведение извлечены из ядерного скрипта
-- src/server/scripts/QuelThalas/MurderRow/boss_kystia_manaheart.cpp,
-- т.е. правила совпадают с реальной механикой ядра.
-- -------------------------------------------------------------

-- 10: Босс кастует Blink (474183) к случайной цели → после блинка вокруг него
--     взрывается FelNova (1223906, heroic+). Все отходят от босса на 12y.
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'boss_cast', 474183, 'run_from_boss', 12, 10, 2500, 'Kystia: Blink→FelNova у ног цели — отойти от босса');

-- 20: У босса стеки Felshield (1217989) и Nibbles (234660) стал hostile —
--     бить Nibbles, чтобы при его низком HP он дал боссу Light Infusion → Destabilized.
--     До превращения Nibbles friendly → правило молчит само (невалидная цель атаки).
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'boss_aura', 1217989, 'switch_target', 234660, 20, 1500, 'Kystia: Felshield стеки — переключиться на Nibbles, выбить Destabilized');

-- 30: На боссе Destabilized (1265412) — стан/окно урона → сжимать бурсты.
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'boss_aura', 1265412, 'use_burst', 0, 30, 5000, 'Kystia: Destabilized — босс оглушён, окно бурста');

-- 40: У босса аура Mirror Images (1264095) — образы наносят урон вокруг, распределиться.
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'boss_aura', 1264095, 'spread', 8, 40, 1500, 'Kystia: Mirror Images — разойтись от союзников на 8y');

-- 50: На боте FelSprayDamage (1253813) — стоим в fel-луке (AreaTrigger 39560) → выйти.
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'bot_aura', 1253813, 'sidestep', 8, 50, 2000, 'Kystia/Nibbles: FelSpray под ногами — шаг в сторону');

-- 60: На боте CorrodingSpittle (1228198) — корродирующий плевок (Nibbles, random).
--     Диспел с себя, если в книге есть dispel (Paladin/Priest/Monk/Druid healer).
REPLACE INTO `playerbots_boss_rules`
(`boss_entry`,`trigger_type`,`trigger_arg`,`action_type`,`action_arg`,`seq`,`cooldown_ms`,`comment`) VALUES
(234648, 'bot_aura', 1228198, 'dispel_self', 0, 60, 3000, 'Kystia/Nibbles: CorrodingSpittle — диспел с себя при наличии');
