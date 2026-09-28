-- ============================================================================
-- Paladin class fixes — часть 10: Кузнец света (Lightsmith)
-- Спутник к paladin/spell_paladin_class_fixes_10.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Благословенные доспехи: чередование + Солидарность
(1289728, 'spell_pal_holy_armaments_ex'),
-- ЩП: Возмездие горна + стаки Божественного наставления
(53600,  'spell_pal_sotr_forge_ex'),
-- Слово света: Святое слово
(85673,  'spell_pal_wog_sacred_word_ex'),
-- Божественное наставление: стак 460822 с каждой способности Силы Света
(53600,  'spell_pal_divine_guidance_spend_ex'),
(85673,  'spell_pal_divine_guidance_spend_ex'),
(85222,  'spell_pal_divine_guidance_spend_ex'),
(156322, 'spell_pal_divine_guidance_spend_ex'),
(85256,  'spell_pal_divine_guidance_spend_ex'),
(383328, 'spell_pal_divine_guidance_spend_ex'),
(53385,  'spell_pal_divine_guidance_spend_ex'),
(427453, 'spell_pal_divine_guidance_spend_ex'),
-- следующее Освящение тратит стаки
(26573,  'spell_pal_divine_guidance_cons_ex');
