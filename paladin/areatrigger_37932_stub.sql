-- ============================================================================
-- areatrigger_37932_stub.sql — СИМПТОМАТИЧЕСКИЙ фикс спама ошибок AreaTrigger 37932
--
-- СИМПТОМ (Server.log, сразу после логина, 14 подряд):
--   "AreaTrigger (AreaTriggerCreatePropertiesId: (Id: 37932, IsCustom: 0)) not created.
--    Invalid areatrigger create properties id"
--
-- ПРИЧИНА: аура 395 (SPELL_AURA_AREA_TRIGGER) при КАЖДОМ наложении создаёт AT {37932, IsCustom:0},
-- но пропсов 37932 нет НИГДЕ: ни в клиенте 12.1.0.69497, ни на wago (рендер 69952 = 404).
-- Ядро хранит ВСЕ пропсы (клиентские + из world-таблицы areatrigger_create_properties)
-- в ЕДИНОЙ карте с ключом {Id, IsCustom} → DB-строка с IsCustom=0 УДОВЛЕТВОРЯЕТ запрос ядра.
--
-- ЧТО ДЕЛАЕТ ФАЙЛ: добавляет НЕЙТРАЛЬНУЮ заглушку (сфера r=2, без действий, без AI-скрипта).
-- Убирает ошибку и спам; AT начинает корректно создаваться и умирать по длительности ауры.
-- ВАЖНО: это НЕ останавливает сам прок-цикл храмовника — его источник ищем отдельно
-- (SQL-охота по serverside_spell_effect + PalDump v4, маркеры «!!! ЦИКЛ? / !!! АУРА-ШТОРМ»).
--
-- КАК СТАВИТЬ (Navicat):
--   1) правый клик по базе  world  →  Execute SQL File...  →  этот файл  →  Start.
--   2) Полный перезапуск worldserver (таблицы читаются только при старте).
-- ПРОВЕРКА: последний запрос файла обязан вернуть ДВЕ строки, в обеих cnt = 1.
-- ============================================================================

-- Шаг 1. Шаблон AT (обязателен: без него create-пропсы не загрузятся,
--        а с AreaTriggerId=0 будет Template=nullptr — класс краша 6006/204019)
REPLACE INTO `areatrigger_template` (`Id`,`IsCustom`,`Flags`,`ActionSetId`,`ActionSetFlags`)
VALUES (37932,0,0,0,0);

-- Шаг 2. Пропсы создания: создать, если строки нет...
INSERT INTO `areatrigger_create_properties`
  (`Id`,`IsCustom`,`AreaTriggerId`,`IsAreatriggerCustom`,`Flags`,`MoveCurveId`,`ScaleCurveId`,`MorphCurveId`,`FacingCurveId`,
   `AnimId`,`AnimKitId`,`DecalPropertiesId`,`SpellForVisuals`,`PositionalSoundKitId`,`TimeToTargetScale`,`Speed`,`SpeedIsTime`,
   `Shape`,`ShapeData0`,`ShapeData1`,`ShapeData2`,`ShapeData3`,`ShapeData4`,`ShapeData5`,`ShapeData6`,`ShapeData7`,
   `Roll`,`Pitch`,`Yaw`,`TargetRoll`,`TargetPitch`,`TargetYaw`,`ScriptName`,`VerifiedBuild`)
SELECT 37932,0,37932,0,0,0,0,0,0,-1,0,0,NULL,0,0,0,0,0,2.0,2.0,0,0,0,0,0,0,0,0,0,0,0,0,'',0
WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties` WHERE `Id`=37932 AND `IsCustom`=0);

-- Шаг 2а. ...и починить, если строка ЕСТЬ, но битая (ядро её отложило при загрузке:
--         ссылка на несуществующий шаблон или невалидная форма — иначе ошибки бы не было)
UPDATE `areatrigger_create_properties`
   SET `AreaTriggerId`=37932, `IsAreatriggerCustom`=0, `Flags`=0,
       `Shape`=0, `ShapeData0`=2.0, `ShapeData1`=2.0, `ScriptName`=''
 WHERE `Id`=37932 AND `IsCustom`=0;

-- Шаг 3. ПРОВЕРКА (обе строки cnt=1; иначе — остановись и пришли мне вывод)
SELECT 'areatrigger_template' AS tbl, COUNT(*) AS cnt FROM `areatrigger_template`
 WHERE `Id`=37932 AND `IsCustom`=0
UNION ALL
SELECT 'areatrigger_create_properties' AS tbl, COUNT(*) AS cnt FROM `areatrigger_create_properties`
 WHERE `Id`=37932 AND `IsCustom`=0;
