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

// v6: критерии создания персонажа командой .playerbots create
struct BotCreateCriteria
{
    uint32 classId = 0;              // CLASS_* (1..14); 0 = ошибка
    uint32 raceId = 0;               // RACE_*; 0 = авто-подбор валидной для класса
    uint32 gender = 0;               // GENDER_MALE / GENDER_FEMALE
    uint32 level = 1;
    std::string spec;                // "", "dps"/"dd", "heal", "tank" | имя спека en | числовой ID
    std::string hero;                // "" | ключ дерева (напр. "templar") — учит спеллы из world.playerbots_hero_talents
    std::vector<uint32> items;       // itemtemplate id; склад в сумки, экипировка потом: .playerbots equip
    std::string name;                // "" = случайный 3-4 латинских буквы
    std::string side;                // "" | "ally" | "horde" — сторона; авто-подбор расы класса (нельзя с race=)
};

struct BotCreateResult
{
    bool ok = false;
    std::string name;
    uint32 guid = 0;
    std::string error;               // если !ok — причина; иначе предупреждения
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

    // .playerbots hero <бот> <ветка[,ветка]|all> — дозаучить spells геро-деревьев на живом боте;
    // после учёта пересобирает знания AI. Возвращает сводку для чата.
    std::string LearnHeroTrees(Player* bot, std::string const& treesCsv);
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

    // v5: босс-механики — правила по creature-entry босса (world.playerbots_boss_rules)
    std::vector<BossRule> const* GetBossRules(uint32 bossEntry);

    // v6: создание персонажа на аккаунте по критериям (.playerbots create)
    BotCreateResult CreateCharacter(uint32 accountId, BotCreateCriteria const& criteria);
    static uint32 ParseClassToken(std::string token);   // "paladin"/"pala"/"2" → CLASS_*

    // v6.1: роли ботов. QA-бот = аккаунт в диапазоне Playerbots.QAAccountsStart/End.
    // QA: sweep, лог CAST_FAIL, быстрые ретраи фейлов. Игровые: обычная ротация.
    bool IsQAAccount(uint32 accountId) const;
    bool IsBotQA(std::string const& botName) const;      // имя → аккаунт → роль

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
    std::unordered_map<uint32 /*bossEntry*/, std::vector<BossRule>> m_bossRules;
    bool m_bossRulesLoaded = false;

    // v6: геро-таланты — спеллы дерева из world.playerbots_hero_talents (лениво)
    std::unordered_map<std::string /*"classId:tree"*/, std::vector<uint32>> m_heroTalents;
    bool m_heroTalentsLoaded = false;
    void LoadHeroTalents();
    // учёт spells одного/нескольких деревьев (csv + "all"); ошибки → err, возврат = сколько выучено
    uint32 LearnHeroTreeSpells(Player* target, uint8 classId, std::string const& treesCsv, std::string& err);

    bool     m_enabled = false;
    uint32   m_freeAccountStart = 0, m_freeAccountEnd = 0;
    uint32   m_qaAccountStart = 0, m_qaAccountEnd = 0;   // v6.1: диапазон QA-аккаунтов
    uint32   m_maxBots = 100;
    uint32   m_rosterRotationMin = 5;   // минуты между ротациями
    static constexpr uint32 ROSTER_SIZE = 3;
    bool     m_rosterRunning = false;
    uint32   m_nextRotateSyncSec = 0;   // мировое время следующей ротации (сек)
};

#define sPlayerbotMgr PlayerbotMgr::Instance()

#endif // PLAYERBOT_MGR_H
