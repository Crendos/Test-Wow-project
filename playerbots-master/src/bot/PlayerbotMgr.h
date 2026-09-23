/*
 * PLAYERBOTS под TrinityCore master (12.x) — Фаза 0 MVP
 * Источник проекта: Test-Wow-project/playerbots-master
 */
#ifndef PLAYERBOT_MGR_H
#define PLAYERBOT_MGR_H

#include "Define.h"
#include "Knowledge.h"
#include "ObjectGuid.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class WorldSession;
class PlayerbotAI;

struct PlayerBotEntry
{
    WorldSession* session = nullptr;
    Player* bot = nullptr;
    PlayerbotAI* ai = nullptr;
    uint32 accountId = 0;
    std::string name;
    std::vector<uint32> combatSpells;
};

class PlayerbotMgr
{
public:
    static PlayerbotMgr& Instance();

    enum class Status : uint8
    {
        OK = 0, AlreadyOnline, UnknownName, NoFreeAccount, NoPlayer, ServerError
    };

    Status AddBot(std::string const& botName, Player* caller, uint32 sec);
    Status RemoveBot(std::string const& botName, Player* caller = nullptr);
    void   RemoveAll();

    bool IsBot(Player* player) const;
    bool IsBotSession(uint32 accountId) const;
    std::vector<std::string> GetBotsOnline() const;

    void UpdateAI(uint32 diff);
    void HandlePlayerBotLoggedIn(Player* player);
    PlayerbotAI* GetBotAI(std::string const& botName);
    void BotSay(uint32 accountId, std::string const& text);

    void StartRoster();
    void StopRoster();
    bool IsRosterRunning() const;
    uint32 GetRosterRotationMinutes() const;
    void SetRosterRotationMinutes(uint32 minutes);
    void RotateNow();

    static void LoadPlayerBotCombatSpells(std::unordered_map<std::string, std::vector<uint32>>& spells);
    void LoadPlayerBots();

    // v3: знание бота. Возвращает вектор правил (сперва legacy-таблица по имени,
    // затем таблица по классу; пустой вектор = авто-строить из спелбукка).
    std::vector<BotKnowledge> ResolveKnowledge(std::string const& botName, uint8 classId);
    static std::unordered_map<uint8 /*classId*/, std::vector<BotKnowledge>> LoadClassKnowledgeTable();

private:
    PlayerbotMgr();

    bool IsNameAllowedForBot(std::string_view name) const;
    Status CreateBot(uint32 accountId, std::string const& name, uint32 sec);
    Status Login(PlayerBotEntry& entry);
    void HandleBotAdded(std::string const& botName, Player* caller);
    void LoadRoster();
    void RotateRoster();
    void PlayerbotLogin(std::string const& botName);

    std::unordered_map<uint32 /*accountId*/, PlayerBotEntry> m_bots;
    std::unordered_map<std::string, std::vector<uint32>> m_combatSpells;
    std::unordered_map<uint8 /*classId*/, std::vector<BotKnowledge>> m_classKnowledge;

    bool     m_enabled = false;
    uint32   m_freeAccountStart = 0, m_freeAccountEnd = 0;
    uint32   m_maxBots = 100;
    uint32   m_rosterRotationMin = 5;   // минуты между ротациями
    static constexpr uint32 ROSTER_SIZE = 3;
    bool     m_rosterRunning = false;
    uint32   m_nextRotateSyncSec = 0;   // мировое время следующей ротации (сек)
};

#define sPlayerbotMgr PlayerbotMgr::Instance()

#endif // PLAYERBOT_MGR_H
