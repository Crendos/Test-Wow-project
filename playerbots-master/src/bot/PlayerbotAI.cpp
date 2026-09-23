/*
 * PLAYERBOTS под TrinityCore master — v4
 * Активная ротация: GCD≈StartRecoveryTime (мин. 1.5с), DoT-управление по аурам,
 * Defensive>Heal>Damage по приоритетам и «сжатие» кулдаунов в окне старта боя,
 * черный список заброшенных спеллов на время боя.
 * Авто-одевание из сумок: баланс itemLevel по классу (броня по subclass).
 */
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "Creature.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>

PlayerbotAI::PlayerbotAI(Player* bot, std::vector<BotKnowledge> knowledge)
    : _bot(bot)
    , m_knowledge(std::move(knowledge))
{
    m_combatRange = GetDefaultCombatRange(_bot->GetClass());
    _manualKnowledge = !m_knowledge.empty();

    if (!_manualKnowledge)
    {
        BuildKnowledgeFromSpellbook();
        TC_LOG_INFO("playerbots", "AI {}: знание построено из спелбукка — {} правил", _bot->GetName(), m_knowledge.size());
    }

    std::sort(m_knowledge.begin(), m_knowledge.end(),
        [](BotKnowledge const& a, BotKnowledge const& b) { return a.priority > b.priority; });

    // одеть лучшее из сумок при логине
    EquipBestItems();

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

/*static*/ int PlayerbotAI::ClassArmorSubClass(uint8 cls)
{
    // RETAIL-легализация: максимальный доступный по классу armor-subclass
    switch (cls)
    {
        case 1:  return 4;   // Warrior  → пластина
        case 2:  return 4;   // Paladin  → пластина
        case 6:  return 4;   // DK       → пластина
        case 3:  return 3;   // Hunter   → кольчуга
        case 7:  return 3;   // Shaman   → кольчуга
        case 4:  return 2;   // Rogue    → кожа
        case 10: return 2;   // Monk     → кожа
        case 11: return 2;   // Druid    → кожа
        case 12: return 2;   // DH       → кожа
        default: return 1;   // Mage/Priest/Warlock/Evoker → ткань
    }
}

// ---------------------------------------------------------------- v4: авто-знание

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

        bool isHeal=false, isDmg=false, isInterrupt=false, isAura=false, isDoT=false, isMount=false, isDispel=false;
        for (SpellEffectInfo const& eff : info->GetEffects())
        {
            if (eff.Effect == SPELL_EFFECT_INTERRUPT_CAST) isInterrupt = true;
            if (eff.Effect == SPELL_EFFECT_HEAL)            isHeal = true;
            if (eff.Effect == SPELL_EFFECT_SCHOOL_DAMAGE)   isDmg = true;
            if (eff.Effect == SPELL_EFFECT_DISPEL)          isDispel = true;
            if (eff.Effect == SPELL_EFFECT_APPLY_AURA)
            {
                isAura = true;
                if (eff.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE
                    || eff.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH)
                    isDoT = true;
                if (eff.ApplyAuraName == SPELL_AURA_MOUNTED)
                    isMount = true;
            }
        }

        if (isMount)
            continue;   // маунты в знание не берём

        if (isInterrupt)
        {
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Interrupt, 60, 100, 0, 100, false, false});
        }
        else if (isHeal && info->IsPositive())
        {
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Heal, 80, 60, 0, 100, false, false});
        }
        else if (isDispel && info->IsPositive())
        {
            // v5: диспел/очищение — для правил dispel_self (и нормальных слепых диспелов мимо боёв боссов)
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Dispel, 85, 100, 0, 100, false, false});
        }
        else if (isDoT && !info->IsPositive())
        {
            // DoT/дебафф урона: поддерживаем на цели (cast до выхода из ауры)
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::DoT, 130, 100, 0, 100, true, false});
        }
        else if (isAura && info->IsPositive() && info->GetMaxRange(true, _bot) <= 0.0f)
        {
            bool bigDef = info->RecoveryTime >= 60000u;
            m_knowledge.push_back(BotKnowledge{kv.first,
                bigDef ? BotKnowledge::Kind::Defensive : BotKnowledge::Kind::SelfBuff,
                bigDef ? uint16(500): uint16(40),
                bigDef ? uint8(35)  : uint8(100),
                0, 100, true, false});
        }
        else if (isDmg && !info->IsPositive())
        {
            bool burst = info->RecoveryTime >= 60000;
            uint16 bonus = uint16(std::min<uint32>(info->RecoveryTime, 30000u) / 1000u);
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Damage,
                uint16(100 + bonus + (burst ? 400 : 0)), 100, 0, 100, false, burst});
        }
        else if (!info->IsPositive() && info->GetMaxRange(false, _bot) > 0.0f)
        {
            // дебаффы без урона — тоже поддерживаем (maintain по ауре цели)
            m_knowledge.push_back(BotKnowledge{kv.first, BotKnowledge::Kind::Debuff,
                uint16(90), 100, 0, 100, true, false});
        }
    }   // конец for (auto const& kv : _bot->GetSpellMap())

    if (std::none_of(m_knowledge.begin(), m_knowledge.end(),
        [](BotKnowledge const& k){ return k.kind == BotKnowledge::Kind::Damage; }))
        TC_LOG_WARN("playerbots", "AI {}: боевых спелов в книге не найдено — melee-only", _bot->GetName());
}

