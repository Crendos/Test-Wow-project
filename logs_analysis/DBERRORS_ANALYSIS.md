# DBErrors — анализ трёх файлов и решение (24.09.2026, rev.2 — сверено с кодом TC master)

Файлы:
- `DBErrors_head100.txt` — первые 100 строк
- `DBErrors_tail100.txt` — последние 100 строк
- `DBErrors_unique.txt` — топ-500 уникальных со счётчиками (шапка: **922 628 уникальных из 9 252 033**; эти 500 покрывают 523 524 вхождения)

> DBErrors.log append-only без ротации — сумма всех рестартов. Маркер `ProcFlags` (432929) пишется 1 раз/рестарт, в топ-500 по частоте не попадает — отсутствие в head/tail/unique ≠ фикс не работает. Проверка: `findstr /i "ProcFlags" DBErrors.log` на свежем логе.

**SQL-решение: `paladin/dberrors_cleanup.sql`** (все блоки, сверены с исходниками TC master).

---

## 1. head100 — 100/100: BroadcastText locale ruRU

```
Hotfix locale table for storage BroadcastText.db2 references row that does not exist <ID> locale ruRU!
```

- **Код:** `DB2DatabaseLoader.cpp:270` — строка локали hotfix-таблицы ссылается на несуществующую запись основной таблицы.
- **Причина:** в `broadcast_text_locale` (hotfixes) есть ruRU-ID без пары в `broadcast_text` (частичный мерж локалей пака).
- **Критичность:** 🟢 — урон не ломает, тексты просто без локали.
- **Фикс:** `DELETE FROM broadcast_text_locale WHERE ID NOT IN (SELECT ID FROM broadcast_text);` → **ЧАСТЬ 1 SQL**.

---

## 2. unique (топ-500)

| Категория | × в топ-500 | Суть | Фикс (world, если не указано иное) |
|---|---:|---|---|
| SmartAI: creature **is not using SmartAI** | ~492 | **Важно:** код `SmartScriptMgr.cpp` логирует это, когда строка `smart_scripts` **есть**, а `creature_template.AIName ≠ 'SmartAI'` → скрипты мёртвые и так не работают | **2a**: DELETE мёртвых SAI (сохранить поведение) *или* включить AIName (изменит поведение) |
| equipment_id без creature_equip_template | 2 (Entry 229979, 9100727) | `ObjectMgr.cpp` — спавн ссылает equipment_id=1, шаблона нет; ядро runtime ставит 0 | **2e**: `UPDATE creature SET equipment_id=0 WHERE id IN (…)` |
| pool_creature 369 без pool_template | 1 (294×) | `PoolMgr.cpp` — orphan-пул | **2f**: DELETE из pool_creature |
| spell_totem_model 157153 | 1 (210×) | `SpellMgr.cpp` — спелла нет в DBC 12.1 | **2g**: DELETE |
| ui_map_quest_line → questline 231 | 1 (187×) | `ObjectMgr.cpp` — пустая/несуществующая квестовая линия (UI) | **2h**: DELETE |
| Item 40582 create info | 1 (168×) | `ObjectMgr.cpp:3806` — `playercreateinfo_item.amount < 0` «удалить предмет», которого нет в DB2-старте | **2i**: DELETE amount<0 |
| SMART_EVENT_QUEST_OBJ_COMPLETION objective 0 | 1 (**391765×!**) | `SmartScriptMgr.cpp`: event_type=**48**, objective в `event_param1`=0 → событие всегда skip. **Самое частое событие SmartAI** | **2d**: DELETE event_type=48 AND event_param1=0 |
| GameObject not using SmartGameObjectAI | 1 | аналог 2a для source_type=1 | **2a** (GO-блок) |

Паладинских/классовых строк: **0**.

---

## 3. tail100 — SmartAI линки + 2 особняка

1. **82+ строк** `Link Event N not found` — в `smart_scripts` колонка **`link`** (не `event_links`!) указывает на несуществующий `id` события. Фикс **2b**: обнулить `link` у битых строк (или DELETE).
2. **`Entry 204019 SourceType 1`** — gameobject с ID спелла Благ. молота; битый линк. Наш молот = AT 6006 + `at_pal_blessed_hammer` (пар.8), SmartAI не нужен → DELETE **2c**.
3. **`bad mapid 618`** — `BattlegroundMgr.cpp`: таблица **`battleground_scripts`**, MapId=618 не существует/не BG → фикс **2j**: DELETE.

---

## 4. Чего в файлах нет (не пугаться)

- `ProcFlags`/`432929`/`37932`/`spell_pal_*` — маркеры не в этих срезах (см. шапку).
- Ошибок импорта наших 10 SQL-партий — нет.
- `AreaTrigger … Invalid` — это Server.log, не DBErrors.

---

## 5. Порядок применения

1. Остановить worldserver.
2. **hotfixes**: ЧАСТЬ 1 SQL (BroadcastText locale).
3. **world**: ЧАСТЬ 2 SQL (2a–2j; 2a — вариант A по умолчанию; 2c — сначала SELECT).
4. Удалить/переименовать старый `DBErrors.log` (иначе старые ошибки останутся в хвосте).
5. Рестарт worldserver.
6. Прогнать findstr-чеклист из конца `dberrors_cleanup.sql`.
7. Отдельно убедиться: `findstr /i "ProcFlags" DBErrors.log` — строка про 432929 **должна** быть (класс-фикс в силе).

**Вердикт:** да, чинится — почти всё SQL из world/hotfixes. На паладина-фиксы не влияет, можно делать до или после партии 11.

---

## 6. Готов к партие 11

Жду ID из `paladin/NEEDED_IDS_12x.md` («имя = ID»):
**Рет:** Удары Света, Порыв Света, Небесный молот, Святитель, Правосудие Света, Воздаяние Света, Вердикт верховного лорда, Светозар, Небесная порука.
**Хил:** Шок небес, Свет небес, Частица Света, Частица добродетели, Частица веры, оба Маяка спасителя, Яркий проблеск.
