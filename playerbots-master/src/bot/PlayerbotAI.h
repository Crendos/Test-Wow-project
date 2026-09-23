/*
 * PLAYERBOTS под TrinityCore master — v4 (активная ротация + системная синергия)
 * Всё происходит в world-thread (tick из PlayerbotMgr::UpdateAI через WorldScript::OnUpdate).
 */
#ifndef PLAYERBOT_AI_H
#define PLAYERBOT_AI_H

#include "Define.h"
#include "Knowledge.h"
#include "ObjectGuid.h"
#include <string>
#include <unordered_set>
#include <vector>

class Player;
class Unit;
class SpellInfo;
class PlayerbotMgr;

class PlayerbotAI
{
public:
    PlayerbotAI(Player* bot, std::vector<BotKnowledge> knowledge);
    ~PlayerbotAI();

    void Update(uint32 diff);
    void Destroy() { delete this; }

    // --- master binding (.playerbots followme / .playerbots stay) ---
    void SetMasterAndFollow(Player* master);
    void ClearFollow();
    bool IsFollowMode() const { return _followEnabled; }

    // команды (.playerbots ...) 
    void PingMaster();
    void BotSay(std::string const& msg);
    void EmoteMe(uint32 emote);
    void EquipBestItems();                         // .playerbots equip — подбор из сумок
    std::vector<uint32> ListKnownSpelIDs() const;  // .playerbots book

    // для mgr: битва/gravel-филлер после длинного тикта
    void NotifyCombatEnter();
    bool HasManualOverride() const { return !m_knowledge.empty() && _manualKnowledge; }

private:
    // ---- знание и авто-структурирование ----
    void BuildKnowledgeFromSpellbook();
    void EnsureSelfBuffs();
    bool TryDefensive();
    bool TryHeal();
    bool TryAttackSpell();              // + DoT/burst/gate идентификатор
    bool SpellFits(BotKnowledge const& k, Unit* target) const;
    void RegisterCastFail(uint32 spellId);  // меморизация невыгодного каста
    void ClearCastBlacklist();

    // состояния
    void DoCombatAI(uint32 diff);
    void HandleFollowTick(uint32 diff);
    void HandlePlaneTick(uint32 diff);

    void DoFindTarget();
    Unit* FindProtectTarget();
    void TargetSelectionIfNeeded();
    float GetMeleeDistance() const { return 4.0f; }

    bool CastSpellAt(uint32 spellId, Unit* target);
    bool IsSpellReady(uint32 spellId) const;
    bool IsSelfBuffSpell(SpellInfo const* info) const;

    // ---- v5: босс-механики (правила из world.playerbots_boss_rules) ----
    bool ProcessBossRules(Unit* target);        // true = действие съело тик/GCD
    bool EvaluateBossTrigger(BossRule const& r, Unit* boss) const;
    bool ExecuteBossAction(BossRule const& r, Unit* boss);
    void RuleMoveTo(float x, float y, float z, uint32 blockMs);

    static float GetDefaultCombatRange(uint8 cls);
    static int   ClassArmorSubClass(uint8 cls);   // auto-equip: максимум по броне

    // ---- авто-одевание (gear scoring) ----
    int  ScoreItem(class Item const* item) const;
    bool ItemFitsClass(class Item const* item, int slot) const;
    static int SlotForInventoryType(int32 invType);

    // plane
    void RandomWander(uint32 diff);
    void RandomEmote(uint32 diff);
    void RandomChat(uint32 diff);

    Player* _bot;
    std::vector<BotKnowledge> m_knowledge;
    bool _manualKnowledge = false;

    // --- combat ---
    Unit*  m_combatTarget  = nullptr;
    uint32 m_recastTimerMs = 0;
    uint32 m_combatEnterMs = 0;          // timestamp входа в бой (для burst-окна)
    bool   _combatActive   = false;
    std::unordered_set<uint32> m_castBlacklist; // заброшенные в текущем бою спелы

    // --- v5: босс-механики ---
    uint32 m_bossEntry     = 0;          // entry босса, чьи правила активны
    uint32 m_ruleMoveBlockMs = 0;        // обычное позиционирование/follow заморожено (бот двигается по правилу)
    uint32 m_forceBurstMs  = 0;          // принудительное burst-окно (правило use_burst)
    std::unordered_map<uint32 /*seq*/, uint32 /*ms*/> m_ruleCooldowns;

    // --- follow ---
    ObjectGuid _masterGuid;
    bool   _followEnabled  = false;
    float  _followDist     = 3.5f;
    uint32 _followRepointMs= 0;
    float  _lastFollowX = 0.0f, _lastFollowY = 0.0f;

    // --- plane ---
    float  m_combatRange      = 0.0f;

    uint32 m_planeEmoteTimer  = 0;
    uint32 m_planeWanderTimer = 0;
    uint32 m_planeChatTimer   = 0;
};

#endif // PLAYERBOT_AI_H
