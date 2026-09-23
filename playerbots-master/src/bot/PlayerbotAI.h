/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Поведение одного бота: PLANE (фоновая «жизнь») + COMBAT (помощь по цели).
 * Всё поведение выполняется в world-thread (tick из PlayerbotMgr::UpdateAI).
 */
#ifndef PLAYERBOT_AI_H
#define PLAYERBOT_AI_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;
class Unit;

class PlayerbotAI
{
public:
    PlayerbotAI(Player* bot, std::vector<uint32> combatSpells);
    ~PlayerbotAI();

    void Update(uint32 diff);
    void Destroy() { delete this; }

    // команды (.playerbots cmd ...)
    void PingMaster();
    void FollowStop();
    void BotSay(std::string const& msg);
    void EmoteMe(uint32 emote);

    // plane: фоновые действия
    void RandomWander(uint32 diff);
    void RandomEmote(uint32 diff);
    void RandomChat(uint32 diff);

    // combat: автоассист
    void DoCombatAI(uint32 diff);
    void DoFindTarget();
    void TargetSelectionIfNeeded();

    bool CastSpellAt(uint32 spellId);
    bool IsSpellReady(uint32 spellId) const;

    float GetMeleeDistance() const  { return 4.0f; }
    float GetMaxRange() const       { return m_combatRange; }

private:
    static float GetDefaultCombatRange(uint8 cls);

    Player* _bot;
    std::vector<uint32> m_combatSpells;   // приоритет: [0] — fallback-базовый

    Unit*  m_combatTarget  = nullptr;
    uint32 m_recastTimerMs = 0;           // минимальный интервал между кастами (v0)

    float  m_combatRange      = 0.0f;

    uint32 m_planeEmoteTimer  = 0;        // тики до следующего эвента
    uint32 m_planeWanderTimer = 0;
    uint32 m_planeChatTimer   = 0;
};

#endif // PLAYERBOT_AI_H
