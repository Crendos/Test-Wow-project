/*
 * PLAYERBOTS под TrinityCore master — v3 (классовое знание: спелы + когда что кастовать)
 * Источники знания (по приоритету):
 *   1) world.playerbots_combat_spells (legacy per-name),
 *   2) world.playerbots_class_knowledge (per-class, ручные правила),
 *   3) авто-построение из РЕАЛЬНОГО спелбукка персонажа (GetSpellMap),
 *      с классификацией по эффектам SpellInfo.
 */
#ifndef PLAYERBOT_AI_H
#define PLAYERBOT_AI_H

#include "Define.h"
#include "Knowledge.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;
class Unit;
class SpellInfo;

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

    // команды
    void PingMaster();
    void BotSay(std::string const& msg);
    void EmoteMe(uint32 emote);
    std::vector<uint32> ListKnownSpelIDs() const;   // выгрузка для .playerbots book

private:
    // ---- v3: знание ----
    void BuildKnowledgeFromSpellbook();    // авто-классификация по эффектам
    void EnsureSelfBuffs();                // поддержка self-бафов (каждый тик вне каста)
    bool TryDefensive();                   // большая защита при малом HP
    bool TryHeal();                        // лечение себя/мастера
    bool TryAttackSpell();                 // основной цикл урона (priority, range, gates)

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
    bool SpellFits(BotKnowledge const& k, Unit* target) const; // range/gates/ready

    static float GetDefaultCombatRange(uint8 cls);

    // plane
    void RandomWander(uint32 diff);
    void RandomEmote(uint32 diff);
    void RandomChat(uint32 diff);

    Player* _bot;
    std::vector<BotKnowledge> m_knowledge; // отсортированы по priority DESC

    // --- combat ---
    Unit*  m_combatTarget  = nullptr;
    uint32 m_recastTimerMs = 0;            // v3: общий GCD-подобный анти-спам (1.5с)

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
