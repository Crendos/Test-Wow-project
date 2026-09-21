-- =====================================================================
--  06_abilities.sql — способности, Nordrassil Core 7.3.5
--  Колонки сверены с дампами legion_world.sql и legion_hotfixes.sql
-- =====================================================================
--  Как в Legion устроен спелл:
--    * Сам спелл (эффекты, цели, ауры, КД) — в legion_hotfixes:
--        spell (265 строк в дампе), spell_effect (345), spell_misc,
--        spell_categories, spell_cooldowns, spell_aura_options, spell_power,
--        spell_scaling, spell_range, spell_radius, spell_duration,
--        spell_procs_per_minute (0 строк)
--      Большая часть — в клиентском DB2-кэше. НОВЫЙ спелл = БД + патч клиента.
--    * «Серверная» обвязка — в legion_world:
--        creature_template.spell1..spell8   — спеллы НПЦ прямо в шаблоне
--        creature_template_spell            — спеллы НПЦ (77 947 строк)
--        spell_script_names                 — привязка C++-скрипта (3 111)
--        spell_proc_event / spell_proc      — проки
--        spell_linked_spell                 — связи спеллов (2 513)
--        spell_scripts                      — DB-скрипты спелла
--        spell_target_position              — телепорт-цели (1 683)
--        npc_spellclick_spells              — спеллы по клику (3 436)
--
--  Для НПЦ чаще всего достаточно creature_template_spell + SmartAI-каста.
-- =====================================================================

-- ---------------------------------------------------------------------
-- 0. Проверки
-- ---------------------------------------------------------------------
SELECT entry, spell1, spell2, spell3, spell4, spell5, spell6, spell7, spell8
FROM `creature_template` WHERE entry = @MOB;

SELECT * FROM `creature_template_spell` WHERE entry = @MOB;
SELECT * FROM `spell_script_names` WHERE spell_id = @SPELL;
SELECT * FROM `spell_proc_event` WHERE entry = @SPELL;


-- ---------------------------------------------------------------------
-- 1. Дать НПЦ спелл через creature_template_spell
--    Колонки: entry, spell, difficultyMask, timerCast, comment
--    difficultyMask: битовая маска сложностей (0 = все)
--    timerCast: 0 = обычный каст, >0 = таймер
-- ---------------------------------------------------------------------
DELETE FROM `creature_template_spell` WHERE entry = @MOB AND spell = @SPELL;
INSERT INTO `creature_template_spell` (`entry`, `spell`, `difficultyMask`, `timerCast`, `comment`)
VALUES (@MOB, @SPELL, 0, 0, 'Мой спелл НПЦ');
-- НЕТ прямой команды .reload для creature_template_spell → рестарт worldserver.

-- Альтернатива:spell1..spell8 прямо в creature_template (есть .reload creature_template)
-- UPDATE `creature_template` SET spell1 = @SPELL WHERE entry = @MOB;


-- ---------------------------------------------------------------------
-- 2. Каст спелла через SmartAI (без creature_template_spell)
--    SMART_ACTION_CAST = 11, параметры: spellId, castFlags
--    SmartCastFlags (SmartScriptMgr.h:1635):
--      0x01 SMARTCAST_INTERRUPT_PREVIOUS  — прервать предыдущий каст
--      0x02 SMARTCAST_TRIGGERED           — мгновенно, без маны и времени каста
--      0x10 CAST_FORCE_TARGET_SELF        — цель кастует на себя
--      0x20 SMARTCAST_AURA_NOT_PRESENT    — только если на цели нет ауры спелла
--    Цели (SMART_TARGET): SELF=1, VICTIM=2, HOSTILE_RANDOM=5,
--      HOSTILE_RANDOM_NOT_TOP=6, ACTION_INVOKER=7, POSITION=8
-- ---------------------------------------------------------------------
-- Пример: каст по текущей цели (VICTIM) с прерыванием предыдущего
INSERT INTO `smart_scripts`
(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
 `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,
 `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,
 `action_param5`,`action_param6`,
 `target_type`,`target_param1`,`target_param2`,`target_param3`,
 `target_x`,`target_y`,`target_z`,`target_o`,`comment`)
VALUES
(@MOB, 0, 0, 0, 0, 0, 100, 0, 4000, 6000, 9000, 12000,  -- UPDATE_IC: initial 4-6с, repeat 9-12с
 11, @SPELL, 0x01, 0, 0, 0, 0,                            -- CAST, flags=INTERRUPT_PREVIOUS
 2, 0, 0, 0, 0, 0, 0, 0, 'Каст по цели');


