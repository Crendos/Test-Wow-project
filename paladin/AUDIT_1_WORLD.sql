-- =============================================================================
-- AUDIT_1_WORLD.sql  —  ВЕСЬ АУДИТ ДЛЯ БД `world` (один прогон целиком)
-- Подключение: HeidiSQL/mysql -> база `world`. Больше НИЧЕГО открывать не надо.
-- Содержит: инвентарь spell_proc, фикс семейства (378285/469883 -> 10, не 2), биндинги.
-- После выполнения: вывод шагов1+3+4+5+6 -> ОДНИМ сообщением агенту.
-- =============================================================================

-- ШАГ1. КОНТРОЛЬ ДО: все spell_proc паладина (ожидаем18 строк, все SpellFamilyName=10)
SELECT SpellId, SpellFamilyName, SpellFamilyMask0, SpellFamilyMask1, SpellFamilyMask2, SpellFamilyMask3,
       ProcFlags, ProcFlags2, Chance, Cooldown
FROM spell_proc
WHERE SpellId IN (157047,267316,321136,326732,378285,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,469883,1253174,1253598,1266308)
ORDER BY SpellId;

-- ШАГ2. Гнев Тира / Очищающий огонь: маски Щита мстителя читаются только при
-- SPELLFAMILY_PALADIN=10. Значение 2 — id класса, в enum семейств его нет (unused).
-- IsAffected: familyName != SpellFamilyName спелла -> прок молча не срабатывает.
UPDATE spell_proc SET SpellFamilyName = 10
WHERE SpellId IN (378285, 469883) AND SpellFamilyName <> 10;

-- ШАГ3. ФИКС E4: остальные (маски=0) ->0 = «фильтр семейства выключен» (стандарт TDB)
UPDATE spell_proc SET SpellFamilyName = 0
WHERE SpellId IN (157047,267316,321136,326732,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,1253174,1253598,1266308)
  AND SpellFamilyName <> 0;

-- ШАГ4. КОНТРОЛЬ ПОСЛЕ (ОЖИДАЕМ: 10 у 378285/469883, 0 у остальных)
SELECT SpellId, SpellFamilyName,
       CASE WHEN SpellId IN (378285,469883) THEN 'OK=10' ELSE 'OK=0' END AS expectation
FROM spell_proc
WHERE SpellId IN (157047,267316,321136,326732,378285,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,469883,1253174,1253598,1266308)
ORDER BY SpellId;

-- ШАГ5. Подозрительное семейство: 10 — это ПАЛАДИН, его не трогать.
-- 2 — unused, такие строки прислать (наш старый фикс как раз ошибочно ставил 2).
SELECT SpellId, SpellFamilyName, SpellFamilyMask0, ProcFlags
FROM spell_proc WHERE SpellFamilyName = 2;

-- ШАГ6. БИНДИНГИ: все наши скрипты на месте (включая новые469309/157047/469883)
SELECT * FROM spell_script_names WHERE ScriptName LIKE 'spell_pal%' ORDER BY spell_id;

-- ШАГ7. Цепочки-маяки (должны присутствовать)
SELECT * FROM spell_script_names
WHERE spell_id IN (375576,255937,427453,386730,1266308,85673,31935,321136,24275,53600)
ORDER BY spell_id;
