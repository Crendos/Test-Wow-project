/*
 * PLAYERBOTS под TrinityCore master — v3 (классовое знание)
 * Знание спелов: ручная БД (имя/класс) ИЛИ авто-построение из spellbookа бота.
 * Правила «когда что кастовать»: defensive-first → heal → damage, range/gates/ready.
 */
#include "PlayerbotMgr.h"   // BotKnowledge живёт в Knowledge.h через mgr-цепочку
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
#include <algorithm>

PlayerbotAI::PlayerbotAI(Player* bot, std::vector<BotKnowledge> knowledge)
    : _bot(bot)
    , m_knowledge(std::move(knowledge))
{
    m_combatRange = GetDefaultCombatRange(_bot->GetClass());

    if (m_knowledge.empty())
    {
        BuildKnowledgeFromSpellbook();
        TC_LOG_INFO("playerbots", "AI {}: знание построено из спелбукка — {} правил", _bot->GetName(), m_knowledge.size());
    }

    std::sort(m_knowledge.begin(), m_knowledge.end(),
        [](BotKnowledge const& a, BotKnowledge const& b) { return a.priority > b.priority; });

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
        case CLASS_DRUID:   return 25.0f;
        case CLASS_PALADIN: return 10.0f;
        default:            return 5.0f;
    }
}

// ---------------------------------------------------------------- v3: зна niya

// Классификатор «что это за спелл» по эффектам — честный, без таблиц имён.
void PlayerbotAI::BuildKnowledgeFromSpellbook()
{
    for (auto const& kv : _bot->GetSpellMap())
    {
        PlayerSpell const& st = kv.second;
        if (st.state == PLAYERSPELL_REMOVED)
            continue;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(kv.first, DIFFICULTY_NONE);
        if (!info || info->IsPassive())
            continue;

        // исключаем маунты и прочую администратинру: любой APPLY_AURA с MOUNT — пропускаем
        bool isMount = false, isHeal = false, isDamage = false, isInterrupt = false, isAura = false;
        for (SpellEffectInfo const& eff : info->GetEffects())
        {
            if (eff.Effect == SPELL_EFFECT_INTERRUPT_CAST)
                isInterrupt = true;
            if (eff.Effect == SPELL_EFFECT_HEAL)
                isHeal = true;
            if (eff.Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
                isDamage = true;
            if (eff.Effect == SPELL_EFFECT_APPLY_AURA)
            {
                isAura = true;
                if (eff.ApplyAuraName == SPELL_AURA_MOUNTED)
                    isMount = true;
            }
        }
        (void)isMount;

        if (isInterrupt)
        {
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Interrupt, 60, 100, 0, 100, false});
            continue;
        }
        if (isHeal && info->IsPositive())
        {
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Heal, 80, 60, 0, 100, false});
            continue;
        }
        if (isAura && info->IsPositive() && info->GetMaxRange(true, _bot) <= 0.0f)
        {
            uint16 priority   = info->RecoveryTime >= 60000 ? 95 : 40;
            uint8  selfHpMax  = info->RecoveryTime >= 60000 ? 35 : 100;
            BotKnowledge::Kind kind = info->RecoveryTime >= 60000
                ? BotKnowledge::Kind::Defensive : BotKnowledge::Kind::SelfBuff;
            m_knowledge.push_back(BotKnowledge{kv.first, kind, priority, selfHpMax, 0, 100, true});
            continue;
        }
        if (isDamage && !info->IsPositive())
        {
            // короткий CD = фирменный «выстук» класса → чуть выше по приоритету
            uint16 bonus = uint16(std::min<uint32>(info->RecoveryTime, 30000u) / 1000u);
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Damage, uint16(100 + bonus), 100, 0, 100, false});
            continue;
        }

        // дефолт: положительный ООC-self — брать не будем (неизвестный EFFект)
    }

    // fallback: ничего боевого не оказалось — импровизация melee-only
    if (std::none_of(m_knowledge.begin(), m_knowledge.end(),
        [](BotKnowledge const& k){ return k.kind == BotKnowledge::Kind::Damage; }))
        TC_LOG_WARN("playerbots", "AI {}: боевых спелов в книге не найдено — melee-only", _bot->GetName());
}

bool PlayerbotAI::IsSelfBuffSpell(SpellInfo const* info) const
{
    if (info->GetMaxRange(true, _bot) > 0.0f)
        return false;
    return info->IsPositive();
}

bool PlayerbotAI::IsSpellReady(uint32 spellId) const
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;
    return _bot->GetSpellHistory()->IsReady(info);
}

bool PlayerbotAI::CastSpellAt(uint32 spellId, Unit* target)
{
    if (!target)
        target = _bot;
    _bot->CastSpell(CastSpellTargetArg(target), spellId, CastSpellExtraArgs(TRIGGERED_NONE));
    return true;
}

