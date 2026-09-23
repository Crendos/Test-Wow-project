/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 */
#include "PlayerbotAI.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementPackets.h"
#include "Player.h"
#include "Random.h"
#include "SharedDefines.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"



PlayerbotAI::PlayerbotAI(Player* bot, std::vector<uint32> combatSpells)
    : _bot(bot)
    , m_combatSpells(std::move(combatSpells))
{
    m_combatRange = GetDefaultCombatRange(_bot->GetClass());

    m_planeEmoteTimer  = urand(5000, 15000);
    m_planeWanderTimer = urand(10000, 30000);
    m_planeChatTimer   = urand(15000, 45000);
}

PlayerbotAI::~PlayerbotAI() = default;

/*static*/ float PlayerbotAI::GetDefaultCombatRange(uint8 cls)
{
    switch (cls)
    {
        case CLASS_HUNTER:  return 30.0f;
        case CLASS_MAGE:    return 30.0f;
        case CLASS_PRIEST:  return 25.0f;
        case CLASS_WARLOCK: return 30.0f;
        case CLASS_DRUID:   return 25.0f;   // без form-aware логики в MVP
        case CLASS_PALADIN: return 10.0f;
        default:            return 5.0f;    // warrior/rogue/sham/dk/dh/monk/evoker
    }
}

// ---------------------------------------------------------------- tick

void PlayerbotAI::Update(uint32 diff)
{
    if (!_bot->IsInWorld() || _bot->IsBeingTeleported())
        return;

    if (m_recastTimerMs > diff)
        m_recastTimerMs -= diff;
    else
        m_recastTimerMs = 0;

    // COMBAT приоритетнее PLANE
    if (_bot->IsInCombat() || (_bot->GetSelectedUnit() && _bot->GetSelectedUnit()->IsInCombat()))
    {
        DoCombatAI(diff);
        return;
    }

    RandomWander(diff);
    RandomEmote(diff);
    RandomChat(diff);
}

// ---------------------------------------------------------------- targeting

void PlayerbotAI::DoFindTarget()
{
    // простая версия: ближайшая враждебная цель в агрессии к боту или к мастеру-выбору
    Unit* victim = _bot->GetSelectedUnit();
    if (victim && victim->IsAlive() && _bot->IsValidAttackTarget(victim))
    {
        m_combatTarget = victim;
        return;
    }

    victim = _bot->GetVictim();
    if (victim && victim->IsAlive())
    {
        m_combatTarget = victim;
        return;
    }

    m_combatTarget = nullptr;
}

void PlayerbotAI::TargetSelectionIfNeeded()
{
    Unit* t = m_combatTarget;
    if (!t || !t->IsAlive() || !_bot->IsValidAttackTarget(t))
        m_combatTarget = nullptr;
}

// ---------------------------------------------------------------- combat

void PlayerbotAI::DoCombatAI(uint32 /*diff*/)
{
    TargetSelectionIfNeeded();
    if (!m_combatTarget)
    {
        DoFindTarget();
        if (!m_combatTarget)
            return;
    }

    Unit* target = m_combatTarget;
    float dist = _bot->GetDistance(target);

    bool const isMeleeGesture = (GetMaxRange() <= 6.0f);

    if (dist > GetMaxRange())
    {
        // подходим: chase к melee или ranged-точке (v0 — chase к melee всегда,
        // ranged-позиционирование в v1)
        _bot->GetMotionMaster()->Clear();
        _bot->GetMotionMaster()->MoveChase(target);
        if (isMeleeGesture)
            _bot->Attack(target, true);
        return;
    }

    // в досягаемости — чутьё stop-move (плане)
    if (_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        _bot->GetMotionMaster()->MoveIdle();

    if (isMeleeGesture && !_bot->GetVictim())
        _bot->Attack(target, true);

    // idle-каст по приоритету
    if (m_recastTimerMs == 0)
    {
        for (uint32 sid : m_combatSpells)
        {
            if (!IsSpellReady(sid))
                continue;
            if (CastSpellAt(sid))
            {
                m_recastTimerMs = 2000;   // anti-spam v0 (в v1: cd/recovery по спеллу)
                break;
            }
        }
    }
}

bool PlayerbotAI::IsSpellReady(uint32 spellId) const
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;
    return _bot->GetSpellHistory()->IsReady(info);
}

bool PlayerbotAI::CastSpellAt(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;

    Unit* target = m_combatTarget ? m_combatTarget : _bot->GetVictim();
    if (!target)
        return false;

    _bot->CastSpell(CastSpellTargetArg(target), spellId, CastSpellExtraArgs(TRIGGERED_NONE));
    return true;
}

// ---------------------------------------------------------------- plane

void PlayerbotAI::RandomWander(uint32 diff)
{
    if (m_planeWanderTimer > diff) { m_planeWanderTimer -= diff; return; }
    m_planeWanderTimer = urand(10000, 30000);

    if (!_bot->IsInWorld() || !_bot->IsAlive())
        return;
    if (_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
        return;

    float x = _bot->GetPositionX() + frand(-10.0f, 10.0f);
    float y = _bot->GetPositionY() + frand(-10.0f, 10.0f);
    float z = _bot->GetPositionZ();          // v0: без terrain-raycasting

    _bot->GetMotionMaster()->MovePoint(1, x, y, z, false);
}

void PlayerbotAI::RandomEmote(uint32 diff)
{
    if (m_planeEmoteTimer > diff) { m_planeEmoteTimer -= diff; return; }
    m_planeEmoteTimer = urand(10000, 40000);

    static std::vector<Emote> const planeEmotes = { EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_WAVE };
    _bot->HandleEmoteCommand(planeEmotes[rand32() % planeEmotes.size()]);
}

void PlayerbotAI::RandomChat(uint32 diff)
{
    if (m_planeChatTimer > diff) { m_planeChatTimer -= diff; return; }
    m_planeChatTimer = urand(30000, 90000);

    static std::vector<std::string> const planeQuotes = {
        "привет!", "как дела?", "пора в бой.", "готов.",
    };
    _bot->Say(planeQuotes[rand32() % planeQuotes.size()], LANG_UNIVERSAL, _bot);
}

// ---------------------------------------------------------------- game interface

void PlayerbotAI::PingMaster()
{
    _bot->GetMotionMaster()->Clear();
    _bot->m_movementInfo.pos.Relocate(_bot->GetPosition());

    WorldPackets::Movement::MoveUpdate moveUpdate;
    moveUpdate.Status = &_bot->m_movementInfo;
    _bot->SendMessageToSet(moveUpdate.Write(), false);
}

void PlayerbotAI::FollowStop()
{
    _bot->GetMotionMaster()->Clear();
    _bot->GetMotionMaster()->MoveIdle();
}

void PlayerbotAI::BotSay(std::string const& msg)
{
    if (!msg.empty())
        _bot->Say(msg, LANG_UNIVERSAL, _bot);
}

void PlayerbotAI::EmoteMe(uint32 emote)
{
    _bot->HandleEmoteCommand(static_cast<Emote>(emote));
}
