-- ============================================================
-- Paladin fixes part 8: Blessed Hammer (204019) spiral AreaTrigger
-- ============================================================
-- Розничная механика: каст 204019 имеет Effect#1 = Create Area Trigger (6006).
-- Серверный AT (невидимая зона-диск ~2 ярда) летит по спирали (2 оборота, ~8.5 ярда, ~2.5 с),
-- урон 204301 вешается на врагов, через которых проходит молот (AI-скрипт at_pal_blessed_hammer).
-- Визуал вращающегося молота клиент рисует сам (SpellVisual заклинания).
--
-- Если TDB уже содержит строку 6006 (розничные данные) — она используется как есть,
-- наш скрипт лишь привязывается через ScriptName. Если строки нет — создаём свою со спиральным сплайном.
--
-- ПРАВКА 25.09.2026: первая версия вставляла AreaTriggerId=0 (→ Template=nullptr →
-- КРАШ при первом касте 204019) и не дописывала сплайн-точки (ошибка в WHERE NOT EXISTS).
-- Файл обновлён и САМОИСЦЕЛЯЕТСЯ: повторный запуск чинит уже испорченную базу.
-- ПРАВКА 25.09.2026 №2: форма была Polygon (Shape=3), но вершины полигона
-- (таблица areatrigger_create_properties_polygon_vertex) не заданы → поиск целей
-- пуст → «крутится, но урон/дебафф 204301 не применяются». Теперь ДИСК (Shape=7).

-- 0) ШАБЛОН AT — ОБЯЗАТЕЛЕН: без строки в areatrigger_template ядро (master 2026+)
-- хранит Template=NULL и ПАДАЕТ (ACCESS_VIOLATION в AreaTrigger::IsServerSide)
-- при первом же касте 204019. REPLACE безопасен и для TDB-варианта (Id=6006, IsCustom=0).
REPLACE INTO `areatrigger_template` (`Id`,`IsCustom`,`Flags`,`ActionSetId`,`ActionSetFlags`) VALUES (6006,0,0,0,0);


-- 1) Привязать AI-скрипт к существующей строке (если TDB её содержит)
UPDATE `areatrigger_create_properties` SET `ScriptName`='at_pal_blessed_hammer' WHERE `Id`=6006 AND `IsCustom`=0;

-- 2) Если строки нет — создать (диск r=2.5, высота 5, сплайн-спираль, 2.5 с на проход)
INSERT INTO `areatrigger_create_properties`
  (`Id`,`IsCustom`,`AreaTriggerId`,`IsAreatriggerCustom`,`Flags`,`MoveCurveId`,`ScaleCurveId`,`MorphCurveId`,`FacingCurveId`,
   `AnimId`,`AnimKitId`,`DecalPropertiesId`,`SpellForVisuals`,`TimeToTargetScale`,`Speed`,`SpeedIsTime`,
   `Shape`,`ShapeData0`,`ShapeData1`,`ShapeData2`,`ShapeData3`,`ShapeData4`,`ShapeData5`,`ShapeData6`,`ShapeData7`,
   `ScriptName`,`VerifiedBuild`)
