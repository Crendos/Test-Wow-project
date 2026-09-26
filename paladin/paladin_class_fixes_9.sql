-- ============================================================================
-- Paladin class fixes — часть 9: Темплар 12.x (дозакрытие партии 6)
--   Сотрясение небес, Неоспоримое постановление, Божественный молот,
--   Освящение Темплара, Избавление Света.
-- Спутник к paladin/spell_paladin_class_fixes_9.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Молот Света: баффы Сотрясения/Постановления + триггер Избавления Света
(427453, 'spell_pal_hol_templar_ex'),
-- летящий молоток: стаки Избавления Света
(431398, 'spell_pal_empyrean_deliverance_ex'),
-- Молотопад (432463): Рет — Приговор/Буря, Прот — Щит праведника/Слово света.
-- Сотрясение этим скриптом больше НЕ продлевается (только Молот Света).
(53600,  'spell_pal_sotr_shake_heavens_ex'),
(85256,  'spell_pal_sotr_shake_heavens_ex'), -- Окончательный приговор
(383328, 'spell_pal_sotr_shake_heavens_ex'), -- Окончательный вердикт (замена Приговора)
(53385,  'spell_pal_sotr_shake_heavens_ex'), -- Божественная буря
(85673,  'spell_pal_sotr_shake_heavens_ex'), -- Слово света
-- ТДА: вращающийся Божественный молот (8с, тик 2с — родные данные спелла)
(375576, 'spell_pal_divine_toll_templar_ex'),
-- Правосудие (все варианты) и Молот гнева: стаки Освящения
(20271,  'spell_pal_sanctification_templar_ex'),
(275779, 'spell_pal_sanctification_templar_ex'),
(275773, 'spell_pal_sanctification_templar_ex'),
(24275,  'spell_pal_sanctification_templar_ex'),
(1241413,'spell_pal_sanctification_templar_ex'),
-- Высшее призвание: билдеры продлевают Сотрясение (гейт по спеку — в скрипте)
(35395,  'spell_pal_higher_calling_ex'),
(24275,  'spell_pal_higher_calling_ex'),
(1241413,'spell_pal_higher_calling_ex'),
(184575, 'spell_pal_higher_calling_ex'),
(407480, 'spell_pal_higher_calling_ex'),
(406647, 'spell_pal_higher_calling_ex'),
(408385, 'spell_pal_higher_calling_ex'),
(20271,  'spell_pal_higher_calling_ex'),
(275779, 'spell_pal_higher_calling_ex'),
(275773, 'spell_pal_higher_calling_ex'),
(204019, 'spell_pal_higher_calling_ex'),
(53595,  'spell_pal_higher_calling_ex'),
-- спад кнопки Молота → проверка Избавления Света
(427441, 'spell_pal_hol_ready_expire_ex');