// gates: range + selfHpMax + targetHp-range
bool PlayerbotAI::SpellFits(BotKnowledge const& k, Unit* target) const
{
    if (!IsSpellReady(k.spellId))
        return false;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(k.spellId, DIFFICULTY_NONE);
    if (!info)
        return false;

    float selfHpPct = _bot->GetHealthPct();
    if (k.selfHpMax < 100 && selfHpPct > float(k.selfHpMax))
        return false;

    if (k.kind == BotKnowledge::Kind::Damage || k.kind == BotKnowledge::Kind::Heal)
    {
        Unit* focus = target ? target : _bot;
        if (!focus)
            return false;

        float maxR = info->GetMaxRange(k.kind == BotKnowledge::Kind::Heal || IsSelfBuffSpell(info), _bot);
        if (maxR > 0.0f && _bot->GetDistance(focus) > maxR * 1.05f)
            return false;

        float targetPct = focus->GetHealthPct();
        if (float(k.targetHpMin) > targetPct || float(k.targetHpMax) < targetPct)
            return false;
    }
    return true;
}

// поддержка self-бафов (только в пакете, когда спелл long-term: молчанка maintain)
void PlayerbotAI::EnsureSelfBuffs()
{
    if (_bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr)
        return;

    for (BotKnowledge const& k : m_knowledge)
    {
        if (k.kind != BotKnowledge::Kind::SelfBuff)
            continue;
        if (_bot->HasAura(k.spellId))
            continue;
        if (!IsSpellReady(k.spellId))
            continue;

        if (CastSpellAt(k.spellId, _bot))
        {
            m_recastTimerMs = 1500;
            break; // одно действие за тик
        }
    }
}

// большая защита (Divine shield, Ice Block, etc.) — при малом HP
bool PlayerbotAI::TryDefensive()
{
    for (BotKnowledge const& k : m_knowledge)
    {
        if (k.kind != BotKnowledge::Kind::Defensive)
            continue;
        if (float(_bot->GetHealthPct()) > float(k.selfHpMax))
            continue;
        if (!IsSpellReady(k.spellId))
            continue;
        return CastSpellAt(k.spellId, _bot);
    }
    return false;
}

// лечение себя (и мастера в группе, если бот в парте)
bool PlayerbotAI::TryHeal()
{
    Player* master = _masterGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(_masterGuid);

    for (BotKnowledge const& k : m_knowledge)
    {
        if (k.kind != BotKnowledge::Kind::Heal)
            continue;
        if (!IsSpellReady(k.spellId))
            continue;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(k.spellId, DIFFICULTY_NONE);
        if (!info)
            continue;

        float maxR = info->GetMaxRange(true, _bot);

        // себя — 60% порог
        if (_bot->GetHealthPct() < 60.f && (maxR <= 0.f || _bot->GetDistance(_bot) <= maxR))
            return CastSpellAt(k.spellId, _bot);

        // мастер — 50% порог (группа совместна)
        if (master && master->GetGroup() == _bot->GetGroup() && master->GetGroup() != nullptr
            && master->GetHealthPct() < 50.f
            && _bot->GetMapId() == master->GetMapId()
            && (maxR <= 0.f || _bot->GetDistance(master) <= maxR * 1.05f))
            return CastSpellAt(k.spellId, master);
    }
    return false;
}

// основной урон-цикл
bool PlayerbotAI::TryAttackSpell()
{
    Unit* target = m_combatTarget;
    if (!target)
        return false;

    for (BotKnowledge const& k : m_knowledge)
    {
        if (k.kind != BotKnowledge::Kind::Damage)
            continue;
        if (!SpellFits(k, target))
            continue;
        return CastSpellAt(k.spellId, target);
    }
    return false;
}

std::vector<uint32> PlayerbotAI::ListKnownSpelIDs() const
{
    std::vector<uint32> out;
    out.reserve(m_knowledge.size());
    for (BotKnowledge const& k : m_knowledge)
        out.push_back(k.spellId);
    return out;
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

    EnsureSelfBuffs();

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

    // defensive > heal > positioning > damage
    if (TryDefensive() || TryHeal())
    {
        m_recastTimerMs = 1500;
        return;
    }

    if (dist > m_combatRange)
    {
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

    if (m_recastTimerMs == 0 && !_bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
    {
        EnsureSelfBuffs(); // м.б. поддержка ауры в бою
        if (m_recastTimerMs != 0)
            return;
        if (TryAttackSpell())
            m_recastTimerMs = 1500;   // v3 GCD-подобный; v4 — реальный GCD по спеллу
    }
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
        MovementGeneratorType mgt = _bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (mgt == CHASE_MOTION_TYPE || mgt == POINT_MOTION_TYPE)
            _bot->GetMotionMaster()->MoveIdle();
        return;
    }

    if (_followRepointMs != 0)
        return;
    _followRepointMs = 400;

    float ang = master->GetOrientation();
    float tx  = master->GetPositionX() - std::cos(ang) * _followDist;
    float ty  = master->GetPositionY() - std::sin(ang) * _followDist;
    float tz  = master->GetPositionZ();

    float ddx = tx - _lastFollowX, ddy = ty - _lastFollowY;
    if (ddx * ddx + ddy * ddy < 1.0f)
        return;

    _lastFollowX = tx;
    _lastFollowY = ty;

    _bot->GetMotionMaster()->Clear();
    _bot->GetMotionMaster()->MovePoint(2, tx, ty, tz, true);
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
    float z = _bot->GetPositionZ();

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
