# 01. Архитектура playerbots на TrinityCore master

Исследование сделано по исходникам master @ `a96d89772a96d275f2d40d2aa27e4a5e0252b04a`.
Все пути файлов, имена методов и номера строк относятся к нему; на вашем коммите
могут незначительно «поплыть».

---

## 1. Принцип playerbots

Бот — это **полноценный `Player` с серверной `WorldSession` без сокета**.
Сервер не замечает подмены: персонаж проходит штатный `HandlePlayerLogin`,
грузится из таблицы `characters`, попадает в мир, виден в списках /who, может
вступать в группу/гильдию, получать лут и квесты. Отличий от реального клиента
два:

1. входные опкоды генерирует не сеть, а AI (или они вообще не нужны — можно
   вызывать хендлеры напрямую: в master они public);
2. исходящие `SMSG_*` уходят в никуда (SendPacket дропается).

---

## 2. Как устроен вход игрока в master (и что обходим)

В master (начиная с DF/TWW) вход — **двухсокетная модель**: у клиента два
соединения (`CONNECTION_TYPE_REALM` и `CONNECTION_TYPE_INSTANCE`).

Цепочка (`CharacterHandler.cpp`):

```
HandlePlayerLoginOpcode(PlayerLogin&)          // public (WorldSession.h:1247)
  → SendConnectToInstance(...)                 // SMSG_CONNECT_TO: клиент открывает 2-й сокет
    ...клиент подключается к instance...
HandleContinuePlayerLogin()                    // public
  → LoginQueryHolder(account, guid), holder->Initialize()
  → SendPacket(ResumeComms) + RegisterTimeSync // ← сокетный танец
  → AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder))
       .AfterComplete([](){ HandlePlayerLogin(holder); })   // async
HandlePlayerLogin(LoginQueryHolder const&)     // public (WorldSession.h:1253)
  → Player::LoadFromDB(...), AddToWorld, MOTD, друзья, почта...
```

Для бота сокетная часть **не нужна**: достаточно воспроизвести середину —
установить `m_playerLoading`, проверить `IsLegitCharacterForAccount`, запустить
тот же async `LoginQueryHolder` с колбэком в `HandlePlayerLogin`. Это и делает
наш `WorldSession::LoginPlayerBot(guid)` (см. патч).

### Voiceassessor-стопперы для «сессии без сокета» (точки патча)

Найдено чтением `WorldSession.cpp` — поэтому интеграционный патч ядра
**обязателен**, без него бот упадёт/исчезнет мгновенно:

| # | Место | Что случится без патча |
|---|---|---|
| 1 | `WorldSession::Update`, стр. ~357: `if (IsConnectionIdle()) m_Socket[REALM]->CloseSocket();` | **Crash**: у бота `m_timeOutTime=0` → idle=true → разыменование `nullptr` сокета |
| 2 | `WorldSession::Update`, хвост, стр. ~547: `if (!m_Socket[CONNECTION_TYPE_REALM]) return false;` | `World::UpdateSessions` (World.cpp:2953+) **удалит сессию** на первом же тике |
| 3 | `WorldSession::SendPacket`, стр. ~253: спам `TC_LOG_ERROR` при `!m_Socket` | Безопасно (пакет дропается), но лог забьётся на сотни строк/сек — глушим флагом бота |
| 4 | `WorldSession::PlayerDisconnected()` требует ОБА сокета открытыми | Не трогаем — на пути бота не вызывается |
| 5 | `IsLegitCharacterForAccount`, `m_playerLoading` — **private** | Поэтому `LoginPlayerBot` живёт ВНУТРИ класса (см. патч) |

Хорошие новости:
- `World::AddSession(WorldSession*)` — **public** (World.cpp:351), очередь
  `addSessQueue` сама добавит сессию в `m_sessions` на следующем тике.
- Opcode-хендлеры WorldSession — **public** (WorldSession.h:1236): AI может
  вызывать `HandleGossipHelloOpcode`, `HandleCastSpellOpcode` и т.п. напрямую,
  как настоящий клиент.
- `ProcessQueryCallbacks()` — public-часть обычного `Update`, значит async-логин
  бота (DelayQueryHolder → callback) работает в штатном цикле `UpdateSessions`.
