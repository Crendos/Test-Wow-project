/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Менеджер фейковых WorldSession + спины состава (ростер/ротация).
 */
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Duration.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSocket.h"

#include <algorithm>

/* static */ PlayerbotMgr& PlayerbotMgr::Instance()
{
    static PlayerbotMgr mgr;
    return mgr;
}

PlayerbotMgr::PlayerbotMgr()
{
    m_enabled          = sConfigMgr->GetBoolDefault("Playerbots.Enabled", false);
    m_freeAccountStart = sConfigMgr->GetIntDefault("Playerbots.FreeAccountsStart", 9000);
    m_freeAccountEnd   = sConfigMgr->GetIntDefault("Playerbots.FreeAccountsEnd", 9499);
    m_maxBots          = sConfigMgr->GetIntDefault("Playerbots.MaxCount", 100);
    m_rosterRotationMin= sConfigMgr->GetIntDefault("Playerbots.Rotation.Minutes", 5);
}

// ---------------------------------------------------------------- naming

bool PlayerbotMgr::IsNameAllowedForBot(std::string_view name) const
{
    if (name.size() < 2)
        return false;

    char c0 = name[0];
    if (c0 == 'T' || c0 == 'C' || c0 == 'M')
        return true;
    // ANSI-подобные суффиксы из вашей buildbot-культуры (опционально)
    return false;
}

bool PlayerbotMgr::IsBot(Player* player) const
{
    return player && m_bots.count(player->GetSession()->GetAccountId()) != 0;
}

bool PlayerbotMgr::IsBotSession(uint32 accountId) const
{
    return m_bots.count(accountId) != 0;
}

std::vector<std::string> PlayerbotMgr::GetBotsOnline() const
{
    std::vector<std::string> names;
    names.reserve(m_bots.size());
    for (auto const& kv : m_bots)
        names.push_back(kv.second.name);
    return names;
}

// ---------------------------------------------------------------- create/login

PlayerbotMgr::Status PlayerbotMgr::AddBot(std::string const& botName, Player* caller, uint32 sec)
{
    (void)caller;
    if (!m_enabled)
        return Status::ServerError;

    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
    if (!guid)
        return Status::UnknownName;

    if (Player* p = ObjectAccessor::FindPlayer(guid))
    {
        // уже в мире: если это и есть бот того же имени — OK
        if (IsBot(p))
            return Status::AlreadyOnline;
        return Status::AlreadyOnline;
    }

    if (m_bots.size() >= m_maxBots)
        return Status::ServerError;

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (!accountId)
        return Status::UnknownName;         // кеш ещё не поднял запись

    if (m_bots.count(accountId))
        return Status::AlreadyOnline;

    // диапазон аккаунтов, зарезервированный под ботов
    if (m_freeAccountStart && (accountId < m_freeAccountStart || accountId > m_freeAccountEnd))
        TC_LOG_WARN("playerbots", "AddBot: accountId {} вне резервного диапазона [{}..{}]",
            accountId, m_freeAccountStart, m_freeAccountEnd);

    PlayerBotEntry entry;
    entry.accountId = accountId;
    entry.name = botName;
    m_bots.emplace(accountId, std::move(entry));

    Status st = CreateBot(accountId, botName, sec);
    if (st != Status::OK)
    {
        m_bots.erase(accountId);
        return st;
    }

    return Status::OK;
}

PlayerbotMgr::Status PlayerbotMgr::CreateBot(uint32 accountId, std::string const& botName, uint32 sec)
{
    auto itr = m_bots.find(accountId);
    if (itr == m_bots.end())
        return Status::ServerError;

    PlayerBotEntry& entry = itr->second;

    // WorldSession ctor master (WorldSession.h):
    //   (uint32 id, std::string&& name, uint32 battlenetAccountId, std::string&& battlenetAccountEmail,
    //    std::shared_ptr<WorldSocket>&& sock, AccountTypes sec, uint8 expansion, time_t mute_time,
    //    std::string&& os, Minutes timezoneOffset, uint32 build, ClientBuild::VariantId, LocaleConstant,
    //    uint32 recruiter, bool isARecruiter)
    entry.session = new WorldSession(
        accountId, std::string(botName),
        accountId,                                  // battlenetAccountId: для бота = gameAccountId (MVP)
        std::string(),                              // battlenetAccountEmail
        nullptr,                                    // sock = nullptr → бот-сессия
        AccountTypes(sec),
        EXPANSION_LEVEL_CURRENT,
        0,                                          // mute_time
        std::string("BOT"),                         // os
        Minutes::zero(),                            // timezoneOffset
        0,                                          // build
        ClientBuild::VariantId{},                   // {Platform,Arch,Type} по нулям
        LOCALE_enUS,
        0,                                          // recruiter
        false);

    switch (Login(entry))
    {
        case Status::OK:         break;
        case Status::NoPlayer:   return Status::NoPlayer;
        default:                 return Status::ServerError;
    }

    return Status::OK;
}

