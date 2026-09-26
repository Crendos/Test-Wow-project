-- ============================================================================
-- Paladin class fixes — часть 6: Геройские деревья
-- Спутник к paladin/spell_paladin_class_fixes_7.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Молот Света (Свет наставления, Храмовник)
(427453, 'spell_pal_hammer_of_light_ex'),
-- Рассветный свет (Вестник): выдача зарядов + трата спендером.
-- 20271/20473 оставлены: скрипт их игнорирует (старый бинд не должен падать «script not found»).
(20271,  'spell_pal_dawnlight_ex'),
(20473,  'spell_pal_dawnlight_ex'),
(255937, 'spell_pal_dawnlight_ex'),
(375576, 'spell_pal_dawnlight_ex'),
(114165, 'spell_pal_dawnlight_ex'),
(85256,  'spell_pal_dawnlight_ex'),
(383328, 'spell_pal_dawnlight_ex'),
(53385,  'spell_pal_dawnlight_ex'),
(85673,  'spell_pal_dawnlight_ex'),
(53600,  'spell_pal_dawnlight_ex'),
(427453, 'spell_pal_dawnlight_ex'),
(85222,  'spell_pal_dawnlight_ex'),
-- Солнечный ожог: только Рет (крит Мг/Бури). Холи-хил не вешаем спеллом урона 431414.
(24275,  'spell_pal_sun_sear_ex'),
(1241413,'spell_pal_sun_sear_ex'),
(53385,  'spell_pal_sun_sear_ex'),
-- Второй восход (Вестник солнца)
(431474, 'spell_pal_second_sunrise_ex'),
-- Доблесть (Ламповщик, Прот)
(85673,  'spell_pal_valiance_ex'),
-- Свет наставления (Храмовник): Рет — Пробуждение зол (255937) выдает кнопку
-- Молота Света (427441) на20с; Прот — то же от Звона (часть5, fix_6)
(255937, 'spell_pal_lights_guidance_wake_ex');

-- spell_proc ------------------------------------------------------------------
-- Второй восход: успешный каст Молота гнева, шанс 15%, ICD 1с
VALUES (431474,0x00,0,0,0,0,0,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0,15,1000,0);
-- Молотопад (432463) больше НЕ с Молота Света: его вешают спендеры
-- (часть 9, spell_pal_sotr_shake_heavens_ex). Маску прока обнуляет MECH_PROC_FIX.sql.