bool PlayerbotAI::IsSpellReady(uint32 spellId) const
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;
    if (m_castBlacklist.count(spellId))
        return false;
    return _bot->GetSpellHistory()->IsReady(info);
}

bool PlayerbotAI::SpellFits(BotKnowledge const& k, Unit* target) const
{
    if (!IsSpellReady(k.spellId))
        return false;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(k.spellId, DIFFICULTY_NONE);
    if (!info)
        return false;

    if (k.selfHpMax < 100 && float(_bot->GetHealthPct()) > float(k.selfHpMax))
        return false;

    // цель-гейт
    if (target)
    {
        float targetPct = target->GetHealthPct();
        if (float(k.targetHpMin) > targetPct || float(k.targetHpMax) < targetPct)
            return false;

        // DoT-управление: не повторяем, если уже обслуживаем цель
        if (k.maintainAura && (k.kind == BotKnowledge::Kind::DoT || k.kind == BotKnowledge::Kind::Debuff)
            && target->HasAura(k.spellId, _bot->GetGUID()))
            return false;

        // range gate
        float maxR = info->GetMaxRange(info->IsPositive(), _bot);
        if (maxR > 0.0f && _bot->GetDistance(target) > maxR * 1.05f)
            return false;
    }

    // burst-окно: в начале боя (8 сек), при цели <30% HP или принудительно правилом босса (use_burst)
    if (k.burst)
    {
        bool window = _combatActive && (m_forceBurstMs > 0 || (getMSTime() - m_combatEnterMs) < 8000u);
        bool lowHp  = target && target->GetHealthPct() < 30.f;
        if (!window && !lowHp)
            return false;
    }

    return true;
}

bool PlayerbotAI::CastSpellAt(uint32 spellId, Unit* target)
{
    if (!target)
        target = _bot;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;

    // каст-тайм: требует стояния. Если идём — остановимся
    if (info->CalcCastTime() > 0)
    {
        MovementGeneratorType mgt = _bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (mgt == CHASE_MOTION_TYPE || mgt == POINT_MOTION_TYPE)
            _bot->GetMotionMaster()->MoveIdle();
    }

    _bot->CastSpell(CastSpellTargetArg(target), spellId, CastSpellExtraArgs(TRIGGERED_NONE));

    // v3 GCD-подобный интервал (на master: StartRecoveryTime — это фактический GCD-флажок)
    uint32 base = std::max<uint32>(info->StartRecoveryTime, 1500u);
    m_recastTimerMs = std::max<uint32>(base, 1000u);
    return true;
}

void PlayerbotAI::RegisterCastFail(uint32 /*spellId*/)
{
    // CastSpell сейчас void-report; в v5 сделаем вывод по GetCastSpellInfo->SpellCastResult
    // и оставим эту функцию как готовый хук: m_castBlacklist.insert(spellId)
}

void PlayerbotAI::ClearCastBlacklist()
{
    if (!m_castBlacklist.empty())
        m_castBlacklist.clear();
}

void PlayerbotAI::NotifyCombatEnter()
{
    m_combatEnterMs = getMSTime();
    _combatActive = true;
}

