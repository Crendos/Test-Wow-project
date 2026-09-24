-- ============================================================================
-- Paladin class fixes — часть 7: по ID пользователя
--   Слава авангарда (Прот) 1267203/1267211/1267215
--   Маяк Спасителя (Свет) 1244878/1245367/1245368
--   Серафимский барьер 1241714, Переполняющий свет 461244
-- (458359 Radiant Glory — уже в партии 1; 1261113 — DBC)
-- Спутник к paladin/spell_paladin_class_fixes_8.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Слава авангарда
(20271,  'spell_pal_glory_of_the_vanguard_ex'),
(275779, 'spell_pal_glory_of_the_vanguard_ex'),
(275773, 'spell_pal_glory_of_the_vanguard_ex'),
-- Щит мстителя: болт Авангарда
(31935,  'spell_pal_avengers_shield_vanguard_ex'),
-- Щит праведника: преломление Доблести
(53600,  'spell_pal_shield_of_the_righteous_vanguard_ex'),
-- Маяк Спасителя: назначение цели
(1244878,'spell_pal_beacon_of_the_savior_apply_ex'),
-- Маяк Спасителя: перенос лечения
(85673,  'spell_pal_beacon_of_the_savior_transfer_ex'),
(85222,  'spell_pal_beacon_of_the_savior_transfer_ex'),
(19750,  'spell_pal_beacon_of_the_savior_transfer_ex'),
(82326,  'spell_pal_beacon_of_the_savior_transfer_ex'),
(156322, 'spell_pal_beacon_of_the_savior_transfer_ex'),
(20473,  'spell_pal_beacon_of_the_savior_transfer_ex'),
-- Серафимский барьер (в 12.x спендеры Холи: Вечное пламя + Свет Зари)
(85673,  'spell_pal_seraphic_barrier_ex'),
(85222,  'spell_pal_seraphic_barrier_ex'),
(156322, 'spell_pal_seraphic_barrier_ex'), -- Вечное пламя (12.x: подтверждено логом +20 Вестник, 145 кастов/бой)
-- Переполняющий свет (хил Св. сияния)
(25914,  'spell_pal_overflowing_light_ex');

-- spell_proc: новых не требуется — все процы решаются скриптами (шанс 20%
-- Авангарда захардкожен по simc/Fatpala: в DBC данных нет).
