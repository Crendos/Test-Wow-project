-- ============================================================================
-- Paladin class fixes — часть 11 (28.09.2026): героические таланты, Гильотина,
-- Божественный арбитр (T36 Ret 4pc) — бонусы выпускающего спендера.
-- Спутник к paladin/spell_paladin_class_fixes_11.cpp (+ классы T36 в основной части).
-- Повторный прогон безопасен.
-- ============================================================================

-- Старая (неверная) Доблесть: «Слово славы -> КД БЗ/БП/БС/ЩБ». Класс удалён из части 7.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_pal_valiance_ex';

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Гильотина (аксессуар 270173): прок и урон от недостающего здоровья
(1291728, 'spell_item_guillotine_proc_ex'),
(1306604, 'spell_item_guillotine_damage_ex'),
-- T36 Ret 4pc: Буря +200% / Приговор, Вердикт +100% у спендера, выпускающего арбитра
(53385,  'spell_pal_t36_divine_arbiter_bonus_ex'),
(224239, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(423593, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(383328, 'spell_pal_t36_divine_arbiter_bonus_ex'),
(85256,  'spell_pal_t36_divine_arbiter_bonus_ex'),
(224266, 'spell_pal_t36_divine_arbiter_bonus_ex'),
-- «Божественная сила: Буря» 1306159: эффект обнулён, бонус считает скрипт выше
(1306159, 'spell_pal_t36_divine_power_storm_ex'),
-- Храмовник: криты Эмпирейского молота -> Гнев нисхождения / Судия Света
(431398, 'spell_pal_empyrean_hammer_crit_ex'),
(431551, 'spell_pal_hero_proc_disabled_ex'),
(1261525,'spell_pal_hero_proc_disabled_ex'),
-- разлёт урона без основной цели
(431625, 'spell_pal_splash_skip_primary_ex'),
(431399, 'spell_pal_splash_skip_primary_ex'),
-- Вестник солнца: Аврора
(255937, 'spell_pal_aurora_ex'),
(114165, 'spell_pal_aurora_ex'),
(375576, 'spell_pal_aurora_ex'),
-- Вестник солнца: DoT Рассветного света (разлёт 8%, Аватар солнца, Затяжное сияние)
(431380, 'spell_pal_dawnlight_dot_ex'),
-- Кузнец света: Молот и наковальня (крит Правосудия Прота)
(275779, 'spell_pal_hammer_and_anvil_ex'),
-- Кузнец света: Сложить оружие (доспех спал с паладина)
(432502, 'spell_pal_laying_down_arms_ex'),
(432496, 'spell_pal_laying_down_arms_ex'),
-- Кузнец света: Доблесть (трата Сияющего света / Наставления Света)
(85673,  'spell_pal_valiance_consume_ex'),
(19750,  'spell_pal_valiance_consume_ex'),
(82326,  'spell_pal_valiance_consume_ex'),
(275773, 'spell_pal_valiance_consume_ex');

-- Гильотина: строка spell_proc НЕ нужна — 1291728 (Dummy) получает прок из DB2
-- (ProcFlags + ProcBasePPM ~3 с хастом) автоматически. Не добавляйте строку с PPM = 0.
-- Родные проки 431551/1261525 гасит spell_pal_hero_proc_disabled_ex (CheckProc = false).

-- ============================================================================
-- PAL_REV2 (28.09.2026, v2): упрощения -> как на ретейле, известные ошибки,
-- недостающие таланты. Повторный прогон безопасен.
-- ============================================================================

-- Спасённый Светом: старый AuraScript на 157047 не мог сработать (прок-аура паладина
-- не видит урон по союзнику) — теперь UnitScript в основной части, привязки не нужно.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_pal_saved_by_the_light_ex';
DELETE FROM `spell_proc` WHERE `SpellId` = 157047;

REPLACE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Священный скакун: стоковый скрипт TC (выбор расовой ауры). В пустой базе его нет.
(190784, 'spell_pal_divine_steed'),
-- Страж: распад стаков + задержка от трат Силы Света
(389539, 'spell_pal_sentinel_decay_ex'),
(53600,  'spell_pal_sentinel_spend_ex'),
(85673,  'spell_pal_sentinel_spend_ex'),
(427453, 'spell_pal_sentinel_spend_ex'),
-- Божественное взыскание: PvP ×0.75 доп. ударов
(20271,  'spell_pal_divine_exaction_pvp_ex'),
(24275,  'spell_pal_divine_exaction_pvp_ex'),
(31935,  'spell_pal_divine_exaction_pvp_ex'),
-- Вестник солнца: Утренняя звезда, Рассвет на союзнике, лучи Аватара солнца,
-- Вечное пламя от Затяжного сияния (без прямого хила)
(431482, 'spell_pal_morning_star_ex'),
(431381, 'spell_pal_dawnlight_hot_ex'),
(431911, 'spell_pal_suns_avatar_beam_ex'),
(431939, 'spell_pal_suns_avatar_beam_ex'),
(156322, 'spell_pal_eternal_flame_lr_ex'),
-- Кузнец света: Молот и наковальня у Света (крит Правосудия 275773 -> 433722)
(275773, 'spell_pal_hammer_and_anvil_ex'),
-- Кузнец света: Божественное вдохновение, Мастерская работа, Общая решимость,
-- Отражение сияния
(432964, 'spell_pal_divine_inspiration_ex'),
(35395,  'spell_pal_masterwork_consume_ex'),
(53595,  'spell_pal_masterwork_consume_ex'),
(204019, 'spell_pal_masterwork_consume_ex'),
(20473,  'spell_pal_masterwork_consume_ex'),
(465,    'spell_pal_shared_resolve_devotion_ex'),
(432502, 'spell_pal_shared_resolve_armament_ex'),
(432496, 'spell_pal_shared_resolve_armament_ex'),
(432616, 'spell_pal_reflection_sacred_weapon_ex'),
(441590, 'spell_pal_reflection_sacred_weapon_ex'),
(432607, 'spell_pal_reflection_holy_bulwark_ex');

-- Божественное вдохновение 432964: в DB2 у таланта нет прок-данных (Dummy, серверный
-- скрипт). Строка даёт события «свои способности/заклинания» (урон и лечение);
-- шанс считает скрипт (RPPM 0.55 с хастом), ВКД 1 с — как в тултипе.
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES (432964,0x00,0,0,0,0,0,0x15510,0x0,0x3,0x2,0x0,0x0,0x0,0,100,1000,0);

-- ============================================================================
-- PAL_REV3 (28.09.2026, v3): сверка талантов с Raider.io (топ М+: Свет/Прот/Рет).
-- Таланты с Dummy «Server-side script» / динамическим значением. Повторный прогон безопасен.
-- ============================================================================
DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_pal_vengeful_wrath_ex','spell_pal_blessing_of_dusk_ex','spell_pal_rising_sunlight_ex',
  'spell_pal_afterimage_spend_ex','spell_pal_afterimage_echo_ex','spell_pal_shield_of_vengeance_talent_ex',
  'spell_pal_blessed_champion_ex','spell_pal_seething_flames_ex',
  'spell_pal_authoritative_rebuke_cleanse_ex','spell_pal_authoritative_rebuke_interrupt_ex');
DELETE FROM `spell_script_names` WHERE `spell_id` = 184662 AND `ScriptName` = 'spell_pal_shield_of_vengeance';

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Мстительный гнев: Молот гнева до +50% по раненой цели
(24275,   'spell_pal_vengeful_wrath_ex'),
-- Благословение сумерек: снижение урона от потерянного здоровья
(1241945, 'spell_pal_blessing_of_dusk_ex'),
-- Восходящий солнечный свет: +лечение по здоровью целей Частиц
(1277651, 'spell_pal_rising_sunlight_ex'),
-- Остаточный образ: счётчик трат Силы Света + отражение Слова славы
(53600,   'spell_pal_afterimage_spend_ex'),
(85673,   'spell_pal_afterimage_spend_ex'),
(85222,   'spell_pal_afterimage_spend_ex'),
(383328,  'spell_pal_afterimage_spend_ex'),
(85256,   'spell_pal_afterimage_spend_ex'),
(224266,  'spell_pal_afterimage_spend_ex'),
(53385,   'spell_pal_afterimage_spend_ex'),
(156322,  'spell_pal_afterimage_spend_ex'),
(427453,  'spell_pal_afterimage_spend_ex'),
(85673,   'spell_pal_afterimage_echo_ex'),
-- Щит возмездия (талант 1261562): Божественная защита кастует 184662;
-- сам щит — стоковый скрипт TC (в пустой базе привязки нет)
(498,     'spell_pal_shield_of_vengeance_talent_ex'),
(403876,  'spell_pal_shield_of_vengeance_talent_ex'),
(184662,  'spell_pal_shield_of_vengeance'),
-- Благословенный защитник: -25% по доп. целям Удара воина Света / храмовника
(35395,   'spell_pal_blessed_champion_ex'),
(407480,  'spell_pal_blessed_champion_ex'),
(406647,  'spell_pal_blessed_champion_ex'),
-- Кипящее пламя: +2 удара Испепеления
(255937,  'spell_pal_seething_flames_ex'),
-- Властное порицание (Кузнец света): Свет — Очищение, Прот — Порицание
(4987,    'spell_pal_authoritative_rebuke_cleanse_ex'),
(96231,   'spell_pal_authoritative_rebuke_interrupt_ex');

-- ============================================================================
-- PAL_REV4 (28.09.2026, v4): сверка с логами WCL топ-игроков (Рет/Прот/Свет, оба героических древа).
-- Повторный прогон безопасен.
-- ============================================================================
DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_pal_blessed_champion_judgment_ex','spell_pal_rush_of_light_ex',
  'spell_pal_tempered_in_battle_ex','spell_pal_tempered_bulwark_ex','spell_pal_truth_prevails_transfer_ex');