SELECT 6006,0,6006,0,0,0,0,0,0,-1,0,0,NULL,0,2.5,1,7,0,0,2.5,2.5,5,5,-2,-2,'at_pal_blessed_hammer',0
WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties` WHERE `Id`=6006 AND `IsCustom`=0);

-- 2а) САМОИСЦЕЛЕНИЕ (25.09.2026): чинит уже вставленную старой версией файла битую
--     строку (AreaTriggerId=0 → Template=nullptr → ACCESS_VIOLATION при первом касте
--     204019). Безопасно: выполняется только если AreaTriggerId не 6006.
UPDATE `areatrigger_create_properties`
SET `AreaTriggerId`=6006, `IsAreatriggerCustom`=0
WHERE `Id`=6006 AND `IsCustom`=0 AND `AreaTriggerId`<>6006;

-- 2б) САМОИСЦЕЛЕНИЕ №2 (25.09.2026): форма Polygon без вершин (поиск целей пуст —
--     молот летит «вхолостую»). Переводим на ДИСК: r=2.5, высота 5,
--     z-диапазон от -2 до +3 относительно точки молота. Идемпотентно.
UPDATE `areatrigger_create_properties`
SET `Shape`=7, `ShapeData0`=0, `ShapeData1`=0, `ShapeData2`=2.5, `ShapeData3`=2.5,
    `ShapeData4`=5, `ShapeData5`=5, `ShapeData6`=-2, `ShapeData7`=-2
WHERE `Id`=6006 AND `IsCustom`=0
  AND (`Shape`<>7 OR `ShapeData0`<>0 OR `ShapeData1`<>0 OR `ShapeData2`<>2.5 OR `ShapeData3`<>2.5
    OR `ShapeData4`<>5 OR `ShapeData5`<>5 OR `ShapeData6`<>-2 OR `ShapeData7`<>-2);

-- 3) Точки спирали (только если строку создали мы, а не TDB)
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,0,0.600000,0.000000,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=0);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,1,0.738531,0.197889,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=1);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,2,0.804682,0.464583,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=2);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,3,0.773398,0.773398,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=3);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,4,0.629167,1.089749,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=4);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,5,0.368278,1.374432,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=5);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,6,0.000000,1.587500,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=6);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,7,-0.453473,1.692383,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=7);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,8,-0.958333,1.659882,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=8);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,9,-1.471666,1.471666,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=9);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,10,-1.944949,1.122917,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=10);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,11,-2.328284,0.623862,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=11);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,12,-2.575000,0.000000,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=12);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,13,-2.646234,-0.709056,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=13);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,14,-2.515082,-1.452083,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=14);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,15,-2.169934,-2.169934,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=15);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,16,-1.616667,-2.800149,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=16);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,17,-0.879446,-3.282135,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=17);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,18,-0.000000,-3.562500,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=18);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,19,0.964640,-3.600086,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=19);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,20,1.945833,-3.370282,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=20);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,21,2.868202,-2.868202,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=21);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,22,3.655349,-2.110417,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=22);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,23,4.235987,-1.135029,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=23);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,24,4.550000,-0.000000,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=24);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,25,4.553938,1.220224,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=25);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,26,4.225482,2.439583,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=26);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,27,3.566470,3.566470,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=27);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,28,2.604167,4.510549,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=28);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,29,1.390613,5.189839,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=29);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,30,0.000000,5.537500,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=30);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,31,-1.475808,5.507790,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=31);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,32,-2.933333,5.080682,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=32);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,33,-4.264738,4.264738,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=33);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,34,-5.365749,3.097917,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=34);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,35,-6.143691,1.646197,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=35);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,36,-6.525000,0.000000,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=36);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,37,-6.461641,-1.731392,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=37);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,38,-5.935882,-3.427083,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=38);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,39,-4.963006,-4.963006,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=39);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,40,-3.591667,-6.220949,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=40);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,41,-1.901781,-7.097542,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=41);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,42,-0.000000,-7.512500,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=42);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,43,1.986975,-7.415493,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=43);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,44,3.920833,-6.791083,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=44);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,45,5.661274,-5.661274,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=45);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,46,7.076149,-4.085417,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=46);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,47,8.051394,-2.157365,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=47);
INSERT INTO `areatrigger_create_properties_spline_point` (`AreaTriggerCreatePropertiesId`,`IsCustom`,`Idx`,`X`,`Y`,`Z`,`VerifiedBuild`)
SELECT 6006,0,48,8.500000,-0.000000,1.200000,0 WHERE NOT EXISTS (SELECT 1 FROM `areatrigger_create_properties_spline_point` WHERE `AreaTriggerCreatePropertiesId`=6006 AND `IsCustom`=0 AND `Idx`=48);
-- Проверка после импорта:
--   SELECT Id, Shape, Speed, SpeedIsTime, ScriptName FROM areatrigger_create_properties WHERE Id=6006 AND IsCustom=0;
--   SELECT COUNT(*) FROM areatrigger_create_properties_spline_point WHERE AreaTriggerCreatePropertiesId=6006 AND IsCustom=0;  -- 49, если строка наша
