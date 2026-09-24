-- ============================================================================
-- fix_432929_retail.sql — фикс прок-цикла «Божественный молот» ПО-РЕТЕЙЛОВСКИ
-- (заменяет прежний fix_432929_proc_icd.sql: REPLACE перезапишет его строку)
--
-- РЕТЕЙЛ-ПОВЕДЕНИЕ (warcraft.wiki / Method / IcyVeins, Midnight 12.x):
--   «Divine Hammer — пассивка Темплара: Divine Toll призывает молоты,
--    вращающиеся вокруг вас 8 сек, урон каждые 2 сек (кап 8 целей);
--    каждая потраченная Сила Света продлевает молот на +0.5 сек».
--   ПРОКОВ С КАСТОВ НА РЕТЕЙЛЕ НЕТ — молот приходит ТОЛЬКО от ТДА.
--
-- ЧТО БЫЛО У НАС: пассивка 432929 (аура 42 PROC_TRIGGER_SPELL → кастует 198034)
--   с ProcChance=101 (всегда) и без ограничений → молот перевешивался от КАЖДОЙ
--   способности (в т.ч. маунт/предмет) = «3-4 бафа каждые 1-2 сек» + спам
--   «AreaTrigger 37932 not created» (пропсов AT в клиенте 12.1.0.69497 нет).
--   На клиентских данных у 432929 нет кулдауна/категории/PPM — потому что на
--   ретейле триггеры проков игроков живут в СЕРВЕРНОМ коде, а не в db2.
--
-- КАК РАБОТАЕТ ФИКС:
--   world.spell_proc имеет приоритет над автогенератором проков ядра
--   (SpellMgr::LoadSpellProcs: "Data already present in DB, overwrites default
--   proc"). Строка с ProcFlags=0 = «прок-условий нет» → генерик-прок выключен.
--   Легитимный молот (от ТДА + продление за ОС) даёт наш код партии 9:
--   spell_pal_divine_toll_templar_ex + spell_pal_sotr_shake_heavens_ex.
--
-- КАК СТАВИТЬ (Navicat):
--   1) правый клик по базе world → Execute SQL File... → этот файл → Start.
--      (если раньше ставил fix_432929_proc_icd.sql — ничего удалять не надо,
--       REPLACE перезапишет строку; кулдаун 1 час больше не используется)
--   2) Полный перезапуск worldserver.
--
-- ПРОВЕРКИ:
--   - Navicat: последний запрос = одна строка (432929, Cooldown 0).
--   - Server.log при старте: строка
--     "The `spell_proc` table entry for spellId 432929 doesn't have any
--      `ProcFlags` value defined, proc will not be triggered."
--     ЭТО ОЖИДАЕМО — так ядро сообщает, что прок 432929 отключён нами.
--   - В игре: молот появляется ТОЛЬКО после ТДА; висит ~8с, тикает каждые 2с,
--     каждая потраченная ОС продлевает на +0.5с; от маунта/предмета/атак НЕ
--     перевешивается; строки AT 37932 больше не сыплются.
-- ============================================================================

REPLACE INTO `spell_proc`
  (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,
   `ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,
   `ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES
  (432929, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

-- ПРОВЕРКА: одна строка, Cooldown = 0
SELECT `SpellId`, `Chance`, `Cooldown` FROM `spell_proc` WHERE `SpellId` = 432929;
