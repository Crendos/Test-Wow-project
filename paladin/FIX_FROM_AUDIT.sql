-- =============================================================================
-- FIX_FROM_AUDIT.sql — world. По дампу spell_proc + spell_script_names (26.09).
--
-- 1) 378285 / 469883 стоят на SpellFamilyName=2. Это id класса, не семейство.
--    SPELLFAMILY_PALADIN = 10. Пока стоит 2, Гнев Тира и Очищающий огонь
--    не прокают от Щита мстителя (IsAffected отсекает).
--    Остальные 16 строк с масками 0 и семейством 0 — верные, не трогаем.
--    599 строк шага 5 (семейства 3/4/6/7/10/11/...) — другие классы и нормальный
--    паладин. Не обнулять.
--
-- 2) В биндах нет скриптов ревизии PAL_MECH_REV_20260926.
--    PK spell_script_names = (spell_id, ScriptName) — REPLACE не сотрёт чужие
--    скрипты на том же спелле (ядровый spell_pal_judgment и т.д. останутся).
--
-- Порядок: сначала step1 + пересборка worldserver (иначе «script not found»),
-- потом этот файл, потом .reload spell_script_names / перезапуск.
-- Hotfixes этим файлом не чинятся: присланный список таблиц масок не содержит.
-- Маски — paladin/MECH_PROC_FIX.sql в базе hotfixes.
-- =============================================================================

-- 1. Семейство
UPDATE spell_proc SET SpellFamilyName = 10
WHERE SpellId IN (378285, 469883) AND SpellFamilyName <> 10;

SELECT SpellId, SpellFamilyName, SpellFamilyMask0, SpellFamilyMask2, ProcFlags, Chance
FROM spell_proc
WHERE SpellId IN (378285, 469883);
-- ждём SpellFamilyName = 10 у обеих

-- 2. Недостающие бинды (в дампе их не было)
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Высшее призвание
(35395,   'spell_pal_higher_calling_ex'),
(24275,   'spell_pal_higher_calling_ex'),
(1241413, 'spell_pal_higher_calling_ex'),
(184575,  'spell_pal_higher_calling_ex'),
(407480,  'spell_pal_higher_calling_ex'),
(406647,  'spell_pal_higher_calling_ex'),
(408385,  'spell_pal_higher_calling_ex'),
(20271,   'spell_pal_higher_calling_ex'),
(275779,  'spell_pal_higher_calling_ex'),
(275773,  'spell_pal_higher_calling_ex'),
(204019,  'spell_pal_higher_calling_ex'),
(53595,   'spell_pal_higher_calling_ex'),
-- спад кнопки Молота Света
(427441,  'spell_pal_hol_ready_expire_ex'),
-- Солнечный ожог (только Рет, гейт в скрипте)
(24275,   'spell_pal_sun_sear_ex'),
(1241413, 'spell_pal_sun_sear_ex'),
(53385,   'spell_pal_sun_sear_ex'),
-- Рассветный свет: выдача и трата. 20271/20473 уже были — скрипт их игнорирует.
(255937,  'spell_pal_dawnlight_ex'),
(375576,  'spell_pal_dawnlight_ex'),
(114165,  'spell_pal_dawnlight_ex'),
(85256,   'spell_pal_dawnlight_ex'),
(383328,  'spell_pal_dawnlight_ex'),
(53385,   'spell_pal_dawnlight_ex'),
(85673,   'spell_pal_dawnlight_ex'),
(53600,   'spell_pal_dawnlight_ex'),
(427453,  'spell_pal_dawnlight_ex'),
(85222,   'spell_pal_dawnlight_ex'),
-- Молотопад на Окончательный вердикт (Приговор/Буря/Щит/Слово уже были)
(383328,  'spell_pal_sotr_shake_heavens_ex'),
-- Холи-резонанс от Святой призмы (ядровый spell_pal_holy_prism не удаляется)
(114165,  'spell_pal_divine_resonance_prism_ex');

-- 3. Контроль: эти ScriptName должны появиться
SELECT spell_id, ScriptName
FROM spell_script_names
WHERE ScriptName IN (
    'spell_pal_higher_calling_ex',
    'spell_pal_hol_ready_expire_ex',
    'spell_pal_sun_sear_ex',
    'spell_pal_divine_resonance_prism_ex'
)
   OR (ScriptName = 'spell_pal_dawnlight_ex' AND spell_id IN (255937,375576,114165,85256,383328))
   OR (ScriptName = 'spell_pal_sotr_shake_heavens_ex' AND spell_id = 383328)
ORDER BY ScriptName, spell_id;
