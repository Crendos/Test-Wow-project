-- ============================================================
-- ПРОВЕРКА ПРОК-БАФОВ — ФИНАЛЬНАЯ ВЕРСИЯ (схема проверена)
-- hotfixes: spell_name / spell_effect / spell_aura_options / spell_misc / spell_procs_per_minute
-- world:    spell_proc / spell_script_names (колонка spell_id!)
-- Выполнять: блок №1 → в БД hotfixes, блок №2 → в БД world.
-- Чтение, сервер останавливать не нужно.
-- Сверка ожиданий: paladin/PROC_BUFFS_ANALYSIS.md (§1).
-- ============================================================

-- ====================== hotfixes ======================

-- №1. Имена всех релевантных спеллов (бафы + триггеры + тики):
SELECT ID, Name
FROM spell_name
WHERE ID IN (378412, 432629, 427441, 386730, 209388, 386652,   -- 6 бафов
             255937, 427453, 375576, 85673, 31935, 386731,     -- триггеры
             20271, 20473);                                    -- тики Бож. резонанса

-- №2. ★ ГЛАВНОЕ: шансы прока для бафов и триггеров.
--   Ожидание из анализа: ProcChance = 100 или 101 (так в DBC обозначают 100%),
--   ProcCharges = 0/1, SpellProcsPerMinuteID = NULL (PPM у этих проков НЕТ).
--   Если ProcChance = 0 и ProcTypeMask1/2 = 0 — прок описан только эффектом (№3).
SELECT ao.SpellID, n.Name,
       ao.ProcChance, ao.ProcCharges,
       ao.ProcTypeMask1, ao.ProcTypeMask2,
       ao.SpellProcsPerMinuteID, ppm.BaseProcRate,
       ao.CumulativeAura
FROM spell_aura_options ao
LEFT JOIN spell_name n ON n.ID = ao.SpellID
LEFT JOIN spell_procs_per_minute ppm ON ppm.ID = ao.SpellProcsPerMinuteID
WHERE ao.SpellID IN (378412, 432629, 427441, 386730, 209388, 386652,
                     255937, 427453, 375576, 85673, 31935, 386731,
                     20271, 20473);

-- №3. Обратный поиск: КАКОЙ спелл накладывает баф
--   (EffectTriggerSpell или EffectMiscValue1 = ID бафа — оба варианта):
SELECT e.SpellID, n.Name, e.EffectIndex, e.Effect, e.EffectAura,
       e.EffectTriggerSpell, e.EffectMiscValue1, e.EffectBasePoints
FROM spell_effect e
LEFT JOIN spell_name n ON n.ID = e.SpellID
WHERE e.EffectTriggerSpell IN (378412, 432629, 427441, 386730, 209388, 386652)
   OR e.EffectMiscValue1  IN (378412, 432629, 427441, 386730, 209388, 386652);
--   Строки с e.SpellID = триггер → связка «каст триггера → баф» подтверждена сервером.

-- №4. Эффекты самих триггеров (Effect=67 «Proc Trigger Spell» — ожидаемая механика):
SELECT e.SpellID, n.Name, e.EffectIndex, e.Effect, e.EffectAura,
       e.EffectTriggerSpell, e.EffectMiscValue1, e.EffectBasePoints
FROM spell_effect e
LEFT JOIN spell_name n ON n.ID = e.SpellID
WHERE e.SpellID IN (255937, 427453, 375576, 85673, 31935, 386731)
ORDER BY e.SpellID, e.EffectIndex;

-- №5. Длительности бафов (сверяем с анализом:10 /6 /20 /15 /8 /15 сек):
SELECT m.SpellID, n.Name, m.DurationIndex
FROM spell_misc m
LEFT JOIN spell_name n ON n.ID = m.SpellID
WHERE m.SpellID IN (378412, 432629, 427441, 386730, 209388, 386652);

-- ====================== world ======================

-- №6. Колонки и строки кастомного spell_proc (TC-таблица, свои шансы):
SHOW COLUMNS FROM spell_proc;
SELECT *
FROM spell_proc
WHERE SpellID IN (378412, 432629, 427441, 386730, 209388, 386652,
                  255937, 427453, 375576, 85673, 31935, 386731);
--   (если ворнинг про колонку — поправь имя по SHOW COLUMNS: скорее всего SpellID)

-- №7. Кастомные скрипты на бафах/триггерах (в т.ч. наши party-fixes):
SELECT *
FROM spell_script_names
WHERE spell_id IN (378412, 432629, 427441, 386730, 209388, 386652,
                   255937, 427453, 375576, 85673, 31935, 386731,
                   20271, 20473);

-- №8. (Опционально) связки «прок-в-прок» (имя колонки уточняем SHOW COLUMNS):
SHOW COLUMNS FROM spell_linked_spell;
SELECT * FROM spell_linked_spell
WHERE SpellId IN (378412, 432629, 427441, 386730, 209388, 386652,
                  255937, 427453, 375576, 85673, 31935, 386731);

-- №9. ★ ЦЕПОЧКА ДО КОНЦА: кто кидает ПРОМЕЖУТОЧНЫЕ аплееры,
--   которых нашёл запрос №3 (427445/378405/386732/432626):
SELECT e.SpellID, n.Name, e.EffectIndex, e.Effect, e.EffectAura,
       e.EffectTriggerSpell, e.EffectMiscValue1
FROM spell_effect e
LEFT JOIN spell_name n ON n.ID = e.SpellID
WHERE e.EffectTriggerSpell IN (427445, 378405, 386732, 432626)
   OR e.EffectMiscValue1  IN (427445, 378405, 386732, 432626);
--   Строки с e.SpellID = каст триггера (255937/85673/375576/427453) замыкают цепочку:
--   триггер → аплеер → баф (иначе аплеер кидает серверный скрипт — смотреть код скрипта).

-- ============================================================
-- Куда смотреть:
--   №2 ProcChance=100/101 + пустой PPM + №3 нашёл триггер  → ВСЁ СХОДИТСЯ с анализом;
--   №2 Chance <100 или PPM >0                              → баф прокает реже, чем записано;
--   №3 пусто, но №4 с EffectTriggerSpell/EffectAura        → механика через ауру-дамми;
--   №7 есть строки                                         → на спелл висит скрипт, его код главнее DBC.
-- Параллельно клиент: /paldumplog clear → бой → /paldumplog procs (PalDump v4.11).
-- ============================================================
