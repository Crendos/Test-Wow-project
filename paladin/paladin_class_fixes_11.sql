-- ============================================================================
-- Paladin class fixes — часть 11 (28.09.2026): героические таланты, Гильотина,
-- Божественный арбитр (T36 Ret 4pc) — бонусы выпускающего спендера.
-- Спутник к paladin/spell_paladin_class_fixes_11.cpp (+ классы T36 в основной части).
-- Повторный прогон безопасен.
-- ============================================================================

-- Старая (неверная) Доблесть: «Слово славы -> КД БЗ/БП/БС/ЩБ». Класс удалён из части 7.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_pal_valiance_ex';

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Гильотина (аксессуар 270173): прок и урон от недостающего здоровья
(1291728, 'spell_item_guillotine_proc_ex'),
(1306604, 'spell_item_guillotine_damage_ex'),
-- T36 Ret 4pc: Буря +200% / Приговор, Вердикт +100% у спендера, выпускающего арбитра
(53385,  'spell_pal_t36_divine_arbiter_bonus_ex'),
(224239, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(423593, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(383328, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(85256,  'spell_pal_t36_divine_arbiter_bonus_ex'),
(224266, 'spell_pal_t36_divine_arbiter_bonus_ex'),
-- «Божественная сила: Буря» 1306159: эффект обнулён, бонус считает скрипт выше
(1306159, 'spell_pal_t36_divine_power_storm_ex'),
-- Храмовник: криты Эмпирейского молота -> Гнев нисхождения / Судия Света
(431398, 'spell_pal_empyrean_hammer_crit_ex'),
(431551, 'spell_pal_hero_proc_disabled_ex'),
(1261525,'spell_pal_hero_proc_disabled_ex'),
-- разлёт урона без основной цели
(431625, 'spell_pal_splash_skip_primary_ex'),
(431399, 'spell_pal_splash_skip_primary_ex'),
-- Вестник солнца: Аврора
(255937, 'spell_pal_aurora_ex'),
(114165, 'spell_pal_aurora_ex'),
(375576, 'spell_pal_aurora_ex'),
-- Вестник солнца: DoT Рассветного света (разлёт 8%, Аватар солнца, Затяжное сияние)
(431380, 'spell_pal_dawnlight_dot_ex'),
-- Кузнец света: Молот и наковальня (крит Правосудия Прота)
(275779, 'spell_pal_hammer_and_anvil_ex'),
-- Кузнец света: Сложить оружие (доспех спал с паладина)
(432502, 'spell_pal_laying_down_arms_ex'),
(432496, 'spell_pal_laying_down_arms_ex'),
-- Кузнец света: Доблесть (трата Сияющего света / Наставления Света)
(85673,  'spell_pal_valiance_consume_ex'),
(19750,  'spell_pal_valiance_consume_ex'),
(82326,  'spell_pal_valiance_consume_ex'),
(275773, 'spell_pal_valiance_consume_ex');

-- Гильотина: строка spell_proc НЕ нужна — 1291728 (Dummy) получает прок из DB2
-- (ProcFlags + ProcBasePPM ~3 с хастом) автоматически. Не добавляйте строку с PPM = 0.
-- Родные проки 431551/1261525 гасит spell_pal_hero_proc_disabled_ex (CheckProc = false).
