/*
 * PLAYERBOTS под TrinityCore master — v2 (плавное движение + follow-мастер)
 * Поведение одного бота: состояния COMBAT > FOLLOW > PLANE.
 * Всё происходит в world-thread (tick из PlayerbotMgr::UpdateAI через WorldScript::OnUpdate).
 */
#ifndef PLAYERBOT_AI_H
#define PLAYERBOT_AI_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;
class Unit;
class SpellInfo;

class PlayerbotAI
{
public:
    PlayerbotAI(Player* bot, std::vector<uint32> combatSpells);
    ~PlayerbotAI();

    void Update(uint32 diff);
    void Destroy() { delete this; }

    // --- master binding (.playerbots followme / .playerbots stay) ---
    void SetMasterAndFollow(Player* master);
    void ClearFollow();
    bool IsFollowMode() const { return _followEnabled; }
    ObjectGuid GetMasterGUID() const { return _masterGuid; }

    // команды (.playerbots cmd ...)
    void PingMaster();
    void BotSay(std::string const& msg);
    void EmoteMe(uint32 emote);

private:
    // состояния
    void DoCombatAI(uint32 diff);
    void HandleFollowTick(uint32 diff);
    void HandlePlaneTick(uint32 diff);

    void DoFindTarget();          // свой таргет бота
    Unit* FindProtectTarget();    // цель-угроза мастера (если мастер есть) 
    void TargetSelectionIfNeeded();
    float GetMeleeDistance() const { return 4.0f; }

    bool CastSpellAt(uint32 spellId);
    bool IsSpellReady(uint32 spellId) const;
    bool IsSelfBuff(SpellInfo const* info) const;

    static float GetDefaultCombatRange(uint8 cls);

    // plane
    void RandomWander(uint32 diff);
    void RandomEmote(uint32 diff);
    void RandomChat(uint32 diff);

    Player* _bot;
    std::vector<uint32> m_combatSpells;   // приоритет: [0] — fallback-базовый

    // --- combat ---
    Unit*  m_combatTarget  = nullptr;
    uint32 m_recastTimerMs = 0;           // минимальный интервал между кастами (v2: по GCD)

    // --- follow ---
    ObjectGuid _masterGuid;
    bool   _followEnabled  = false;
    float  _followDist     = 3.5f;
    uint32 _followRepointMs= 0;           // каденс перезапуска сплайна (мс)
    float  _lastFollowX = 0.0f, _lastFollowY = 0.0f; // анти-спам: дедуп по мастерской точке

    // --- plane ---
    float  m_combatRange      = 0.0f;

    uint32 m_planeEmoteTimer  = 0;
    uint32 m_planeWanderTimer = 0;
    uint32 m_planeChatTimer   = 0;
};

#endif // PLAYERBOT_AI_H
