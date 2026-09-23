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
-- ЩП: пандемия-обновление Сотрясения небес
(53600,  'spell_pal_sotr_shake_heavens_ex'),
-- ТДА: вращающийся Божественный молот
(375576, 'spell_pal_divine_toll_templar_ex'),
-- Правосудие (все варианты) и Молот гнева: стаки Освящения
(20271,  'spell_pal_sanctification_templar_ex'),
(275779, 'spell_pal_sanctification_templar_ex'),
(275773, 'spell_pal_sanctification_templar_ex'),
(24275,  'spell_pal_sanctification_templar_ex'),
(1241413,'spell_pal_sanctification_templar_ex');