PlayerbotMgr::Status PlayerbotMgr::Login(PlayerBotEntry& entry)
{
    if (!entry.session)
        return Status::ServerError;

    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(entry.name);
    if (!guid)
        return Status::NoPlayer;

    // критичный порядок: пометить до того, как World::AddSession_ вытолкнет сессию в очередь
    entry.session->MarkAsPlayerBot();
    entry.session->LoginPlayerBot(guid);

    TC_LOG_INFO("playerbots", "Login: бот {} заходит (accountId {})", entry.name, entry.accountId);
    return Status::OK;
}

// ---------------------------------------------------------------- login-finish (хук из WorldSession::LoginPlayerBot)

void PlayerbotMgr::HandlePlayerBotLoggedIn(Player* player)
{
    auto itr = m_bots.find(player->GetSession()->GetAccountId());
    if (itr == m_bots.end())
        return;

    PlayerBotEntry& entry = itr->second;
    entry.bot = player;

    // заклинания этого бота (ленивая загрузка из world.playerbots_combat_spells)
    if (m_combatSpells.empty())
        LoadPlayerBotCombatSpells(m_combatSpells);
    auto sp = m_combatSpells.find(entry.name);
    if (sp != m_combatSpells.end())
        entry.combatSpells = sp->second;

    entry.ai = new PlayerbotAI(player, entry.combatSpells);

    TC_LOG_INFO("playerbots", "HandlePlayerBotLoggedIn: {} в мире (guid {})", entry.name, player->GetGUID().ToString());
}

// ---------------------------------------------------------------- logout

PlayerbotMgr::Status PlayerbotMgr::RemoveBot(std::string const& botName, Player* /*caller*/)
{
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
    if (!guid)
        return Status::UnknownName;

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    auto itr = m_bots.find(accountId);
    if (itr == m_bots.end())
        return Status::NoPlayer;

    PlayerBotEntry entry = itr->second;  // копия — порядок удаления важен
    m_bots.erase(itr);

    // сначала AI (он ссылается на Player), затем выход персонажа.
    // LogoutPlayer(true) сам сохранит и уберёт объект из мира.
    if (entry.ai)
        entry.ai->Destroy();

    if (entry.bot && entry.session)
    {
        entry.bot->SaveToDB(false);
        entry.session->LogoutPlayer(true);
    }

    // сама сессия удалится при следующем проходе World::UpdateSessions,
    // когда IsPlayerBot() сессии даст LogoutPlayer=true и Update() вернет false
    TC_LOG_INFO("playerbots", "RemoveBot: {} удален (accountId {})", botName, accountId);
    return Status::OK;
}

void PlayerbotMgr::RemoveAll()
{
    for (auto const& name : GetBotsOnline())
        RemoveBot(name);
}

PlayerbotAI* PlayerbotMgr::GetBotAI(std::string const& botName)
{
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
    if (!guid)
        return nullptr;
    auto itr = m_bots.find(sCharacterCache->GetCharacterAccountIdByGuid(guid));
    if (itr == m_bots.end())
        return nullptr;
    return itr->second.ai;
}

// ---------------------------------------------------------------- per-frame AI

void PlayerbotMgr::UpdateAI(uint32 diff)
{
    if (!m_enabled || m_bots.empty())
        return;

    for (auto& kv : m_bots)
    {
        PlayerBotEntry& e = kv.second;
        if (!e.ai || !e.bot)
            continue;

        // сессия ещё в очереди / на логине — бот промолчит этот тик
        if (!e.bot->IsInWorld() || e.bot->IsBeingTeleported())
            continue;

        e.ai->Update(diff);
    }
}

// ---------------------------------------------------------------- say

void PlayerbotMgr::BotSay(uint32 accountId, std::string const& text)
{
    auto itr = m_bots.find(accountId);
    if (itr == m_bots.end() || !itr->second.bot)
        return;

    itr->second.bot->Say(text, LANG_UNIVERSAL, itr->second.bot);
}

// ---------------------------------------------------------------- roster (world.playerbots_rotation)

static QueryResult QueryRoster()
{
    return WorldDatabase.Query("SELECT guid, name, classId FROM playerbots_rotation ORDER BY rotation_id");
}

