-- ============================================================================
-- Paladin class fixes — часть 4: Свет (хил) + доп. Защиты
-- Спутники: spell_paladin_class_fixes_4.cpp (4a) и _5.cpp (4b)
-- ============================================================================

-- 4a: ядро хила --------------------------------------------------------------
-- Мастерство Светоносца (+ Негасимый свет х1.2 на Заре, + Маяк Светоносца)
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(85673,  'spell_pal_mastery_lightbringer_ex'),
(85222,  'spell_pal_mastery_lightbringer_ex'),
(19750,  'spell_pal_mastery_lightbringer_ex'),
(82326,  'spell_pal_mastery_lightbringer_ex'),
(156322, 'spell_pal_mastery_lightbringer_ex'),
-- Избавление (крит-шанс по HP цели)
(85673,  'spell_pal_extrication_ex'),
(85222,  'spell_pal_extrication_ex'),
-- Пробуждение
(414195, 'spell_pal_awakening_ex'),
(31884,  'spell_pal_awakening_on_aw_ex'),
(231895, 'spell_pal_awakening_on_aw_ex'),
(454351, 'spell_pal_awakening_on_aw_ex'),
(20271,  'spell_pal_awakening_consume_ex'),
(275779, 'spell_pal_awakening_consume_ex'),
(275773, 'spell_pal_awakening_consume_ex'),
-- Праведное правосудие (Свет): Правосудие -> Освящение
(275773, 'spell_pal_righteous_judgment_holy_ex'),
(20271,  'spell_pal_righteous_judgment_holy_ex'),
-- Мгновение сострадания (ФоЛ на маяк)
(19750,  'spell_pal_moment_of_compassion_ex'),
-- Лучезарный свет (Св. свет -> 5 союзников по 8%)
(82326,  'spell_pal_resplendent_light_ex'),
-- Убеждение света / Возвращение / Славный рассвет (Св. сияние)
(20473,  'spell_pal_lights_conviction_ex'),
(20473,  'spell_pal_reclamation_ex'),
(20473,  'spell_pal_glorious_dawn_ex'),
-- Божественные откровения
(19750,  'spell_pal_divine_revelations_fol_ex'),
(275773, 'spell_pal_divine_revelations_judg_ex'),
-- Насыщенные вливания (трата Вливания света -> -1с КД СС)
(54149,  'spell_pal_imbued_infusions_ex'),
-- Почтение (крит ФоЛ/СЛ сбрасывает Правосудие)
(19750,  'spell_pal_veneration_ex'),
(82326,  'spell_pal_veneration_ex'),
-- Защита Тира (Аура света -> 211210)
(31821,  'spell_pal_protection_of_tyr_ex'),
-- Истина превыше всего (Правосудие лечит вас)
(275773, 'spell_pal_truth_prevails_ex'),
-- Освобождение (12% хила -> урон)
(85673,  'spell_pal_liberation_ex'),
(85222,  'spell_pal_liberation_ex'),
-- Сияющая праведность (СоП -> урон + Пробуждение судьбы)
(53600,  'spell_pal_shining_righteousness_ex');

-- 4b: маяки и триггеры АН -----------------------------------------------------
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(156910, 'spell_pal_beacon_of_faith_ex'),
(200025, 'spell_pal_beacon_of_virtue_ex'),
(31884,  'spell_pal_tyrs_deliverance_trigger_ex'),
(231895, 'spell_pal_tyrs_deliverance_trigger_ex'),
(200653, 'spell_pal_tyrs_deliverance_select_ex'),
(31884,  'spell_pal_hand_of_divinity_ex'),
(231895, 'spell_pal_hand_of_divinity_ex'),
-- АУДИТ26.09: биндинги к proc-строкам ниже (были только proc-строки — таланты
-- «Спасение светом» и «Очищающий огонь» не работали)
(157047, 'spell_pal_saved_by_the_light_ex'),
(469883, 'spell_pal_refining_fire_ex');

-- spell_proc ------------------------------------------------------------------
-- 157047 Спасение светом: цель с маяком получает урон (успех фильтруется скриптом)
-- ProcFlags: 0x00aa220 (TAKE melee/spell/periodic damage)
REPLACE INTO `spell_proc` VALUES (157047,0x00,10,0,0,0,0,0x00AA220,0x0,0x1,0x2,0x0,0x0,0x0,0,100,10000,0);
-- 469883 Очищающий огонь: Щит мстителя (FamMask 0x4000/0x0/0x2/0x0)
REPLACE INTO `spell_proc` VALUES (469883,0x00,10,0x4000,0x0,0x2,0x0,0x10,0x0,0x1,0x2,0x3,0x0,0x0,0,100,0,0);

-- ПРИМЕЧАНИЯ:
-- * 53576 (Вливание света — проц) НЕ нуждается в строке: PROC_TRIGGER_SPELL
--   входит в белый список авто-генерации proc-ов из DBC.
-- * Освобождение у маяков (461243): у базового маяка 53563 в данных уже есть
--   E3 = -5% урона (87) — талант «вшит» в DBC; скрипт не требуется.
-- * Отложено (нужен абсорб-носитель из WCL): 461244, 1241714, 1241805, 394088,
--   447985/448040, 1244878+ (Маяк Спасителя), 31821 (иммун к прерываниям с Концентрацией).