DELETE FROM `spell_script_names` WHERE `spell_id` = 20271 AND `ScriptName` = 'spell_pal_blessed_champion_ex';

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Благословенный защитник: Правосудие бьёт ещё 4 цели (-25% по ним)
(20271,   'spell_pal_blessed_champion_judgment_ex'),
(20271,   'spell_pal_blessed_champion_ex'),
-- Прилив Света: крит генератора Силы Света -> +5% скорости 10 с
(407067,  'spell_pal_rush_of_light_ex'),
-- Закалённый в бою: перенос избыточного лечения паладин <-> союзник со Святым оплотом
(469701,  'spell_pal_tempered_in_battle_ex'),
(432496,  'spell_pal_tempered_bulwark_ex'),
-- Истина превыше всего: 50% избытка самолечения -> 2 союзника (461529)
(461546,  'spell_pal_truth_prevails_transfer_ex');

-- Прилив Света: только криты (HitMask 0x2), КД 0.5 с, урон способностями/заклинаниями.
-- Закалённый в бою / Святой оплот: получение лечения (способность, заклинание, периодика).
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES
(407067,0x00,0,0,0,0,0,0x11110,0x0,0x1,0x2,0x2,0x1,0x0,0,100,500,0),
(469701,0x00,0,0,0,0,0,0x80008800,0x0,0x2,0x2,0x0,0x1,0x0,0,100,0,0),
(432496,0x00,0,0,0,0,0,0x80008800,0x0,0x2,0x2,0x0,0x1,0x0,0,100,0,0);