// ---------------------------------------------------------------- ticks

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
            break;
    }
}

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

        if (_bot->GetHealthPct() < 60.f && (maxR <= 0.f || _bot->GetDistance(_bot) <= maxR))
            return CastSpellAt(k.spellId, _bot);

        if (master && master->GetGroup() && master->GetGroup() == _bot->GetGroup()
            && master->GetHealthPct() < 50.f
            && _bot->GetMapId() == master->GetMapId()
            && (maxR <= 0.f || _bot->GetDistance(master) <= maxR * 1.05f))
            return CastSpellAt(k.spellId, master);
    }
    return false;
}

bool PlayerbotAI::TryAttackSpell()
{
    Unit* target = m_combatTarget;
    if (!target)
        return false;

    for (BotKnowledge const& k : m_knowledge)
    {
        bool offensive = (k.kind == BotKnowledge::Kind::Damage
                          || k.kind == BotKnowledge::Kind::DoT
                          || k.kind == BotKnowledge::Kind::Debuff);
        if (!offensive)
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

void PlayerbotAI::Update(uint32 diff)
{
    if (!_bot->IsInWorld() || _bot->IsBeingTeleported())
        return;

    if (m_recastTimerMs > diff)  m_recastTimerMs -= diff;  else m_recastTimerMs = 0;
    if (_followRepointMs > diff) _followRepointMs -= diff; else _followRepointMs = 0;
    if (m_ruleMoveBlockMs > diff) m_ruleMoveBlockMs -= diff; else m_ruleMoveBlockMs = 0;
    if (m_forceBurstMs > diff)    m_forceBurstMs    -= diff; else m_forceBurstMs    = 0;
    for (auto it = m_ruleCooldowns.begin(); it != m_ruleCooldowns.end();)
    {
        if (it->second > diff) { it->second -= diff; ++it; }
        else it = m_ruleCooldowns.erase(it);
    }

    Player* master = _masterGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(_masterGuid);

    bool inCombatNew = _bot->IsInCombat()
        || (_bot->GetSelectedUnit() && _bot->GetSelectedUnit()->IsInCombat())
        || (master && master->IsInCombat());

    if (inCombatNew && !_combatActive)
        NotifyCombatEnter();
    if (!inCombatNew && _combatActive)
    {
        _combatActive = false;
        ClearCastBlacklist();
        m_bossEntry = 0;
        m_ruleCooldowns.clear();
    }

    if (inCombatNew)
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
    if (Unit* prot = FindProtectTarget())
    {
        m_combatTarget = prot;
        _bot->AttackerStateUpdate(prot);
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

    // Defensive > Heal > позиционирование > Damage/DoT/Debuff
    if (TryDefensive() || TryHeal())
        return;

    // v5: босс-механики — до обычной ротации и позиционирования.
    // Пока бот двигается по правилу (m_ruleMoveBlockMs) — тик свободен, но позицию не трогаем.
    if (ProcessBossRules(target) || m_ruleMoveBlockMs > 0)
        return;

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
        EnsureSelfBuffs();
        if (m_recastTimerMs != 0)
            return;
        if (TryAttackSpell())
            m_recastTimerMs = std::max<uint32>(m_recastTimerMs, 1500u);
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

// ---------------------------------------------------------------- equip/scoring

int PlayerbotAI::SlotForInventoryType(int32 invType)
{
    switch (invType)
    {
        case 1:  return 0;   // HEAD
        case 2:  return 1;   // NECK
        case 3:  return 2;   // SHOULDER
        case 4:  return 3;   // BODY (рубашка)
        case 5:  return 4;   // CHEST
        case 6:  return 5;   // WAIST
        case 7:  return 6;   // LEGS
        case 8:  return 7;   // FEET
        case 9:  return 8;   // WRIST
        case 10: return 9;   // HANDS
        case 11: case 12: return 10; // FINGER → первый палец
        case 13: case 14: return 12; // TRINKET
        case 15: return 14;  // BACK
        case 16: case 17: case 21: case 22: return 15; // 1H/2H/MAINHAND/WEAPONMAINHAND → MAINHAND
        case 18: case 23: return 16;  // OFFHAND → OFFHAND
        case 26: return 17;  // RANGED → RANGED
        case 19: return 18;  // TABARD
        default: return -1;
    }
}

int PlayerbotAI::ScoreItem(Item const* item) const
{
    if (!item)
        return -1;
    auto* t = item->GetTemplate();
    return int(t->GetBaseItemLevel()) * 10 + int(t->GetQuality());
}

bool PlayerbotAI::ItemFitsClass(Item const* item, int slot) const
{
    if (!item)
        return false;
    auto* t = item->GetTemplate();
    // Фильтр брони: допустиммо по классу (plate→4, mail→3, leather→2, cloth→1)
    bool isArmorSlot = (slot == 0 || slot == 2 || slot == 4 || slot == 5 || slot == 6 || slot == 7 || slot == 8 || slot == 9 || slot == 14);
    if (isArmorSlot && t->GetSubClass() > 0)   // 0 = misc/без брони (кольца, тринкеты и т.п.)
    {
        int pref = ClassArmorSubClass(_bot->GetClass());
        if (int(t->GetSubClass()) > pref)
            return false;
    }
    return true;
}

void PlayerbotAI::EquipBestItems()
{
    // Слоты, которые попробуем закрыть из сумки rucksack-bag0
    static int const armorSlots[] = { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16 };

    for (int trial = 0; trial < 8; ++trial)       // до 8 замещений за раз — безопасно
    {
        bool swapped = false;
        for (int slot : armorSlots)
        {
            Item* bestItem = nullptr;
            int  bestScore = -1;
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            {
                Item* item = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
                if (!item)
                    continue;
                auto* t = item->GetTemplate();
                int candSlot = SlotForInventoryType(int32(t->GetInventoryType()));
                if (candSlot != slot)
                    continue;
                if (!ItemFitsClass(item, slot))
                    continue;
                int score = ScoreItem(item);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestItem = item;
                }
            }
            if (!bestItem)
                continue;

            Item* current = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(slot));
            if (bestScore <= ScoreItem(current) + 5)
                continue;

            // 2H в MAINHAND — не рушим, если занят OFFHAND (sanity с swap)
            _bot->SwapItem(bestItem->GetPos(), uint16(INVENTORY_SLOT_BAG_0 << 8 | slot));
            swapped = true;
        }
        if (!swapped)
            break;
    }
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

// ---------------------------------------------------------------- v5: босс-механики

void PlayerbotAI::RuleMoveTo(float x, float y, float z, uint32 blockMs)
{
    _bot->GetMotionMaster()->Clear();
    _bot->GetMotionMaster()->MovePoint(0, x, y, z);
    m_ruleMoveBlockMs = blockMs;
}

bool PlayerbotAI::EvaluateBossTrigger(BossRule const& r, Unit* boss) const
{
    switch (r.TriggerType)
    {
        case BossRule::Trigger::BossCast:
        {
            Spell const* s = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return s && s->m_spellInfo && s->m_spellInfo->Id == r.TriggerArg;
        }
        case BossRule::Trigger::BossAura:
            return boss->HasAura(r.TriggerArg);
        case BossRule::Trigger::BotAura:
            return _bot->HasAura(r.TriggerArg);
        case BossRule::Trigger::BossHpBelow:
            return boss->GetHealthPct() < float(r.TriggerArg);
        case BossRule::Trigger::Always:
            return true;
    }
    return false;
}

// true = действие съело GCD/тик; false = либо не смогли, либо исполнили без GCD
// (движение/переключение/burst-окно — это различает ProcessBossRules по маркерам).
bool PlayerbotAI::ExecuteBossAction(BossRule const& r, Unit* boss)
{
    switch (r.ActionType)
    {
        case BossRule::Action::Interrupt:
            for (BotKnowledge const& k : m_knowledge)
            {
                if (k.kind != BotKnowledge::Kind::Interrupt || !SpellFits(k, boss))
                    continue;
                CastSpellAt(k.spellId, boss);
                return true;
            }
            return false;

        case BossRule::Action::RunFromBoss:
        {
            float dist = float(r.ActionArg ? r.ActionArg : 12u);
            float ang  = boss->GetAbsoluteAngle(_bot);
            RuleMoveTo(_bot->GetPositionX() + std::cos(ang) * dist,
                       _bot->GetPositionY() + std::sin(ang) * dist,
                       _bot->GetPositionZ(), 1200u);
            return false;   // движение — GCD свободен, дальше по списку
        }

        case BossRule::Action::Spread:
        {
            float dist = float(r.ActionArg ? r.ActionArg : 8u);
            Player* ally = _bot->SelectNearestPlayer(dist);
            if (!ally || ally == _bot)
                return false;   // никто рядом — уже не толпимся
            float ang = ally->GetAbsoluteAngle(_bot);
            RuleMoveTo(_bot->GetPositionX() + std::cos(ang) * dist,
                       _bot->GetPositionY() + std::sin(ang) * dist,
                       _bot->GetPositionZ(), 1200u);
            return false;
        }

        case BossRule::Action::Sidestep:
        {
            float dist = float(r.ActionArg ? r.ActionArg : 8u);
            float ang  = frand(0.0f, 6.2831853f);
            RuleMoveTo(_bot->GetPositionX() + std::cos(ang) * dist,
                       _bot->GetPositionY() + std::sin(ang) * dist,
                       _bot->GetPositionZ(), 1000u);
            return false;
        }

        case BossRule::Action::SwitchTarget:
        {
            Creature* c = _bot->FindNearestCreature(r.ActionArg, 80.0f, true);
            if (!c || !_bot->IsValidAttackTarget(c))
                return false;   // цели нет/она friendly (Nibbles до превращения) — правило молчит
            if (m_combatTarget != c)
            {
                m_combatTarget = c;
                _bot->Attack(c, true);
            }
            return false;
        }

        case BossRule::Action::UseDefensive:
            if (TryDefensive())
                return true;
            return false;

        case BossRule::Action::DispelSelf:
            for (BotKnowledge const& k : m_knowledge)
            {
                if (k.kind != BotKnowledge::Kind::Dispel || !SpellFits(k, _bot))
                    continue;
                CastSpellAt(k.spellId, _bot);
                return true;
            }
            return false;

        case BossRule::Action::UseBurst:
            m_forceBurstMs = 5000;
            return false;
    }
    return false;
}

// Отдельная схема «не смогли исполнить» vs «исполнили без GCD»:
// - ExecuteBossAction false + движение поставлено → это "result 2".
// Кодируем: если действие была движением/переключением/форс-бурстом — после вызова
// m_ruleMoveBlockMs>0 или m_forceBurstMs изменилось или цель сменилась.
// Поэтому ProcessBossRules проверяет исполнение косвенно и по кулдауну.
bool PlayerbotAI::ProcessBossRules(Unit* target)
{
    Creature* boss = target->ToCreature();
    if (!boss)
        return false;

    m_bossEntry = boss->GetEntry();
    std::vector<BossRule> const* rules = sPlayerbotMgr.GetBossRules(m_bossEntry);
    if (!rules)
        return false;

    for (BossRule const& r : *rules)
    {
        auto cd = m_ruleCooldowns.find(r.Seq);
        if (cd != m_ruleCooldowns.end() && cd->second > 0)
            continue;
        if (!EvaluateBossTrigger(r, boss))
            continue;

        // запоминаем маркеры до исполнения
        uint32 moveBefore  = m_ruleMoveBlockMs;
        Unit* targetBefore = m_combatTarget;
        bool consumed = ExecuteBossAction(r, boss);

        bool movedToNew = (r.ActionType == BossRule::Action::RunFromBoss
                        || r.ActionType == BossRule::Action::Spread
                        || r.ActionType == BossRule::Action::Sidestep)
                        && m_ruleMoveBlockMs > moveBefore;
        bool switched  = (r.ActionType == BossRule::Action::SwitchTarget) && m_combatTarget != targetBefore;
        bool burstOpen = (r.ActionType == BossRule::Action::UseBurst) && m_forceBurstMs > 0;

        if (consumed)
        {
            m_ruleCooldowns[r.Seq] = r.CooldownMs;
            return true;
        }
        if (movedToNew || switched || burstOpen)
        {
            m_ruleCooldowns[r.Seq] = r.CooldownMs;
            return false;   // без GCD — тик жив, следующие правила не трогаем: одно действие за тик
        }
        // else: не смогли — пробуем следующее правило
    }
    return false;
}
