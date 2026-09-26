-- !! УСТАРЕЛ: единый прогон — см. paladin/AUDIT_2_HOTFIXES.sql (база hotfixes) !!
-- =============================================================================
-- PROC_AUDIT — единая перепроверка прок-аур паладина (26.09.2026).
-- После: PROC_FIX.sql (маски 3 виновных -> 0) + fix_6/fix_7 (узкие пути).
-- Как выполнять: HeidiSQL/mysql против БД `hotfixes` (запросы 1-2),
--   затем переключиться на БД `world` (запрос 3). Остановка сервера не нужна.
-- Ожидания — в комментариях. Любое ненулевое «лишнее» поле у п.1 = кандидат
--   на болезнь из PROC_BUFFS_ANALYSIS.md §8 (прок «с любой способности»).
-- =============================================================================

-- 1. Маски/шансы/кумулятивность ВСЕХ отслеживаемых аур --------------------------
SELECT ao.SpellID,
       ao.ProcTypeMask1, ao.ProcTypeMask2,
       ao.ProcChance, ao.ProcCharges, ao.ProcInterval,
       ao.CumulativeAura
FROM spell_aura_options AS ao
WHERE ao.SpellID IN (
    427445, -- Свет наставления (Храмовник)   -- ОЖИДАЕМ 0/0      (PROC_FIX)
    432626, -- Неоспоримое постановление       -- ОЖИДАЕМ 0/0      (PROC_FIX)
    386732, -- Бож. резонанс (аплеер)          -- ОЖИДАЕМ 0/0      (PROC_FIX)
    386738, -- Бож. резонанс, талант Прот      -- ОЖИДАЕМ 0/0, CumulativeAura=2
    378405, -- Свет титанов (талант)           -- ОЖИДАЕМ 16384/0  (узкий, не трогали)
    378412, -- Свет титанов (баф)              -- informational
    209388, -- Оплот порядка                   -- ОЖИДАЕМ ProcChance=101
    386652, -- Оплот праведной ярости          -- ОЖИДАЕМ ProcChance=100, CumulativeAura=5
    427441, -- Молот Света (кнопка-баф)        -- ОЖИДАЕМ ProcChance=101
    432629, -- Неоспоримое (баф хаст)          -- informational
    386730, -- Бож. резонанс (баф общий)       -- informational (тик — наш AuraScript)
    1266308 -- Бож. резонанс (баф Рет)         -- informational (spell_proc в world)
);

-- 2. Аплееры: кто накладывает отслеживаемые бафы (БД `hotfixes`) ---------------
--    Легитимные цепочки: 378405->378412, 386732->386730, 427445->427441, 432626->432629.
--    После нуления масок сами аплееры не срабатывают — их кидают наши скрипты;
--    строки нужны лишь как карта зависимостей DBC.
SELECT e.ID, e.Effect, e.EffectTriggerSpell, e.EffectMiscValue, e.EffectBasePoints
FROM spell_effect AS e
WHERE e.EffectTriggerSpell IN (378412, 386730, 427441, 432629, 386732, 386731)
   OR e.EffectMiscValue   IN (378412, 386730, 427441, 432629);

-- 3. Биндинги цепочек (БД `world`!) -------------------------------------------SELECT *
FROM spell_script_names
WHERE spell_id IN (
    375576, -- Божественный звон  -> divine_toll_ex (Резонанс, Прот-Молот)
    255937, -- Пробуждение зол    -> wake_of_ashes + lights_guidance_wake_ex (Рет-Молот)
    427453, -- Молот Света        -> hammer_of_light_ex (эхо) + hol_templar_ex (хаст)
    386730, -- Резонанс баф       -> divine_resonance_prot_ex (тики 31935/20473)
    1266308,-- Рет-резонанс       -> divine_resonance_ret_ex
    85673,  -- Слово благословения-> light_of_the_titans_ex (Свет титанов)
    31935,  -- Щит мстителя       -> bulwark_of_order_ex (Оплоты)
    377128  -- Золотая тропа      -> golden_path_ex
);

-- 5. СВИП ШИРОКИХ МАСОК по ВСЕЙ базе (болезнь §8: прок «от каста чего угодно») --
--    Частый бит: ProcTypeMask2=4 («успешный каст») и подозрительный
--    ProcTypeMask1=2446336 (0x255380). Наши три уже обнулены (PROC_FIX) —
--    в выводе их быть не должно. Вывод пуст = других жертв нет; строки,
--    которые вылезут, прислать мне вместе с именем спелла.
SELECT ao.SpellID, ao.ProcTypeMask1, ao.ProcTypeMask2, ao.ProcChance, ao.CumulativeAura
FROM spell_aura_options AS ao
WHERE (ao.ProcTypeMask2 & 4) <> 0
   OR (ao.ProcTypeMask1 & 2446336) = 2446336
ORDER BY ao.SpellID;
