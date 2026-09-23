/*
 * PLAYERBOTS под TrinityCore master — v2
 * Плавное движение: сплайн-путь на сервере (MovePoint) + штатная рассылка
 * SMSG_MONSTER_MOVE наблюдателям, которую делает MoveSplineInit::Launch()
 * сам ядро — благодаря этому никакой инжекции пакетов руками не требуется.
 * Follow: пере-указываем конечную точку (re-point) по мастеру с каденсом,
 * анти-спам по мастерской позиции — получается «мягкий» шаг за игроком.
 */
#include "PlayerbotAI.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementPackets.h"
#include "ObjectAccessor.h"
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
        default:            return 5.0f;
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

    if (_followRepointMs > diff)
        _followRepointMs -= diff;
    else
        _followRepointMs = 0;

    Player* master = _masterGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(_masterGuid);

    // COMBAT > FOLLOW > PLANE
    if (_bot->IsInCombat() || (_bot->GetSelectedUnit() && _bot->GetSelectedUnit()->IsInCombat())
        || (master && master->IsInCombat()))
    {
        DoCombatAI(diff);
        return;
    }

    if (_followEnabled && master && _bot->GetMapId() == master->GetMapId())
    {
        HandleFollowTick(diff);
        return;
    }

    HandlePlaneTick(diff);
}

// ---------------------------------------------------------------- targeting

Unit* PlayerbotAI::FindProtectTarget()
{
    if (_masterGuid.IsEmpty())
        return nullptr;

    Player* master = ObjectAccessor::FindPlayer(_masterGuid);
    if (!master || master->GetMapId() != _bot->GetMapId())
        return nullptr;

    // сначала — атакующие мастера
    for (Unit* attacker : master->getAttackers())
        if (attacker->IsAlive() && _bot->IsValidAttackTarget(attacker))
            return attacker;

    return nullptr;
}

void PlayerbotAI::DoFindTarget()
{
    Unit* victim = _bot->GetSelectedUnit();
    if (victim && victim->IsAlive() && _bot->IsValidAttackTarget(victim) && victim->IsHostileTo(_bot))
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

    Unit* protectTarget = FindProtectTarget();
    if (protectTarget)
    {
        m_combatTarget = protectTarget;
        _bot->AttackerStateUpdate(protectTarget);
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
    bool const wantsMelee = (GetDefaultCombatRange(_bot->GetClass()) <= 6.0f);

    if (dist > m_combatRange)
    {
        // догнать: тот же сплайн-механизм, наблюдатели видят шаг
        _bot->GetMotionMaster()->Clear();
        _bot->GetMotionMaster()->MoveFollow(target, GetMeleeDistance());
        if (wantsMelee)
            _bot->Attack(target, true);
        return;
    }

    if (_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        _bot->GetMotionMaster()->MoveIdle();

    if (wantsMelee && !_bot->GetVictim() && m_recastTimerMs == 0)
        _bot->Attack(target, true);

    if (m_recastTimerMs == 0)
    {
        for (uint32 sid : m_combatSpells)
        {
            if (!IsSpellReady(sid))
                continue;
            if (CastSpellAt(sid))
            {
                m_recastTimerMs = 2000;   // v2 антиспам; v3 — GCD по спеллу
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

bool PlayerbotAI::IsSelfBuff(SpellInfo const* info) const
{
    if (info->GetMaxRange(true, _bot) > 0.0f)
        return false;
    return info->IsPositive();
}

bool PlayerbotAI::CastSpellAt(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;

    if (IsSelfBuff(info))
    {
        _bot->CastSpell(CastSpellTargetArg(_bot), spellId, CastSpellExtraArgs(TRIGGERED_NONE));
        return true;
    }

    Unit* target = m_combatTarget ? m_combatTarget : _bot->GetVictim();
    if (!target)
        return false;

    float maxR = info->GetMaxRange(false, _bot);
    if (maxR > 0.0f && _bot->GetDistance(target) > maxR * 1.05f)
        return false;

    _bot->CastSpell(CastSpellTargetArg(target), spellId, CastSpellExtraArgs(TRIGGERED_NONE));
    return true;
}

// ---------------------------------------------------------------- follow

void PlayerbotAI::SetMasterAndFollow(Player* master)
{
    _masterGuid = master ? master->GetGUID() : ObjectGuid::Empty;
    _followEnabled = master != nullptr;
    _followRepointMs = 0;
    _lastFollowX = _lastFollowY = 0.0f;
}

void PlayerbotAI::ClearFollow()
{
    _followEnabled = false;
    _bot->GetMotionMaster()->MoveIdle();
}

void PlayerbotAI::HandleFollowTick(uint32 diff)
{
    Player* master = ObjectAccessor::FindPlayer(_masterGuid);
    if (!master)
    {
        ClearFollow();
        return;
    }
    (void)diff;

    float dist = _bot->GetDistance(master);
    if (dist < _followDist)
    {
        if (_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE
            || _bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            _bot->GetMotionMaster()->MoveIdle();
        return;
    }

    if (_followRepointMs != 0)
        return;
    _followRepointMs = 400;                       // каденс — 400 мс

    // точка зависает позади мастера (по его же ориентации)
    float ang = master->GetOrientation();
    float tx  = master->GetPositionX() - std::cos(ang) * _followDist;
    float ty  = master->GetPositionY() - std::sin(ang) * _followDist;
    float tz  = master->GetPositionZ();

    // анти-спам: если мастер не сдвинулся по существу — не перезапускать сплайн
    float ddx = tx - _lastFollowX, ddy = ty - _lastFollowY;
    if (ddx * ddx + ddy * ddy < 1.0f)              // <1 метр — старое поведение держать
        return;

    _lastFollowX = tx;
    _lastFollowY = ty;

    _bot->GetMotionMaster()->Clear();
    _bot->GetMotionMaster()->MovePoint(2, tx, ty, tz, true); // generatePath=true
}

// ---------------------------------------------------------------- plane

void PlayerbotAI::HandlePlaneTick(uint32 diff)
{
    RandomWander(diff);
    RandomEmote(diff);
    RandomChat(diff);
}

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

    _bot->GetMotionMaster()->MovePoint(1, x, y, z, true);
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

void PlayerbotAI::BotSay(std::string const& msg)
{
    if (!msg.empty())
        _bot->Say(msg, LANG_UNIVERSAL, _bot);
}

void PlayerbotAI::EmoteMe(uint32 emote)
{
    _bot->HandleEmoteCommand(static_cast<Emote>(emote));
}
