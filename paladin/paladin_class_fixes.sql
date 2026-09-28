-- ============================================================================
-- Paladin class fixes 12.1.0 — часть 1: Воздаяние (Retribution) + P0-фиксы
-- Спутник к paladin/spell_paladin_class_fixes.cpp
-- Ставится поверх TDB (world). Повторный прогон безопасен (REPLACE INTO).
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 1) spell_script_names — привязка новых скриптов
-- ----------------------------------------------------------------------------
-- Дубли: наш старый бинд Крещендо (Сила Света шла бы дважды со стоковым 406833)
-- и стоковая пара Приговора 11.x (молот бил бы дважды вместе с нашим 12.x).
-- Божественный помощник (spell_pal_divine_auxiliary) на 343527 не трогаем.
DELETE FROM `spell_script_names` WHERE (`spell_id`, `ScriptName`) IN
((408385, 'spell_pal_crusading_strikes_hp_ex'),
 (343527, 'spell_pal_execution_sentence'));

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
-- Великое правосудие: одно наложение 197277 за удар траты Силы Света
(85256,  'spell_pal_greater_judgment_consume_ex'),
(383328, 'spell_pal_greater_judgment_consume_ex'),
(53385,  'spell_pal_greater_judgment_consume_ex'),
(53600,  'spell_pal_greater_judgment_consume_ex'),
(427453, 'spell_pal_greater_judgment_consume_ex'),
(215661, 'spell_pal_greater_judgment_consume_ex'),
(415091, 'spell_pal_greater_judgment_consume_ex'),
-- Мастерство: удар 383921 кастует скрипт Правосудия (20271/275779/275773).
-- Строка на 267316 оставлена, чтобы старый бинд не сыпал «script not found».
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
-- Неземное наследие: старый бинд оставлен (скрипт глушит прок <35%). Выдача — с Гнева карателя.
(387170, 'spell_pal_empyrean_legacy_ex'),
(31884,  'spell_pal_empyrean_legacy_aw_ex'),
(454351, 'spell_pal_empyrean_legacy_aw_ex'),
(20271,  'spell_pal_empyrean_legacy_judgment_ex'),
(275779, 'spell_pal_empyrean_legacy_judgment_ex'),
(275773, 'spell_pal_empyrean_legacy_judgment_ex'),
(85673,  'spell_pal_empyrean_legacy_spend_ex'),
(156322, 'spell_pal_empyrean_legacy_spend_ex'),
(85256,  'spell_pal_empyrean_legacy_spend_ex'),
(383328, 'spell_pal_empyrean_legacy_spend_ex'),
(427453, 'spell_pal_empyrean_legacy_spend_ex'),
(53385,  'spell_pal_empyrean_legacy_mod_ex'),
(85222,  'spell_pal_empyrean_legacy_mod_ex'),
-- Выйти на свет (1263782): Воздаяние, Гнев карателя -> Ан'ше + энергия Света; Молот гнева -> Клинок
(31884,  'spell_pal_walk_into_light_aw_ex'),
(454351, 'spell_pal_walk_into_light_aw_ex'),
(24275,  'spell_pal_walk_into_light_how_ex'),
(1241413,'spell_pal_walk_into_light_how_ex'),
-- Крестовый поход (1253598): скорость на Гневе карателя, не на 231895
(1253598, 'spell_pal_crusade_ex'),
(31884,  'spell_pal_crusade_aw_ex'),
(454351, 'spell_pal_crusade_aw_ex'),
(31884,  'spell_pal_crusade_aw_cast_ex'),
(454351, 'spell_pal_crusade_aw_cast_ex'),
-- Удар храмовника: Темплар Слэш всегда критует
(406647, 'spell_pal_templar_slash_crit_ex'),
-- Сияющая слава (458359): Всплеск пепла активирует АН (454351) на 8 с
(255937, 'spell_pal_radiant_glory_ex'),
-- Буря света: кап 5 целей
(53385,  'spell_pal_divine_storm_cap_ex'),
-- Освящённый клинок (404834): Клинок правосудия ставит Освящение, не чаще 10 с
(184575, 'spell_pal_consecrated_blade_ex'),
-- Крещендо ударов: Сила Света через удар — стоковый скрипт TrinityCore на таланте 406833
-- (2 стака -> 406834). Ставим бинд явно на случай пустой базы.
(406833, 'spell_pal_crusading_strikes'),
-- Приговор (343527): 20% светлого урона по задетым взрывом 1260251, через 10 с
(343527, 'spell_pal_execution_sentence_ex'),
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
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (406833,0x00,0,0,0,0,0,0x14,0x0,0x1,0x2,0x403,0x2,0x0,0,0,0,0);
-- Искусство войны: автоатаки (вкл. Крещендо ударов 408385 -> 0x4), ICD 1 c
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (406064,0x00,0,0,0,0,0,0x4,0x0,0x1,0x2,0x403,0x2,0x0,0,0,1000,0);
-- Праведная причина: траты Сила Света (ICD 1 c)
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (402912,0x00,0,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,1000,0);
-- Сила небес: Удар крестоносца/Удары храмовника/Крещендо
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (326732,0x00,0,0,0,0,0,0x14,0x0,0x1,0x2,0x403,0x0,0x0,0,0,0,0);
-- Мастерство 267316: пассивный бонус урона из DBC. Удар 383921 — не этот прок
-- (у спелла нет ауры-прока), а AfterHit Правосудия в spell_pal_judgment_greater_ex.
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (267316,0x00,0,0,0,0,0,0x10,0x0,0x1,0x2,0x3,0x0,0x0,0,0,0,0);
-- Судья, присяжные и палач: успешный каст Приговора (343527) — фильтр в скрипте
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (406157,0x00,0,0,0,0,0,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0,0,0,0);
-- ...и траты Сила Света при активном 1253174 (возврат стоимости)
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (1253174,0x00,0,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,0,0);
-- Крестовый поход: траты Сила Света во время Гнева карателя (31884/454351)
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (1253598,0x00,0,0,0,0,0,0x55410,0x0,0x3,0x2,0x403,0x0,0x0,0,0,0,0);