- `SessionMap m_sessions` ключуется **по accountId** → правило:
  **1 бот = 1 отдельный игровой аккаунт**.

---

## 3. Движение бота (два уровня)

### Уровень v1 (в каркасе): серверное следование + телепорт-снапы

`bot->GetMotionMaster()->MoveFollow(master, dist, angle)` — позиция меняется
на сервере, но клиенты её **не видят гладко** (движение игроков по протоколу
рассылает владелец — клиент). Поэтому v1 дополнительно рассылает наблюдателям
`WorldPackets::Movement::MoveUpdateTeleport` (MovementPackets.h:354) раз в
~400 мс. С точки зрения игрока бот «перемещается рывками», но уже живой:
стоит, ходит за вами, садится в комбат.

### Уровень v2 (цель): инжекция клиентских move-пакетов

`WorldSession::HandleMovementOpcode(OpcodeClient, MovementInfo&)` — public
(WorldSession.h:1384+). AI обновляет `bot->m_movementInfo` (скорости/флаги стоят
у Unit), двигает позицию серверно (тот же `PathGenerator`/mmaps, что у
существ) и вызывает хендлер — тот рассылает окружающим штатные `SMSG_MOVE_*`,
и у всех клиентов бот идёт ровно, как живой игрок. Нюанс из кода
(`HandleMovementOpcode`, стр. 492): `if (!mover->movespline->Finalized()) return;`
— инжектить можно только когда сплайн движения завершён; поэтому v2-движок у
playerbots исторически сам считает координатный шаг по diff, а не через
MotionMaster-сплайны.

### Анти-залипание

Бот-аккаунтам в патче выставляется игнор idle-kick; дополнительно стоит
выключить для них speed/teleport-detector'ы Warden-стиля, иначе серверная
телепортация (v1-снапы) будет расцениваться как спидхак. Точки:
`WorldSession::ValidateMovementInfo`, античит-подобные проверки в
MovementHandler — помечены TODO в патче.

---

## 4. Дизайн AI (как у «взрослых» playerbots, но компактно)

Трёхслойная модель Strategy/Trigger/Action в миниатюре:

```
команда/событие  →  BotState (Idle/Stay/Follow/Combat/Dead)
                         │
   Trigger (условия)     │   Action (действия)
   ─ inCombat?           │   ─ DoMeleeAttack / CastRotationSpell
   ─ masterTooFar?       │   ─ FollowMaster / StopFollow
   ─ needHeal(ally<?%)   │   ─ Chat reply / emote
                         ▼
   Ротация: world.playerbots_rotation (class, spec, priority, spellId, minPower, condition)
```

- **Боевая ротация — данные, не код**: записи `playerbots_rotation`
  (класс → приоритетный список spellId с условиями по ресурсу/HP цели).
  Под «про-игроков» позже добавляются: симкрафт-подобные условия
  (прок/DoT-трекинг), кик кастов, контроль, позиционка.
- **Тик**: `WorldScript::OnUpdate(diff)` → `PlayerbotMgr::Update` → AI каждого
  бота. Частота сетки команд/ротации регулируется `Playerbots.AiIntervalMs`.
- **Человечность**: задержки реакции (jitter), случайные эмоуты/фразы из
  таблицы, задержка «cast как у игрока» через GCD — закладываем в v1.1.

---

## 5. Роадмап

| Фаза | Содержание | Статус |
|---|---|---|
| 0 | Интеграционный патч + менеджер + `.playerbot add/remove` + follow/stay + базовая ротация | каркас готов |
| 1 | Движение v2 (инжект пакетов), лут/добыча, квесты вместе с мастером, гильд-чат ответы | — |
| 2 | Роли в группе (танк/хил/дпс стратегии), LFG-семейство опкодов, БГ | — |
| 3 | rndbots: автономное население мира (спавн, «прокачка», АХ-экономика, чат-болтовня) | — |
| 4 | **«Режим приключения»**: компаньоны — персональные боты с профилями поведения (поверх фаз 0–3) | после основных ботов |

Оценка объёма: фаза 0 ≈ 2–4 тыс. строк; к «про-уровню» (фазы 1–3) — 20–40 тыс.
строк с итерациями балансировки. Это осознанно цена отсутствия готового порта.
