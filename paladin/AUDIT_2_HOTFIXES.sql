-- =============================================================================
-- AUDIT_2_HOTFIXES.sql — база hotfixes.
-- 26.09: шаг 1 падал на колонке ProcInterval (её нет; есть ProcCategoryRecovery).
--        шаг 2 падал на EffectMiscValue (колонка называется EffectMiscValue1).
--        шаг 3 уже прислан (1400 строк) — повторно не нужен, оставлен закомментированным.
-- Выполнить шаги 1 и 2. Прислать оба вывода.
-- =============================================================================

-- ШАГ1. Маски наших аур. Колонки сверены с HotfixDatabase.cpp (SpellAuraOptions.db2).
SELECT ao.SpellID, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance, ao.CumulativeAura, ao.ProcCategoryRecovery
FROM spell_aura_options AS ao
WHERE ao.SpellID IN (
    427445,432626,386732,386738,378405,378412,209388,386652,
    427441,432629,386730,1266308,321136,469309,469883,157047,431474,
    432463,431533,425518,431687,431522,431536,198034,433674
)
ORDER BY ao.SpellID;

-- ШАГ2. Эффекты тех же спеллов + кто накладывает бафы.
-- EffectMiscValue в этой базе нет, только EffectMiscValue1 / EffectMiscValue2.
SELECT e.SpellID, e.EffectIndex, e.Effect, e.EffectAura, e.EffectTriggerSpell, e.EffectMiscValue1, e.EffectBasePoints
FROM spell_effect AS e
WHERE e.SpellID IN (
    427445,432626,386732,427441,432463,431533,425518,431687,431522,1266308,432629,386730
)
   OR e.EffectTriggerSpell IN (378412,386730,427441,432629,386732,386731,431398,431536,433674)
   OR e.EffectMiscValue1   IN (378412,386730,427441,432629)
ORDER BY e.SpellID, e.EffectIndex;

-- ШАГ3 уже получен 26.09 (1400 строк широких масок). Не запускать снова.
-- SELECT ao.SpellID, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance, ao.CumulativeAura
-- FROM spell_aura_options AS ao
-- WHERE (ao.ProcTypeMask2 & 4) <> 0
--    OR (ao.ProcTypeMask1 & 2446336) = 2446336
-- ORDER BY ao.SpellID;
