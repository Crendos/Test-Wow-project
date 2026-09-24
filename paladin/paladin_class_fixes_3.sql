-- ============================================================================
-- Paladin class fixes — часть 3: Защита (остаток) + Молот гнева в АН
-- Спутник к paladin/spell_paladin_class_fixes_3.cpp
-- ============================================================================

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Щит мстителя: концентратор талант-обработок
(31935,   'spell_pal_avengers_shield_ex'),
-- Щит праведника: потребление Оплота праведной ярости
(53600,   'spell_pal_shield_of_the_righteous_ex'),
-- Слово света: Прот-скейлинг + задержка Стража
(85673,   'spell_pal_word_of_glory_ex'),
-- Аура Щита праведника: Крепость + Внутренний свет
(132403,  'spell_pal_sotr_aura_ex'),
-- Рвение фанатика: продление АН/Крестового похода/Стража
(20271,   'spell_pal_zealots_paragon_ex'),
(275779,  'spell_pal_zealots_paragon_ex'),
(275773,  'spell_pal_zealots_paragon_ex'),
(24275,   'spell_pal_zealots_paragon_ex'),
(1241413, 'spell_pal_zealots_paragon_ex'),
-- Доблестный крестовый поход (Священный скакун -> Щит праведника)
(190784,  'spell_pal_valiant_crusade_ex'),
-- Благословенный молот (урон по области)
(204019,  'spell_pal_blessed_hammer_ex'),
-- Молот гнева в АН (оверрайды Правосудия)
(31884,   'spell_pal_hammer_of_wrath_aw_ex'),
(231895,  'spell_pal_hammer_of_wrath_aw_ex'),
(454351,  'spell_pal_hammer_of_wrath_aw_ex');

-- spell_proc: 378285 Гнев Тира — триггер 378286 (AoE вокруг целей Щита мстителя)
-- FamMask Щита мстителя: [0x4000, 0x0, 0x2, 0x0]
REPLACE INTO `spell_proc` VALUES (378285,0x00,10,0x4000,0x0,0x2,0x0,0x10,0x0,0x1,0x2,0x3,0x0,0x0,0,0,0,0);
