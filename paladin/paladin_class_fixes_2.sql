-- ============================================================================
-- Paladin class fixes — часть 2: остаток Воздаяния + ядро Защиты
-- Спутник к paladin/spell_paladin_class_fixes_2.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Буря Светоносца
(53385,  'spell_pal_tempest_of_the_lightbringer_ex'),
(224239, 'spell_pal_tempest_wave_ex'),
-- Правосудие справедливости
(20271,  'spell_pal_judgment_of_justice_ex'),
(275779, 'spell_pal_judgment_of_justice_ex'),
(275773, 'spell_pal_judgment_of_justice_ex'),
-- Сжигание праха (рост тиков Поджигания)
(383346, 'spell_pal_burn_to_ash_ex'),
-- Сияние
(321136, 'spell_pal_shining_light_ex'),
(85673,  'spell_pal_shining_light_consume_ex'),
-- Оплот порядка
(31935,  'spell_pal_bulwark_of_order_ex'),
-- Свет титанов
(85673,  'spell_pal_light_of_the_titans_ex'),
-- Отрада + Зрение святости (тики Освящения)
(81297,  'spell_pal_consecration_prot_ex'),
-- Печать возмездия (Благословенный молот)
(204301, 'spell_pal_seal_of_reprisal_bh_ex'),
-- Благословение Света
(53385,  'spell_pal_lightforged_blessing_ex');

-- spell_proc: Око за око (удары по вам во время щитов)
-- SpellId, SchoolMask, Family, FamMask0-3, ProcFlags, ProcFlags2, TypeMask, PhaseMask, HitMask, Attributes, DisableEffects, PPM, Chance, CD, Charges
REPLACE INTO `spell_proc` VALUES (469309,0x00,10,0,0,0,0,0x88,0x0,0x1,0x2,0x0,0x0,0x0,0,0,0,0);
-- Сияние: успешный каст Щита праведника (ProcFlags2 = CAST_SUCCESSFUL)
REPLACE INTO `spell_proc` VALUES (321136,0x00,10,0,0,0,0,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0,0,0,0);
