-- ============================================================
-- ПРОВЕРКА ПРОК-БАФОВ НА СЕРВЕРЕ (TrinityCore-филк, клиент 12.1)
-- Как выполнять: HeidiSQL / mysql.exe, читать можно БЕЗ остановки сервера.
--   Шаг1: БД `hotfixes`  → запросы №1–3 (spell / spell_effect / spell_proc)
--   Шаг2: БД `world`      → запросы №4–5 (script-таблицы)
-- Если запрос ругается на таблицу/колонку — вилка называет её в ошибке,
-- поправь имя по аналогии (частые варианты: EffectMiscValue_0, entry↔SpellID).
-- Сверка ожиданий: paladin/PROC_BUFFS_ANALYSIS.md (§1 и §5).
-- ============================================================

-- №0. Есть ли вообще эти спеллы в DBC (все6 обязаны найтись):
SELECT ID, SpellName_0
FROM spell
WHERE ID IN (378412, 432629, 427441, 386730, 209388, 386652);

-- №1. Обратный поиск триггеров: какой спелл НАКЛАДЫВАЕТ баф
--      (EffectMiscValue = ID бафа — так находим аplier'а и подтверждаем
--       связку «каст триггера → баф», ожидаемые строки: см. таблицу §1):
SELECT e.SpellID, s.SpellName_0, e.Effect, e.EffectApplyAuraName, e.EffectMiscValue
FROM spell_effect e
JOIN spell s ON s.ID = e.SpellID
WHERE e.EffectMiscValue IN (378412, 432629, 427441, 386730, 209388, 386652);
--   ВАРИАНТ Б (если колонка EffectMiscValue называется иначе):
-- SELECT e.* FROM spell_effect e WHERE e.SpellID IN
--   (SELECT ID FROM spell WHERE SpellName_0 IN
--    ('Light of the Titans','Undisputed Ruling','Hammer of Light','Divine Resonance',
--     'Bulwark of Order','Bulwark of Righteous Fury'));

-- №2. Proc-конфигурация: строки spell_proc для триггеров И для бафов.
--   Ожидание: Chance =100 (или1) у триггеров, либо строка отсутствует —
--   тогда прок описан только в Effect самого спелла (Effect 67 / Proc Trigger).
SELECT *
FROM spell_proc
WHERE SpellID IN (
    378412, 432629, 427441, 386730, 209388, 386652,   -- бафы
    255937, 427453, 375576, 85673, 31935, 386731       -- триггеры
);
--   ВАРИАНТ Б (3.3.5-стиль с отдельной таблицей):
-- SELECT * FROM spell_proc_event WHERE entry IN (255937, 427453, 375576, 85673, 31935, 386731);

-- №3. Сами триггеры: флаги/кулдаун/эффект (Effect67 = Trigger Spell —
--      подтверждает «каст X накладывает баф Y»):
SELECT s.ID, s.SpellName_0, e.Effect, e.EffectApplyAuraName, e.EffectMiscValue
FROM spell s
JOIN spell_effect e ON e.SpellID = s.ID
WHERE s.ID IN (255937, 427453, 375576, 85673, 31935, 386731)
ORDER BY s.ID;

-- №4. world: не висят ли кастомные скрипты (в т.ч. наши party-fixes)
--      на триггерах или бафах — лишний скрипт мог бы перехватить прок:
SELECT *
FROM spell_script_names
WHERE spell IN (378412, 432629, 427441, 386730, 209388, 386652,
                255937, 427453, 375576, 85673, 31935, 386731);

-- №5. world: серверные скрипты-обработчики, если вилка использует spell_scripts:
SELECT *
FROM spell_scripts
WHERE id IN (378412, 432629, 427441, 386730, 209388, 386652,
             255937, 427453, 375576, 85673, 31935, 386731);

-- №6. (Опционально) PPM/шанс из misc-таблиц, если вилка ведёт их отдельно:
-- SELECT * FROM spell_misc WHERE SpellID IN (378412, 432629, 427441, 386730, 209388, 386652);

-- ============================================================
-- Куда смотреть в результатах:
--   * запрос1 нашёл строки → спелл-апplier найден: смотри его SpellID = триггер;
--   * запрос2 Chance ≠100 → прок РЕДКИЙ — это и есть расхождение с анализом;
--   * запрос4/5 дал строки → на спелл висит скрипт, его поведение важнее DBC;
--   * пусто во всём → вилка тянет проки иначе (напиши — поищем по spell labels).
-- Дальше клиентский уровень: /paldumplog procs (PalDump v4.11) — вывод кидаешь мне.
-- ============================================================