-- 387170 больше не прокает сам: старая строка вешала 387178 на трату Силы Света.
-- Без удаления повторный прогон части 1 оставлял бы неверный прок в уже залитой базе.
DELETE FROM `spell_proc` WHERE `SpellId` = 387170;

-- Вливание света (54149), Свет: Вспышка света тратит бафф.
-- Правосудие (275773) тратит его в spell_pal_judgment_greater_ex (+1 Сила Света,
-- «Недостойный» +150%). Поглощение 414022 — UnitScript, в SQL не нужен.
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(19750, 'spell_pal_infusion_of_light_fol_ex');

-- ============================================================================
-- Комплекты T35 (12.0) и T36 (12.1): серверные части бонусов.
-- Модификаторы (T35 2pc всех спеков, метки T36 Holy 2pc, +10% Цели T36 Ret 2pc,
-- крит по 204242 и радиус 81297 T36 Prot 2pc) работают из DBC без SQL.
-- ============================================================================
REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- T36 Ret 2pc/4pc: Божественная сила и Божественный арбитр при трате Божественной цели
(383328, 'spell_pal_t36_ret_divine_purpose_ex'),
(85256,  'spell_pal_t36_ret_divine_purpose_ex'),
(53385,  'spell_pal_t36_ret_divine_purpose_ex'),
(427453, 'spell_pal_t36_ret_divine_purpose_ex'),
(85673,  'spell_pal_t36_ret_divine_purpose_ex'),
(215661, 'spell_pal_t36_ret_divine_purpose_ex'),
-- T35 Ret 4pc: Приговор/Вердикт/Буря вешают Поджигание
(383328, 'spell_pal_t35_ret_expurgation_ex'),
(85256,  'spell_pal_t35_ret_expurgation_ex'),
(53385,  'spell_pal_t35_ret_expurgation_ex'),
-- T35 Holy 4pc: Святой шок +20% в маяк
(25914,  'spell_pal_t35_holy_beacon_ex'),
-- T36 Holy 4pc: Свет небес -> Вливание света (Правосудие — в spell_pal_judgment_greater_ex)
(82326,  'spell_pal_t36_holy_light_ex'),
-- T35 Prot 4pc: Щит праведника -> 1272298, трата Щитом мстителя
(1264847, 'spell_pal_t35_prot_4pc_ex'),
(53600,  'spell_pal_t35_prot_sotr_ex'),
(31935,  'spell_pal_t35_prot_avengers_shield_ex'),
-- T36 Prot 2pc: Освящение +30%
(26573,  'spell_pal_t36_prot_consecration_ex'),
-- T36 Prot 4pc: +20% Света (x2 при крите)
(275779, 'spell_pal_t36_prot_4pc_ex'),
(204301, 'spell_pal_t36_prot_4pc_ex'),
(53595,  'spell_pal_t36_prot_4pc_ex'),
(88263,  'spell_pal_t36_prot_4pc_ex'),
(35395,  'spell_pal_t36_prot_4pc_ex'),
-- Стоковые скрипты TC, от которых зависят бонусы. Их строки есть только в базовом дампе TDB,
-- у пустой базы их нет.
(223817, 'spell_pal_divine_purpose'),
(53651,  'spell_pal_light_s_beacon'),
(383344, 'spell_pal_expurgation'),
(26573,  'spell_pal_consecration');

-- Стоковые spell_proc (как в TC: sql/old/9.x 2022_02_10_00 и updates/master 2026_09_21_03).
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(223817,0x00,10,0x00000000,0x00000000,0x00000000,0x00000000,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0,0,0,0), -- Divine Purpose
(223819,0x00,10,0x00000000,0x00000000,0x00000000,0x00000000,0x0,0x0,0x0,0x1,0x0,0x8,0x0,0,0,0,0), -- Divine Purpose (buff, REQ_SPELLMOD)
(383344,0x00,10,0x00000000,0x00000000,0x00000000,0x40000000,0x0,0x0,0x1,0x2,0x0,0x0,0x0,0,0,0,0); -- Expurgation
