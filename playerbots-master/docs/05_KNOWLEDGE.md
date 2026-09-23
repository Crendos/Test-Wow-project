# 05 — v3: классовое знание (спелы + когда что кастовать)

Бот знает класс и способности через трёхъярусный источник знания и набор правил.
Реализовано полностью в `src/bot/` (ядро не трогали).

## Иерархия источников (в порядке приоритета)

1. **legacy per-name** — таблица `world.playerbots_combat_spells` (v0-совместимость).
2. **ручные правила per-class** — новая таблица `world.playerbots_class_knowledge`
   (schema: `sql/world_playerbots_class_knowledge.sql`).
3. **авто-построение из спелбукка бота** — если для его класса нет записей:
   при логине `BuildKnowledgeFromSpellbook()` перебирает `GetSpellMap()` персонажа и
   на лету классифицирует каждый выученный спелл по эффектам SpellInfo:

| Признак | Назначение |
|---|---|
| `SPELL_EFFECT_INTERRUPT_CAST` | Interrupt (резерв, пока без реакции) |
| `SPELL_EFFECT_HEAL` + `IsPositive()` | Heal: на себя (HP<60), на мастера-в-группе (HP<50) |
| `SPELL_EFFECT_APPLY_AURA` + Positive + `MaxRange(getPos)==0` | SelfBuff (maintain-by-absence) |
| то же + `RecoveryTime >= 60000` | Defensive (Ice Block/Divine Shield и пр.) при HP<35 |
| `SPELL_EFFECT_SCHOOL_DAMAGE` + `!IsPositive()` | Damage-боевая с очерёдностью: `100 + min(RecoveryTime,30s)/1000` |

Wait-сидящие спелы (маунты) отфильтровываются по `SPELL_AURA_MOUNTED`.

## Правила применения «когда что»

Каждый тик (в COMBAT-состоянии) по приоритету:
1. `TryDefensive()` — большой защитный спелл при HP бота < `self_hp_max` (35%).
2. `TryHeal()` — себя (<60%) или мастера в группе (<50%, в дальности MaxRange·1.05).
3. Позиционирование (ближний чек — `MoveFollow` на GetMeleeDistance).
4. `TryAttackSpell()` — первый по priority Damage-спелл, если `SpellFits`:
   - IsReady по SpellHistory,
   - MaxRange·1.05,
   - диапазоны target_hp_min/target_hp_max (execute-правила),
   - selfHpMax (для treat-заклинаний).
5. Между кастами `m_recastTimerMs = 1500` — квитанрический GCD-подобный интервал
   (в v4 — точный GCD/recovery по спеллу).
6. Вне каста — `EnsureSelfBuffs()` (поддержка бафов `maintain_aura` отсутствием ауры).

## SQL-ручки (если хотите править вручную)

```sql
INSERT INTO playerbots_class_knowledge
  (class_id, spellid, kind, priority, self_hp_max, target_hp_min, target_hp_max, maintain_aura)
VALUES
  (8, 133,    0, 100, 100,   0, 100, 0),   -- Mage Fireball спам
  (8, 108853, 0, 120, 100,   0, 100, 0),   -- Mage Fire Blast инстант выше
  (8, 45438,  4, 500,  35,   0, 100, 0),   -- Mage Ice Block при HP<35
  (8, 543,    2,  40, 100,   0, 100, 1);   -- Mage Armor, поддерживаем
```

После INSERT **перезайдите бота** (`.playerbots remove/add`) либо перезапустите мир —
кэш знаний строится при логине персонажа.

## Отладка в зале

- `.playerbots book <имя>` — распечатать спелы, попавшие в знание бота (id из DBC);
- боту-лоadd не мешает невалидное ID в таблице — `SpellInfo == nullptr` просто скипается;

## Что дальше (v4+)

- точный GCD/recovery и cast-time учёт (CalcCastTime);
- реакция Interrupt на цель, AoE-сборки (`GetMaxAffectedTargets`), дебафф-управление;
- pet-логика охотника (отдельный под-тик);
- целевые выборы по группам (Tank/Healer/DPS-роли).
