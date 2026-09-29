/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Менеджер фейковых WorldSession + спины состава (ростер/ротация).
 */
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "DBCEnums.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Duration.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSocket.h"

#include <algorithm>
#include <cctype>

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
    m_qaAccountStart   = sConfigMgr->GetIntDefault("Playerbots.QAAccountsStart", 9500);
    m_qaAccountEnd     = sConfigMgr->GetIntDefault("Playerbots.QAAccountsEnd", 9599);
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

bool PlayerbotMgr::IsQAAccount(uint32 accountId) const
{
    return m_qaAccountStart != 0
        && accountId >= m_qaAccountStart
        && accountId <= m_qaAccountEnd;
}

bool PlayerbotMgr::IsBotQA(std::string const& botName) const
{
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
    if (!guid)
        return false;
    return IsQAAccount(sCharacterCache->GetCharacterAccountIdByGuid(guid));
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
    if (m_combatSpells.empty())
        LoadPlayerBotCombatSpells(m_combatSpells);

    // v6.1: роль бота определяется диапазоном аккаунта (Playerbots.QAAccountsStart/End)
    entry.ai = new PlayerbotAI(player, ResolveKnowledge(entry.name, player->GetClass()),
        IsQAAccount(player->GetSession()->GetAccountId()));

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
    {
        // v6: закрыть лог боя с SUMMARY, если бот выходит из режима манекена
        if (entry.ai->IsDummyMode())
            entry.ai->StopDummy("бот выходит");
        entry.ai->Destroy();
    }

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

/* static */ std::unordered_map<uint8, std::vector<BotKnowledge>> PlayerbotMgr::LoadClassKnowledgeTable()
{
    std::unordered_map<uint8, std::vector<BotKnowledge>> out;
    if (QueryResult result = WorldDatabase.Query(
        "SELECT class_id, spellid, kind, priority, self_hp_max, target_hp_min, target_hp_max, maintain_aura FROM playerbots_class_knowledge ORDER BY class_id, priority DESC"))
    {
        do
        {
            Field* f = result->Fetch();
            BotKnowledge k;
            k.spellId      = f[1].GetUInt32();
            k.kind         = BotKnowledge::Kind(f[2].GetUInt8());
            k.priority     = f[3].GetUInt16();
            k.selfHpMax    = f[4].GetUInt8();
            k.targetHpMin  = f[5].GetUInt8();
            k.targetHpMax  = f[6].GetUInt8();
            k.maintainAura = f[7].GetBool();
            out[f[0].GetUInt8()].push_back(k);
        } while (result->NextRow());
        TC_LOG_INFO("playerbots", "LoadClassKnowledgeTable: загружено {} классовых групп", out.size());
    }
    return out;
}

std::vector<BotKnowledge> PlayerbotMgr::ResolveKnowledge(std::string const& botName, uint8 classId)
{
    // 1) legacy per-name (world.playerbots_combat_spells) — обратная совместимость с v0/v2
    auto const& legacy = m_combatSpells.find(botName);
    if (legacy != m_combatSpells.end() && !legacy->second.empty())
    {
        std::vector<BotKnowledge> out;
        for (uint32 sid : legacy->second)
            out.push_back(BotKnowledge{ sid, BotKnowledge::Kind::Damage, 100, 100, 0, 100, false });
        return out;
    }

    // 2) пер-class таблица (world.playerbots_class_knowledge)
    auto& table = m_classKnowledge;
    if (table.empty())
        table = LoadClassKnowledgeTable();

    auto itr = table.find(classId);
    if (itr != table.end())
        return itr->second;

    // 3) пусто — AI сам построит из спелбукка персонажа
    return {};
}

// ---------------------------------------------------------------- v5: босс-правила

static BossRule::Trigger ParseBossTrigger(std::string_view s)
{
    if (s == "boss_aura")    return BossRule::Trigger::BossAura;
    if (s == "bot_aura")     return BossRule::Trigger::BotAura;
    if (s == "hp_below")     return BossRule::Trigger::BossHpBelow;
    if (s == "always")       return BossRule::Trigger::Always;
    return BossRule::Trigger::BossCast;   // "boss_cast" и любой мусор → каст босса
}

static BossRule::Action ParseBossAction(std::string_view s)
{
    if (s == "run_from_boss") return BossRule::Action::RunFromBoss;
    if (s == "spread")        return BossRule::Action::Spread;
    if (s == "sidestep")      return BossRule::Action::Sidestep;
    if (s == "switch_target") return BossRule::Action::SwitchTarget;
    if (s == "use_defensive") return BossRule::Action::UseDefensive;
    if (s == "dispel_self")   return BossRule::Action::DispelSelf;
    if (s == "use_burst")     return BossRule::Action::UseBurst;
    return BossRule::Action::Interrupt;   // "interrupt" и мусор → interrupt
}

std::vector<BossRule> const* PlayerbotMgr::GetBossRules(uint32 bossEntry)
{
    if (!m_bossRulesLoaded)
    {
        m_bossRulesLoaded = true;
        if (QueryResult result = WorldDatabase.Query(
            "SELECT boss_entry, trigger_type, trigger_arg, action_type, action_arg, seq, cooldown_ms "
            "FROM playerbots_boss_rules ORDER BY boss_entry, seq"))
        {
            uint32 count = 0;
            do
            {
                Field* f = result->Fetch();
                BossRule r;
                r.BossEntry   = f[0].GetUInt32();
                r.TriggerType = ParseBossTrigger(f[1].GetString());
                r.TriggerArg  = f[2].GetUInt32();
                r.ActionType  = ParseBossAction(f[3].GetString());
                r.ActionArg   = f[4].GetUInt32();
                r.Seq         = f[5].GetUInt32();
                r.CooldownMs  = std::max<uint32>(f[6].GetUInt32(), 200u);
                m_bossRules[r.BossEntry].push_back(r);
                ++count;
            } while (result->NextRow());
            TC_LOG_INFO("playerbots", "GetBossRules: загружено {} правил босс-механик ({} боссов)",
                count, m_bossRules.size());
        }
        else
            TC_LOG_INFO("playerbots", "GetBossRules: таблица playerbots_boss_rules пуста/отсутствует — без босс-механик");
    }

    auto itr = m_bossRules.find(bossEntry);
    return itr == m_bossRules.end() ? nullptr : &itr->second;
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

// ---------------------------------------------------------------- v6: создание персонажа

namespace
{
    std::string ToLowerCopy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    bool IsDigits(std::string const& s)
    {
        return !s.empty() && std::all_of(s.begin(), s.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    // "" / "dps"|"dd" / "heal" / "tank" | имя спека en | числовой ID
    ChrSpecializationEntry const* ResolveSpecEntry(uint32 classId, std::string const& spec)
    {
        std::string want = ToLowerCopy(spec);

        if (IsDigits(want))
        {
            uint32 id = 0;
            try { id = uint32(std::stoul(want)); } catch (...) { return nullptr; }
            if (ChrSpecializationEntry const* e = sChrSpecializationStore.LookupEntry(id))
                if (e->ClassID == classId)
                    return e;
            return nullptr;
        }

        ChrSpecializationRole role = ChrSpecializationRole::Dps;
        bool roleFilter = true;
        if (want == "heal" || want == "healer")
            role = ChrSpecializationRole::Healer;
        else if (want == "tank" || want == "prot" || want == "protection")
            role = ChrSpecializationRole::Tank;
        else if (!want.empty() && want != "dps" && want != "dd" && want != "damage")
            roleFilter = false;    // это имя спека (en)

        ChrSpecializationEntry const* best = nullptr;
        // DB2Storage::iterator даёт указатели (T const*) — только указательная форма
        for (ChrSpecializationEntry const* pe : sChrSpecializationStore)
        {
            if (pe->ClassID != classId)
                continue;
            if (roleFilter)
            {
                if (pe->GetRole() != role)
                    continue;
            }
            else
            {
                std::string nm = ToLowerCopy(std::string(pe->Name[LOCALE_enUS] ? pe->Name[LOCALE_enUS] : ""));
                if (nm != want)
                    continue;
            }
            if (!best || pe->OrderIndex < best->OrderIndex)
                best = pe;
        }

        if (best || roleFilter)
            return best;

        // имя не распознано — не угадываем, ошибка на месте
        return nullptr;
    }
}

/* static */ uint32 PlayerbotMgr::ParseClassToken(std::string token)
{
    token = ToLowerCopy(std::move(token));
    if (IsDigits(token))
    {
        try { return uint32(std::stoul(token)); } catch (...) { return 0; }
    }

    struct ClassToken { char const* name; uint32 id; };
    static constexpr ClassToken tokens[] =
    {
        { "warrior",      1 }, { "paladin", 2 }, { "hunter", 3 }, { "rogue",   4 },
        { "priest",       5 }, { "death knight", 6 }, { "deathknight", 6 }, { "dk", 6 },
        { "shaman",       7 }, { "mage",    8 }, { "warlock", 9 }, { "monk",   11 },
        { "druid",       12 }, { "demon hunter", 13 }, { "demonhunter", 13 }, { "dh", 13 },
        { "evoker",      14 },
    };
    for (ClassToken const& t : tokens)
        if (token == t.name)
            return t.id;
    return 0;
}

void PlayerbotMgr::LoadHeroTalents()
{
    m_heroTalentsLoaded = true;
    if (QueryResult result = WorldDatabase.Query(
        "SELECT class, tree, spell_id FROM playerbots_hero_talents ORDER BY class, tree, spell_id"))
    {
        uint32 count = 0;
        do
        {
            Field* f = result->Fetch();
            std::string key = std::to_string(f[0].GetUInt32()) + ":" + ToLowerCopy(f[1].GetString());
            m_heroTalents[key].push_back(f[2].GetUInt32());
            ++count;
        } while (result->NextRow());
        TC_LOG_INFO("playerbots", "LoadHeroTalents: загружено {} спеллов геро-талантов ({} деревьев)",
            count, m_heroTalents.size());
    }
    else
        TC_LOG_INFO("playerbots", "LoadHeroTalents: таблица playerbots_hero_talents пуста/отсутствует — hero= ничего не выучит");
}

BotCreateResult PlayerbotMgr::CreateCharacter(uint32 accountId, BotCreateCriteria const& c)
{
    BotCreateResult res;

    if (!accountId || !c.classId)
    {
        res.error = "нужен существующий accountId и класс";
        return res;
    }

    // --- 1. аккаунт должен быть в auth ------------------------------------
    QueryResult acc = LoginDatabase.Query(
        ("SELECT id FROM account WHERE id = " + std::to_string(accountId)).c_str());
    if (!acc)
    {
        res.error = "аккаунт " + std::to_string(accountId) + " не найден в auth";
        return res;
    }

    // --- 2. race / class / gender -----------------------------------------
    auto racePlayable = [](uint32 r) -> bool
    {
        ChrRacesEntry const* e = sChrRacesStore.LookupEntry(r);
        return e && !e->GetFlags().HasFlag(ChrRacesFlag::NPCOnly);
    };

    uint32 race = c.raceId;
    if (race)
    {
        if (!racePlayable(race) || !sObjectMgr->GetPlayerInfo(race, c.classId))
        {
            res.error = "недопустимая комбинация race=" + std::to_string(race)
                + " class=" + std::to_string(c.classId);
            return res;
        }
    }
    else
    {
        for (uint32 r = 1; r <= 40 && !race; ++r)
            if (racePlayable(r) && sObjectMgr->GetPlayerInfo(r, c.classId))
                race = r;
        if (!race)
        {
            res.error = "для класса " + std::to_string(c.classId) + " не нашлось playable-расы";
            return res;
        }
        res.error += "race подобрана автоматически=" + std::to_string(race) + "; ";
    }

    uint8 gender = uint8(c.gender <= GENDER_FEMALE ? c.gender : GENDER_MALE);
    uint32 level = std::max<uint32>(1, std::min<uint32>(c.level,
        uint32(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))));

    // --- 3. имя (или случайное 3-4 латинских) ------------------------------
    auto validateName = [](std::string& n) -> std::string
    {
        if (!normalizePlayerName(n))
            return "пустое/некорректное после normalize";
        if (ObjectMgr::CheckPlayerName(n, sWorld->GetDefaultDbcLocale(), true) != CHAR_NAME_SUCCESS)
            return "недопустимые символы или длина";
        if (sObjectMgr->IsReservedName(n))
            return "зарезервировано сервером";
        if (sCharacterCache->GetCharacterCacheByName(n))
            return "уже занято";
        return std::string();
    };

    std::string name = c.name;
    if (name.empty())
    {
        for (int attempt = 0; attempt < 80; ++attempt)
        {
            uint32 len = urand(3, 4);
            std::string cand;
            cand += char('A' + urand(0, 25));
            for (uint32 i = 1; i < len; ++i)
                cand += char('a' + urand(0, 25));
            if (validateName(cand).empty())
            {
                name = cand;
                break;
            }
        }
        if (name.empty())
        {
            res.error = "не удалось подобрать свободное имя (80 попыток)";
            return res;
        }
    }
    else
    {
        std::string err = validateName(name);
        if (!err.empty())
        {
            res.error = "имя '" + name + "': " + err;
            return res;
        }
    }

    // --- 4. временная фейковая сессия аккаунта (паттерн CreateBot) --------
    WorldSession* session = new WorldSession(
        accountId, std::string(name),
        accountId,                                  // battlenetAccountId = gameAccountId (MVP)
        std::string(),                              // battlenetAccountEmail
        nullptr,                                    // sock = nullptr — бот-сессия
        AccountTypes(SEC_PLAYER),
        EXPANSION_LEVEL_CURRENT,
        0,                                          // mute_time
        std::string("BOT"),                         // os
        Minutes::zero(),                            // timezoneOffset
        0,                                          // build
        ClientBuild::VariantId{},
        LOCALE_enUS,
        0,                                          // recruiter
        false);
    session->MarkAsPlayerBot();                     // тишина SendPacket (сокета и так нет)

    // --- 5. Player + Create (паттерн CharacterHandler::HandlePlayerCreate) -
    WorldPackets::Character::CharacterCreateInfo createInfo;
    createInfo.Race  = uint8(race);
    createInfo.Class = uint8(c.classId);
    createInfo.Sex   = gender;
    createInfo.Name  = name;
    // Customizations пусты — валидно: ValidateAppearance на CREATE не вызывается

    ObjectGuid::LowType guidLow = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
    std::shared_ptr<Player> newChar(new Player(session), [](Player* p)
    {
        p->CleanupsBeforeDelete();
        delete p;
    });
    newChar->GetMotionMaster()->Initialize();

    if (!newChar->Create(guidLow, &createInfo))
    {
        res.error = "Player::Create отказал — проверь race/class/gender";
        newChar.reset();
        delete session;
        return res;
    }

    // --- 6. спец ДО уровня (иначе LearnSpecializationSpells не сработает) --
    ChrSpecializationEntry const* specEntry = ResolveSpecEntry(c.classId, c.spec);
    if (!specEntry)
    {
        res.error = "спек не найден для класса: '" + c.spec + "'";
        newChar.reset();
        delete session;
        return res;
    }
    newChar->SetPrimarySpecialization(specEntry->ID);
    newChar->SetActiveTalentGroup(uint8(specEntry->OrderIndex >= 0 ? specEntry->OrderIndex : 0));

    // --- 7. уровень: GiveLevel выучит спеллы спека <= уровня --------------
    if (level > 1)
        newChar->GiveLevel(uint8(level));

    // --- 8. геро-таланты: списки spell_id из world.playerbots_hero_talents -
    if (!c.hero.empty())
    {
        if (!m_heroTalentsLoaded)
            LoadHeroTalents();
        std::string key = std::to_string(c.classId) + ":" + ToLowerCopy(c.hero);
        auto it = m_heroTalents.find(key);
        if (it == m_heroTalents.end())
            res.error += "геро-дерево '" + c.hero + "' не найдено в playerbots_hero_talents; ";
        else
            for (uint32 spellId : it->second)
                newChar->LearnSpell(spellId, false);
    }

    // --- 9. предметы: в сумки; экипировка — .playerbots equip после входа --
    for (uint32 itemId : c.items)
        if (!newChar->StoreNewItemInBestSlots(itemId, 1, ItemContext::NONE))
            res.error += "item " + std::to_string(itemId) + " не удалось уложить (нет itemtemplate/места); ";

    // --- 10. сохранение: синхронный коммит (команда ждёт результата) -------
    LoginDatabaseTransaction loginTrans = LoginDatabase.BeginTransaction();
    CharacterDatabaseTransaction charTrans = CharacterDatabase.BeginTransaction();
    newChar->SaveToDB(loginTrans, charTrans, true);
    CharacterDatabase.DirectCommitTransaction(charTrans);
    LoginDatabase.DirectCommitTransaction(loginTrans);

    // --- 11. кэш имён + событие создания -----------------------------------
    sCharacterCache->AddCharacterCacheEntry(newChar->GetGUID(), accountId, name,
        gender, uint8(race), uint8(c.classId), uint8(newChar->GetLevel()), false);
    sScriptMgr->OnPlayerCreate(newChar.get());

    res.guid = uint32(newChar->GetGUID().GetCounter());
    res.name = name;
    res.ok = true;

    newChar.reset();    // CleanupsBeforeDelete + delete (сессия ещё нужна — ниже)
    delete session;

    // предупреждение, если аккаунт вне обоих рабочих диапазонов (игровом и QA)
    bool inFree = accountId >= m_freeAccountStart && accountId <= m_freeAccountEnd;
    if (!inFree && !IsQAAccount(accountId))
        res.error += "внимание: accountId вне Playerbots.FreeAccountsStart/End ("
            + std::to_string(m_freeAccountStart) + ".." + std::to_string(m_freeAccountEnd)
            + ") и вне QA-диапазона; ";

    TC_LOG_INFO("playerbots",
        "CreateCharacter: {} (guid {}, account {}) class={} race={} gender={} level={} spec={} hero={}",
        res.name, res.guid, accountId, c.classId, race, uint32(gender), level,
        specEntry->ID, c.hero.empty() ? "-" : c.hero);
    return res;
}

