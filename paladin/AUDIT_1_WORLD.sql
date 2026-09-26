-- =============================================================================
-- AUDIT_1_WORLD.sql  —  ВЕСЬ АУДИТ ДЛЯ БД `world` (один прогон целиком)
-- Подключение: HeidiSQL/mysql -> база `world`. Больше НИЧЕГО открывать не надо.
-- Содержит: инвентарь spell_proc, фикс E4 (family10->0/2), биндинги, чужие family.
-- После выполнения: вывод шагов1+3+4+5+6 -> ОДНИМ сообщением агенту.
-- =============================================================================

-- ШАГ1. КОНТРОЛЬ ДО: все spell_proc паладина (ожидаем18 строк, все SpellFamilyName=10)
SELECT SpellId, SpellFamilyName, SpellFamilyMask0, SpellFamilyMask1, SpellFamilyMask2, SpellFamilyMask3,
       ProcFlags, ProcFlags2, Chance, Cooldown
FROM spell_proc
WHERE SpellId IN (157047,267316,321136,326732,378285,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,469883,1253174,1253598,1266308)
ORDER BY SpellId;

-- ШАГ2. ФИКС E4: фам-маски Щита мстителя требуют семейство PALADIN(2), иначе маски не читаются
UPDATE spell_proc SET SpellFamilyName = 2
WHERE SpellId IN (378285, 469883) AND SpellFamilyName <> 2;

-- ШАГ3. ФИКС E4: остальные (маски=0) ->0 = «фильтр семейства выключен» (стандарт TDB)
UPDATE spell_proc SET SpellFamilyName = 0
WHERE SpellId IN (157047,267316,321136,326732,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,1253174,1253598,1266308)
  AND SpellFamilyName <> 0;

-- ШАГ4. КОНТРОЛЬ ПОСЛЕ (ОЖИДАЕМ: 2 у 378285/469883, 0 у остальных)
SELECT SpellId, SpellFamilyName,
       CASE WHEN SpellId IN (378285,469883) THEN 'OK=2' ELSE 'OK=0' END AS expectation
FROM spell_proc
WHERE SpellId IN (157047,267316,321136,326732,378285,387170,402912,403530,404357,
                  406064,406157,406833,431474,469309,469883,1253174,1253598,1266308)
ORDER BY SpellId;

-- ШАГ5. ЧУЖИЕ строки с подозрительным семейством (не0 и не2 -> прислать агенту)
SELECT * FROM spell_proc WHERE SpellFamilyName NOT IN (0,2);

-- ШАГ6. БИНДИНГИ: все наши скрипты на месте (включая новые469309/157047/469883)
SELECT * FROM spell_script_names WHERE ScriptName LIKE 'spell_pal%' ORDER BY spell_id;

-- ШАГ7. Цепочки-маяки (должны присутствовать)
SELECT * FROM spell_script_names
WHERE spell_id IN (375576,255937,427453,386730,1266308,85673,31935,321136,24275,53600)
ORDER BY spell_id;
