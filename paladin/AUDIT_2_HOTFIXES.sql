-- =============================================================================
-- AUDIT_2_HOTFIXES.sql  —  ВЕСЬ АУДИТ ДЛЯ БД `hotfixes` (один прогон целиком)
-- Подключение: HeidiSQL/mysql -> база `hotfixes`. Больше НИЧЕГО открывать не надо.
-- Содержит:1) маски/шансы всех отслеживаемых аур,2) аплееры,3) свип широких масок.
-- После выполнения: вывод шагов1+2+3 -> ОДНИМ сообщением агенту.
-- =============================================================================

-- ШАГ1. МАСКИ/ШАНСЫ всех отслеживаемых аур
-- ОЖИДАЕМ:427445=0/0 (PROC_FIX),432626=0/0,386732=0/0,386738=0/0,
--          378405=16384/0,209388 Chance=101,386652 Chance=100 CumulativeAura=5
SELECT ao.SpellID, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance, ao.CumulativeAura, ao.ProcInterval
FROM spell_aura_options AS ao
WHERE ao.SpellID IN (427445,432626,386732,386738,378405,378412,209388,386652,
                     427441,432629,386730,1266308,321136,469309,469883,157047,431474)
ORDER BY ao.SpellID;

-- ШАГ2. АПЛЕЕРЫ: кто накладывает бафы (цепочки DBC:378405->378412,386732->386730,
--        427445->427441,432626->432629)
SELECT e.ID, e.Effect, e.EffectTriggerSpell, e.EffectMiscValue, e.EffectBasePoints
FROM spell_effect AS e
WHERE e.EffectTriggerSpell IN (378412,386730,427441,432629,386732,386731)
   OR e.EffectMiscValue   IN (378412,386730,427441,432629);

-- ШАГ3. СВИП ШИРОКИХ МАСОК по всей базе (болезнь §8: прок «от каста чего угодно»).
--        Наши три (427445/432626/386732) обнулены — в выводе быть не должно.
--        Пусто = других жертв нет. Непусто -> прислать агенту с именами спеллов.
SELECT ao.SpellID, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance, ao.CumulativeAura
FROM spell_aura_options AS ao
WHERE (ao.ProcTypeMask2 & 4) <> 0
   OR (ao.ProcTypeMask1 & 2446336) = 2446336
ORDER BY ao.SpellID;