void PlayerbotMgr::StartRoster()
{
    if (m_rosterRunning)
        return;

    m_rosterRunning = true;
    m_nextRotateSyncSec = 0;

    LoadRoster();
    TC_LOG_INFO("playerbots", "StartRoster: состав запущен, ротация каждые {} мин", m_rosterRotationMin);
}

void PlayerbotMgr::StopRoster()
{
    m_rosterRunning = false;
    TC_LOG_INFO("playerbots", "StopRoster: состав остановлен");
}

bool   PlayerbotMgr::IsRosterRunning() const        { return m_rosterRunning; }
uint32 PlayerbotMgr::GetRosterRotationMinutes() const { return m_rosterRotationMin; }

void PlayerbotMgr::SetRosterRotationMinutes(uint32 minutes)
{
    m_rosterRotationMin = std::max(1u, minutes);
}

void PlayerbotMgr::LoadRoster()
{
    if (QueryResult result = QueryRoster())
    {
        std::vector<std::string> all;
        do
        {
            Field* f = result->Fetch();
            all.push_back(f[1].GetString());
        } while (result->NextRow());

        // заходим первыми ROSTER_SIZE
        for (size_t i = 0; i < std::min<size_t>(ROSTER_SIZE, all.size()); ++i)
        {
            std::string const& nm = all[i];
            if (!ObjectAccessor::FindPlayer(sCharacterCache->GetCharacterGuidByName(nm)))
                AddBot(nm, nullptr, SEC_PLAYER);
        }
        TC_LOG_INFO("playerbots", "LoadRoster: загружено {} из playerbots_rotation", all.size());
    }
    else
        TC_LOG_ERROR("playerbots", "LoadRoster: таблица world.playerbots_rotation пуста/отсутствует");
}

void PlayerbotMgr::RotateRoster()
{
    if (QueryResult result = QueryRoster())
    {
        std::vector<std::string> all;
        do
        {
            Field* f = result->Fetch();
            all.push_back(f[1].GetString());
        } while (result->NextRow());

        size_t total = all.size();
        if (total <= ROSTER_SIZE)
            return;

        // определяем, сколько ботов из начала списка уже в мире сейчас
        size_t liveHead = 0;
        for (size_t i = 0; i < std::min<size_t>(ROSTER_SIZE, total); ++i)
            if (ObjectAccessor::FindPlayer(sCharacterCache->GetCharacterGuidByName(all[i])))
                ++liveHead;

        // ротация: заменяем на следующий слот
        size_t startIdx = liveHead;                      // куда сдвигать окно
        size_t winPos   = startIdx % total;
        if (winPos == 0)
            winPos = (startIdx ? total - 1 : 0);         // гасим переход 0<->total-1

        for (size_t i = 0; i < liveHead; ++i)
            if (!ObjectAccessor::FindPlayer(sCharacterCache->GetCharacterGuidByName(all[winPos])))
                AddBot(all[winPos], nullptr, SEC_PLAYER);   // missed: добавка

        // удаляем самый старый из заголовка
        if (liveHead)
            RemoveBot(all[0]);

        TC_LOG_INFO("playerbots", "RotateRoster: окно [{}) из {} ботов, живых в голове {}", winPos, total, liveHead);
    }
}

void PlayerbotMgr::RotateNow()
{
    if (!m_rosterRunning)
        return;
    RotateRoster();
}

// полезная кухня: авто-ротация по таймеру — вызывается из worldserver main loop
void PlayerbotMgr::LoadPlayerBots()
{
    if (!m_rosterRunning)
        return;

    // авто-ротация по мировому таймеру: раз в N минут
    // (реализация на уровне moon — World::GetWorldUpdateCount() даёт секунд не хранит,
    //  поэтому next-принцип: проверяем сравнительный сдвиг)
    //
    // Для MVP: тикаем ротацию из команд (.playerbots rotate now). Полноценный cron —
    // в следующей итерации (фаза 1 «spawning»).
}

/* static */ void PlayerbotMgr::LoadPlayerBotCombatSpells(std::unordered_map<std::string, std::vector<uint32>>& spells)
{
    spells.clear();
    // Опциональная БД-таблица world.playerbots_combat_spells (name, spellid)
    if (QueryResult result = WorldDatabase.Query(
        "SELECT name, spellid FROM playerbots_combat_spells ORDER BY name, priority"))
    {
        do
        {
            Field* f = result->Fetch();
            std::string name = f[0].GetString();
            uint32 spell = f[1].GetUInt32();
            spells[name].push_back(spell);
        } while (result->NextRow());
        TC_LOG_INFO("playerbots", "LoadPlayerBotCombatSpells: загружено {} групп спелов", spells.size());
    }
}