-- ---------------------------------------------------------------------
-- 3. Наложить ауру (SMART_ACTION_ADD_AURA = 75)
-- ---------------------------------------------------------------------
INSERT INTO `smart_scripts`
(`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,
 `event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,
 `action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,
 `action_param5`,`action_param6`,
 `target_type`,`target_param1`,`target_param2`,`target_param3`,
 `target_x`,`target_y`,`target_z`,`target_o`,`comment`)
VALUES
(@MOB, 0, 1, 0, 4, 0, 100, 0, 0, 0, 0, 0,    -- AGGRO
 75, @AURA_SPELL, 0, 0, 0, 0, 0,             -- ADD_AURA
 1, 0, 0, 0, 0, 0, 0, 0, 'Аура на себя при аггро');


-- ---------------------------------------------------------------------
-- 4. Проки (spell_proc_event)
--    Колонки: entry, SchoolMask, SpellFamilyName, SpellFamilyMask0..3,
--             procFlags, procEx, ppmRate, CustomChance, Cooldown, effectmask
--    entry = ID спелла-прока
-- ---------------------------------------------------------------------
-- DELETE FROM `spell_proc_event` WHERE entry = @PROC_SPELL;
-- INSERT INTO `spell_proc_event`
-- (`entry`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,
--  `SpellFamilyMask2`,`SpellFamilyMask3`,`procFlags`,`procEx`,`ppmRate`,
--  `CustomChance`,`Cooldown`,`effectmask`)
-- VALUES (@PROC_SPELL, 0, @FAMILY_NAME, @MASK0, 0, 0, 0, @PROC_FLAGS, 0, 0, @CHANCE, 0, 0);
-- .reload spell_proc_event

-- Новая система проков (spell_proc):
-- spellId, schoolMask, spellFamilyName, spellFamilyMask0..3, typeMask, spellTypeMask,
-- spellPhaseMask, hitMask, attributesMask, ratePerMinute, chance, cooldown, charges, modcharges
-- .reload spell_proc


-- ---------------------------------------------------------------------
-- 5. Связи спеллов (spell_linked_spell)
--    Колонки: spell_trigger, spell_effect, type, caster, target, hastype, hastalent,
--             hasparam, hastype2, hastalent2, hasparam2, chance, cooldown, duration,
--             hitmask, removeMask, effectMask, targetCountType, targetCount,
--             actiontype, group, param, randList, comment
-- ---------------------------------------------------------------------
-- INSERT INTO `spell_linked_spell`
-- (`spell_trigger`,`spell_effect`,`type`,`caster`,`target`,`chance`,`cooldown`,
--  `duration`,`comment`)
-- VALUES (@TRIGGER_SPELL, @EFFECT_SPELL, 0, 0, 0, 100, 0, 0, 'связь спеллов');
-- .reload spell_linked_spell


-- ---------------------------------------------------------------------
-- 6. DB-скрипт спелла (spell_scripts)
--    Колонки: id, effIndex, delay, command, datalong, datalong2, dataint, x, y, z, o
--    id = ID спелла, effIndex = индекс эффекта
-- ---------------------------------------------------------------------
-- DELETE FROM `spell_scripts` WHERE id = @SPELL;
-- INSERT INTO `spell_scripts`
-- (`id`,`effIndex`,`delay`,`command`,`datalong`,`datalong2`,`dataint`,`x`,`y`,`z`,`o`)
-- VALUES (@SPELL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
-- .reload spell_scripts


-- ---------------------------------------------------------------------
-- 7. Привязка C++-скрипта к спеллу (spell_script_names)
--    Колонки: spell_id, ScriptName
--    ScriptName должен быть зарегистрирован в коде ядра
--    (src/server/scripts/... через RegisterSpellScript).
-- ---------------------------------------------------------------------
-- DELETE FROM `spell_script_names` WHERE spell_id = @SPELL;
-- INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`)
-- VALUES (@SPELL, 'spell_my_custom');
-- НЕТ прямой команды .reload → рестарт worldserver.


-- ---------------------------------------------------------------------
-- 8. Телепорт-цель спелла (spell_target_position)
--    Колонки: id, target_map, target_position_x, target_position_y,
--             target_position_z, target_orientation
-- ---------------------------------------------------------------------
-- DELETE FROM `spell_target_position` WHERE id = @SPELL;
-- INSERT INTO `spell_target_position`
-- (`id`,`target_map`,`target_position_x`,`target_position_y`,`target_position_z`,`target_orientation`)
-- VALUES (@SPELL, @MAP, @X, @Y, @Z, @O);
-- .reload spell_target_position


-- ---------------------------------------------------------------------
-- 9. Спелл по клику на НПЦ (npc_spellclick_spells)
--    Колонки: npc_entry, spell_id, cast_flags, user_type, add_npc_flag
-- ---------------------------------------------------------------------
-- DELETE FROM `npc_spellclick_spells` WHERE npc_entry = @MOB AND spell_id = @SPELL;
-- INSERT INTO `npc_spellclick_spells`
-- (`npc_entry`,`spell_id`,`cast_flags`,`user_type`,`add_npc_flag`)
-- VALUES (@MOB, @SPELL, 0, 0, 0);
-- .reload npc_spellclick_spells


-- ---------------------------------------------------------------------
-- 10. НОВЫЙ спелл (полный цикл)
--     1) Добавить строки в hotfixes: spell, spell_effect, spell_misc,
--        spell_categories, spell_cooldowns, spell_aura_options и т.д.
--     2) ОБЯЗАТЕЛЬНО пропатчить клиентские DB2 (spell.db2, spell-effect.db2, ...)
--        или DBCache — иначе клиент не увидит спелл.
--     3) При необходимости — spell_script_names + C++-скрипт для кастомной логики.
--     Одним SQL новый спелл в игре НЕ появится.
-- ---------------------------------------------------------------------


-- ---------------------------------------------------------------------
-- Применить  (команды сверены с cs_reload.cpp)
-- ---------------------------------------------------------------------
--   .reload creature_template       ← если менял spell1..spell8
--   .reload spell_proc_event
--   .reload spell_proc
--   .reload spell_linked_spell
--   .reload spell_scripts
--   .reload spell_target_position
--   .reload npc_spellclick_spells
--   .reload spell_area / spell_group / spell_group_stack_rules / spell_learn_spell
--   .reload spell                   ← всё спелловое разом
--   .reload smart_scripts           ← касты через SmartAI
--
-- НЕТ прямой команды .reload для:
--   creature_template_spell, spell_script_names, таблиц hotfixes (spell, spell_effect)
--   → нужен РЕСТАРТ worldserver.