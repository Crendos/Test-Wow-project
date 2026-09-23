# 04 — v2: плавное движение и follow-мастер

## Главная находка: инжекция move-пакетов НЕ нужна

Причинно: в master `MoveSplineInit::Launch()` (`src/server/game/Movement/Spline/MoveSplineInit.cpp:164`)
для **любого** юнита (включая объект Player без сокета) делает

```cpp
WorldPackets::Movement::MonsterMove packet;
packet.MoverGUID = unit->GetGUID();
packet.Pos = ...;
packet.InitializeSplineData(move_spline);
unit->SendMessageToSet(packet.Write(), true);
```

то есть по каждому `MovePoint()`/`MoveFollow()` ядро само рассылает наблюдателям
`SMSG_MONSTER_MOVE` с полными spline-траекториями (путь, скорости, флаги) —
свой `SendPacket` у бот-сессии молча дропит SMSG для себя, а реальные клиенты видят
обычный плавный шаг. Дополнительно `Unit::Update()` → `UpdateSplineMovement(diff)`
каждый тик продвигает позицию по сплайну на сервере — позиция в мире и атаки согласованы.

Поэтому в v2 движение устроено «как нельзя проще»: AI бота только вызывает
`MotionMaster->MovePoint / MoveFollow`, а ядро берёт пакетную работу на себя.

## Состояния поведения (приоритет сверху вниз)

1. **COMBAT** — выбранная цель в бою / бот в бою / мастер в бою (protect).
2. **FOLLOW** — после `.playerbots followme <имя>`: руковагенин вокруг мастера.
3. **PLANE** — фоновая жизнь (блуждание ±10 м, эмоуты, болтовня).

## Follow (фаза v2)

`HandleFollowTick` каждый тик проверяет:
- дистанцию до мастера < `_followDist` (3.5 м) → `MoveIdle()` и выход;
- каденс `_followRepointMs = 400` мс — не пересчитывать сплайн чаще;
- точку назначения: **позади мастера по его ориентации** с отступом `_followDist`;
- анти-спам по позиции мастера: если целевая точка сместилась < 1 м — не реланчить;
- `generatePath = true` — путь строится по MMap (если у вас собраны ммапы).

Итог: мастер идёт — каждые 400 мс перезапускается навигация, ядро шлёт MonsterMove,
бот плавно догоняет «хвост». Мастер стоит — бот стоит.

## Combat (v2-полировка)

- **Защита мастера**: если мастер в бою, цель ищется через `master->getAttackers()`
  (предельный API master). Боевой апдейт вначале помечает цель через
  `AttackerStateUpdate`, что заводит у камеры правильный бой.
- **Дальность спелов**: перед кастом проверяется `SpellInfo::GetMaxRange(false, _bot)`
  с допуском ×1.05 — бот не тратит спелы вне зоны.
- **Self-buffs**: определяются по «MaxRange(positive) == 0 && IsPositive()» и кастуются
  на себя (`CastSpellTargetArg(_bot)`).
- **Антиспам**: 2-секундный recast-таймер (в v3 сменим на реальный GCD).
- Ближние классы — masterом пригоняется `MoveFollow` вместо простого `MoveChase`
  (уже подавно рассылками для наблюдателей).

## Новые команды

| Команда | Действие |
|---|---|
| `.playerbots followme <имя>` | бот привязывается к вам и следует |
| `.playerbots stay <имя>` | отвязка, бот стоит |
| (без изменений) `.playerbots add/list/remove/removeall/ping + roster` | база MVP |

## Что НЕ входит в v2 (запланировано дальше)

- Точное соблюдение GCD/recovery по спеллу, повороты ходом, strafe;
- защита от 2-х целей одновременно (multi-target priority);
- по ходу: whisper-парсер (бот слушает шёпот мастера — консольное сейчас через чат-команды);
- режим «компаньон по приключению» (фаза 4).

## Техн. заметки

- Файлы полностью в `scripts/Custom/playerbots/` — правок ядра с v2 **не добавилось**
  (patсh остался прежним — `patches/0001-core-integration.diff`).
- Патч ядра повторно накатывать не нужно: просто обновите файлы модуля (или перезапустите
  `tools\apply_windows.cmd` — шаг git apply сработает «already applied» без проблем).
- Проверка компиляции (gcc-12.2, master a96d897):
  `PlayerbotAI.cpp.o`, `PlayerbotMgr.cpp.o`, `cs_playerbots.cpp.o` — 0 ошибок.