-- ============================================================================
-- PAL_REV5 (28.09.2026, v5): второй проход по логам WCL. Повторный прогон безопасен.
-- v6 (PAL_REV6_20260928), v7 (PAL_REV7_20260928) и v8 (PAL_REV8_20260928) — правки только в C++
-- (сборка/стабильность) + правка ядра Spell.cpp; этот файл без изменений.
-- ============================================================================
DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
  'spell_pal_born_in_sunlight_aw_ex','spell_pal_undying_embers_ex',
  'spell_pal_will_of_the_dawn_ex','spell_pal_armory_of_light_ex');
DELETE FROM `spell_script_names` WHERE `spell_id` = 1263920 AND `ScriptName` = 'spell_pal_hero_proc_disabled_ex';

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Рождённый в солнечном свете: 1264050 на время Гнева карателя; родной прок таланта выключен
(31884,   'spell_pal_born_in_sunlight_aw_ex'),
(454351,  'spell_pal_born_in_sunlight_aw_ex'),
(216331,  'spell_pal_born_in_sunlight_aw_ex'),
(231895,  'spell_pal_born_in_sunlight_aw_ex'),
(1263920, 'spell_pal_hero_proc_disabled_ex'),
-- Неугасимые угли: тики Очищающего огня лечат 125..300%
(1244019, 'spell_pal_undying_embers_ex'),
-- Воля рассвета: +40% скорости при падении ниже 35% (раз в минуту)
(431406,  'spell_pal_will_of_the_dawn_ex'),
-- Оружейная Света: 15% шанс -20% урона (щит: заклинания, без щита: ближний бой)
(1277443, 'spell_pal_armory_of_light_ex');

-- v9 (28.09): Великий крестоносец — обнуление КД Щита мстителя (31935).
-- 85416 — баф прока (ловим наложение), 85043 — пассивка таланта (ловим сам прок).
-- Скрипт не привязан к типу ауры/номеру эффекта, поэтому работает при любых данных.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_pal_grand_crusader_reset_ex';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(85043, 'spell_pal_grand_crusader_reset_ex'),
(85416, 'spell_pal_grand_crusader_reset_ex');

-- Неугасимые угли: периодический урон (тик 469882, фильтр в скрипте).
-- Воля рассвета: получение любого урона; E1/E2 (периодик и «по здоровью») не прокают.
REPLACE INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES
(1244019,0x00,0,0,0,0,0,0x40000,0x0,0x1,0x2,0x0,0x1,0x0,0,100,0,0),
(431406, 0x00,0,0,0,0,0,0x100000,0x0,0x0,0x0,0x0,0x0,0x3,0,100,0,0);
