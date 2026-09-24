-- ============================================================================
-- Paladin class fixes 12.1.0 — часть 1: Воздаяние (Retribution) + P0-фиксы
-- Спутник к paladin/spell_paladin_class_fixes.cpp
-- Ставится поверх TDB (world). Повторный прогон безопасен (REPLACE INTO).
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 1) spell_script_names — привязка новых скриптов
-- ----------------------------------------------------------------------------
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Искусство войны (406064): сброс КД Клинка правосудия от автоатак
(406064, 'spell_pal_art_of_war_ex'),
-- Праведная причина (402912): сброс КД Клинка от трат Сила Света
(402912, 'spell_pal_righteous_cause_ex'),
-- Сила небес (326732): бесплатная Буря света
(326732, 'spell_pal_empyrean_power_ex'),
-- Великое правосудие (231663): стаки 197277 на цели Правосудия
(20271,  'spell_pal_judgment_greater_ex'),
(275779, 'spell_pal_judgment_greater_ex'),
(275773, 'spell_pal_judgment_greater_ex'),
-- Мастерство: Правосудие Верховного лорда (267316): проц 383921
(267316, 'spell_pal_highlords_judgment_ex'),
-- Судья, присяжные и палач (406157): после Приговора — 1253174 (ядро кастует триггер E1)
(406157, 'spell_pal_judge_jury_executioner_ex'),
-- Возврат Сила Света (1253174)
(1253174, 'spell_pal_judge_jury_executioner_refund_ex'),
-- Свет внутри (урон, 1261111): +10% ФП/Буре в АН
(383328, 'spell_pal_light_within_damage_ex'),
(85256,  'spell_pal_light_within_damage_ex'),
(53385,  'spell_pal_light_within_damage_ex'),
-- Свет внутри (Клинок, 1261159): усиленный Клинок пускает волну 1261160
(184575, 'spell_pal_light_within_blade_ex'),
-- Неземное наследие (387170): крит Правосудия по цели <35% -> 387178
(387170, 'spell_pal_empyrean_legacy_ex'),
-- Крестовый поход (1253598): стак скорости атаки за трату Сила Света
(1253598, 'spell_pal_crusade_ex'),
-- Удар храмовника: Темплар Слэш всегда критует
(406647, 'spell_pal_templar_slash_crit_ex'),
-- Сияющая слава (458359): Всплеск пепла активирует АН (454351) на 8 с
(255937, 'spell_pal_radiant_glory_ex'),
-- Буря света: кап 5 целей
(53385,  'spell_pal_divine_storm_cap_ex'),
-- Пламя света (406545): +3%/+7% к урону Света по целям с Поджиганием
(184575,  'spell_pal_holy_flames_ex'),
(20271,   'spell_pal_holy_flames_ex'),
(275779,  'spell_pal_holy_flames_ex'),
(275773,  'spell_pal_holy_flames_ex'),
(53385,   'spell_pal_holy_flames_ex'),
(383328,  'spell_pal_holy_flames_ex'),
(85256,   'spell_pal_holy_flames_ex'),
(24275,   'spell_pal_holy_flames_ex'),
(1241413, 'spell_pal_holy_flames_ex'),
(408385,  'spell_pal_holy_flames_ex'),
(407480,  'spell_pal_holy_flames_ex'),
(406647,  'spell_pal_holy_flames_ex'),
(255937,  'spell_pal_holy_flames_ex'),
(1260251, 'spell_pal_holy_flames_ex'),
(1261160, 'spell_pal_holy_flames_ex'),
(383921,  'spell_pal_holy_flames_ex');

-- ----------------------------------------------------------------------------
-- 2) spell_proc — без этих строк ауры-процы НЕ работают вовсе
--    (SpellId, SchoolMask, FamilyName, FamMask0-3, ProcFlags, ProcFlags2,
--     TypeMask, PhaseMask, HitMask, AttributesMask, DisableEffectsMask,
--     ProcsPerMinute, Chance, Cooldown(ms), Charges)
-- ----------------------------------------------------------------------------
-- P0: Крещендо ударов (406833) — в клиентских данных Cflags3=0x10, из-за чего
-- аура-стак не процает. AttributesMask=0x2 (TRIGGERED_CAN_PROC) — фикс.
REPLACE INTO `spell_proc` VALUES (406833,0x00,10,0,0,0,0,0x14,0x0,0x1,0x2,0x403,0x2,0x0,0,0,0,0);
-- Искусство войны: автоатаки (вкл. Крещендо ударов 408385 -> 0x4), ICD 1 c
REPLACE INTO `spell_proc` VALUES (406064,0x00,10,0,0,0,0,0x4,0x0,0x1,0x2,0x403,0x2,0x0,0,0,1000,0);
-- Праведная причина: траты Сила Света (ICD 1 c)
REPLACE INTO `spell_proc` VALUES (402912,0x00,10,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,1000,0);
-- Сила небес: Удар крестоносца/Удары храмовника/Крещендо
REPLACE INTO `spell_proc` VALUES (326732,0x00,10,0,0,0,0,0x14,0x0,0x1,0x2,0x403,0x0,0x0,0,0,0,0);
-- Мастерство: Правосудие Верховного лорда — проц от Правосудия
REPLACE INTO `spell_proc` VALUES (267316,0x00,10,0,0,0,0,0x10,0x0,0x1,0x2,0x3,0x0,0x0,0,0,0,0);
-- Судья, присяжные и палач: успешный каст Приговора (343527) — фильтр в скрипте
REPLACE INTO `spell_proc` VALUES (406157,0x00,10,0,0,0,0,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0,0,0,0);
-- ...и траты Сила Света при активном 1253174 (возврат стоимости)
REPLACE INTO `spell_proc` VALUES (1253174,0x00,10,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,0,0);
-- Неземное наследие: крит Правосудия (доп. гейт в скрипте: цель <35% HP)
REPLACE INTO `spell_proc` VALUES (387170,0x00,10,0,0,0,0,0x55410,0x0,0x1,0x2,0x3,0x0,0x0,0,0,0,0);
-- Крестовый поход: траты Сила Света во время 231895
REPLACE INTO `spell_proc` VALUES (1253598,0x00,10,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,0,0);
