-- =============================================================================
-- MECH_PROC_FIX.sql — hotfixes. Снять широкие проки у талантов, которые
-- теперь выдаются узкими скриптами (часть 7/9, ревизия PAL_MECH_REV_20260926).
-- Иначе DBC и скрипт срабатывают оба: двойные молотки / стаки / продления,
-- в том числе «с любой способности», если маска = Cast Successful.
--
-- НЕ трогает 378405 (Свет титанов, узкая маска) и уже обнулённые
-- 427445 / 432626 / 386732 (PROC_FIX.sql) и 432929 (fix_432929_root.sql).
--
-- Выполнять в базе hotfixes, затем перезапуск worldserver.
-- =============================================================================

-- 0. ДО
SELECT ao.SpellID, n.Name, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance
FROM spell_aura_options ao
LEFT JOIN spell_name n ON n.ID = ao.SpellID
WHERE ao.SpellID IN (432463, 431533, 425518, 431687);

-- 1. ФИКС
UPDATE spell_aura_options
SET ProcTypeMask1 = 0, ProcTypeMask2 = 0
WHERE SpellID IN (
    432463, -- Молотопад: скрипт spell_pal_sotr_shake_heavens_ex
    431533, -- Сотрясение небес: бафф 431536 выдаёт spell_pal_hol_templar_ex (тик — у самого баффа)
    425518, -- Избавление Света: стаки вешает spell_pal_empyrean_deliverance_ex
    431687  -- Высшее призвание: продление в spell_pal_higher_calling_ex
);

-- 1b. Рассветный свет: бафф зарядов 431522 должен стакаться минимум до 2
--     (иначе SetStackAmount обрежется CumulativeAura и второй спендер не получит DoT).
UPDATE spell_aura_options
SET CumulativeAura = 2
WHERE SpellID = 431522 AND CumulativeAura < 2;

-- 2. ПОСЛЕ (маски 0/0; у 431522 CumulativeAura >= 2)
SELECT ao.SpellID, n.Name, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.CumulativeAura
FROM spell_aura_options ao
LEFT JOIN spell_name n ON n.ID = ao.SpellID
WHERE ao.SpellID IN (432463, 431533, 425518, 431687, 431522);
