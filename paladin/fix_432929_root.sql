-- ============================================================================
-- fix_432929_root.sql — КОРНЕВОЙ фикс прок-цикла «Божественный молот»
-- (заменяет fix_432929_retail.sql — тот был НЕЭФФЕКТИВЕН, см. ниже)
--
-- ПОЛНАЯ ЦЕПОЧКА (проверено по твоим SQL-выдачам и коду ядра):
--   hotfix.spell_aura_options (432929): ProcChance=101, ProcTypeMask=(0, 4)
--     бит 0x4 = PROC_FLAG_2_CAST_SUCCESSFUL — «прокать после КАЖДОГО успешного
--     каста» (Unit.h / SpellMgr.h). SpellInfo берёт прок-флаги именно отсюда
--     (SpellInfo.cpp:1390: ProcFlags = _options->ProcTypeMask).
--   → генератор проков ядра видит ProcFlags != 0 и создаёт БЕЗУСЛОВНЫЙ прок
--     (шанс 101%, кулдауна нет) → кастует 198034 после любой способности
--     → аура молота перевешивается (E0 тик 2с + E1 AT 37932 = «3-4 бафа»).
--
-- ПОЧЕМУ ПРЕЖНИЙ ФИКС НЕ РАБОТАЛ: загрузчик world.spell_proc ЗАПОЛНЯЕТ нулевые
-- поля строки из DB2 («take defaults from dbcs», SpellMgr.cpp:1573-1581) —
-- строка из одних нулей = тот же безусловный прок. Без маркер-строки в логе
-- это и поймала твоя проверка уровня 3.
--
-- КАК РАБОТАЕТ ЭТОТ ФИКС:
--   ЧАСТЬ 1 (корень): ProcTypeMask=(0,0) в hotfix.spell_aura_options →
--     SpellInfo::ProcFlags = {0,0} → генератор пропускает спелл целиком
--     («Nothing to do if no flags set») → прока НЕТ. По-ретейловски чисто:
--     у молота нет проков с кастов, он приходит только от ТДА (наш код партии 9).
--   ЧАСТЬ 2 (ремень безопасности): world.spell_proc с Chance=0.01 (0.01%).
--     Ненулевой шанс НЕ бэкфиллится → даже если ЧАСТЬ 1 не дойдёт до сервера,
--     прок сможет сработать лишь раз на ~10000 кастов. А когда ЧАСТЬ 1
--     работает, эта строка даёт МАРКЕР в Server.log (см. проверку ниже).
--
-- КАК СТАВИТЬ (Navicat, ВНИМАНИЕ — две разные базы!):
--   ЧАСТЬ 1: правый клик по hotfix-БАЗЕ (та, где ты делал Q6/Q7) → New Query →
--     вставь блок ЧАСТИ 1 → Execute. В гриде: одна строка, ProcTypeMask1=0,
--     ProcTypeMask2=0.
--   ЧАСТЬ 2: правый клик по базе world → New Query → блок ЧАСТИ 2 → Execute.
--     В гриде: одна строка, Chance=0.01.
--     (если раньше выполнял fix_432929_retail.sql — он перезапишется этой
--      строкой, отдельно удалять не нужно)
--   Затем ПОЛНЫЙ перезапуск worldserver.
--
-- ПРОВЕРКИ ПОСЛЕ РЕСТАРТА:
--   1) Server.log:  findstr /i "ProcFlags" Server.log
--      → ДОЛЖНА появиться строка
--        "The `spell_proc` table entry for spellId 432929 doesn't have any
--         `ProcFlags` value defined, proc will not be triggered."
--      Это ДОКАЗАТЕЛЬСТВО, что ЧАСТЬ 1 сработала (ProcFlags стал 0).
--      Если строки НЕТ — ЧАСТЬ 1 не доехала (проверь, что UPDATE делался в
--      hotfix-базу); цикл всё равно держит ремень ЧАСТИ 2 (0.01%).
--   2) В игре: молот только после ТДА; маунт/предмет/атаки бафы не трогают;
--      строк «Invalid areatrigger create properties id» нет.
-- ============================================================================

-- ============================ ЧАСТЬ 1 (hotfix-база!) ========================
UPDATE `spell_aura_options`
   SET `ProcTypeMask1` = 0, `ProcTypeMask2` = 0
 WHERE `SpellID` = 432929;

-- проверка ЧАСТИ 1: одна строка, ProcTypeMask1=0, ProcTypeMask2=0
SELECT `ID`, `SpellID`, `ProcChance`, `ProcTypeMask1`, `ProcTypeMask2`
  FROM `spell_aura_options` WHERE `SpellID` = 432929;

-- ============================ ЧАСТЬ 2 (world) ===============================
REPLACE INTO `spell_proc`
  (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,
   `ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,
   `ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`)
VALUES
  (432929, 0,0,0,0,0,0, 0,0,0,0,0,0,0, 0, 0.01, 0, 0);

-- проверка ЧАСТИ 2: одна строка, Chance = 0.01
SELECT `SpellId`, `Chance`, `Cooldown` FROM `spell_proc` WHERE `SpellId` = 432929;
