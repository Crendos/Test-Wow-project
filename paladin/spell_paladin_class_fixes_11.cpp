// ============================================================================
// Paladin 12.1.0 class fixes — часть 11 (28.09.2026): героические таланты
// (сверка по simc midnight 12.1.0.69933 + Wowhead 12.1) и Гильотина.
//
// Что здесь (всё — серверные скрипты; у талантов в DBC только Dummy «Server-side script»):
//   ГИЛЬОТИНА (аксессуар 270173): 1291728 (~3 PPM + хаст) -> 1306604,
//     урон +2% за каждый 1% недостающего здоровья цели (E1 = 2).
//   ХРАМОВНИК:
//     * Гнев нисхождения 431551: крит Эмпирейского молота (431398) -> 50% урона
//       соседям в 10 м (431625, основная цель не бьётся) + дебафф -5% урона по вам.
//     * Судия Света 1261525: крит Эмпирейского молота -> 50% шанс +1 стак
//       Избавления Света (433674). Родной прок ауры отключён (иначе ловит любой крит).
//     * (Ревностное оправдание, задержки молотков, Сакросанкт — в части 7;
//        Божественное взыскание — в части 6, в скрипте Звона.)
//   ВЕСТНИК СОЛНЦА:
//     * Аврора 439760: Рет — после Пробуждения зол Божественная цель (408458);
//       Свет — после Святой призмы / Божественного звона (223819). ВКД 2 с.
//     * Рассветный свет 431380: 8% урона тика (431581 E0) расходится по соседям (431399);
//       Аватар солнца 431425: лучи паладин -> Рассвет (431911 урон / 431939 хил, v2).
//     * Затяжное сияние 431407: истечение Рассвета -> Великое правосудие (197277)
//       на цель (продление — в части 7, ExtendDot).
//   КУЗНЕЦ СВЕТА:
//     * Молот и наковальня 433718: крит Правосудия (Прот 275779) -> волна 433717
//       вокруг цели. Прот-Звон с Раскатистым ударом (1271553) — в части 6.
//     * Сложить оружие 432866: доспех (432502/432496) спал с вас -> КД Возложения
//       рук -15 с (E0) + Сияющий свет (Прот, 327510) / Наставление Света (Свет, 54149).
//     * Доблесть 432919 (переписано, старая версия в части 7 была неверной):
//       трата Сияющего света (Прот, Слово славы) / Наставления Света (Свет) ->
//       КД Святых доспехов -3 с (E0 = 3000).
// Вставка: конец spell_paladin.cpp (после части 10). Регистрация: из
// AddSC_paladin_spell_scripts_ex10() (лоадер не меняется).
// Спутник: paladin_class_fixes_11.sql
// ============================================================================

// === CUT HERE ===============================================================
// PAL_REV2_20260928

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <algorithm>
#include <any>
#include <mutex>
#include <unordered_map>
#include "GameTime.h"

enum PaladinEx11Spells
{
    // Гильотина
    SPELL_EX11_GUILLOTINE_AURA      = 1291728,
    SPELL_EX11_GUILLOTINE_DAMAGE    = 1306604,

    // Храмовник
    SPELL_EX11_EMPYREAN_HAMMER      = 431398,
    SPELL_EX11_EMPYREAN_HAMMER_WD   = 431625,
    SPELL_EX11_WRATHFUL_DESCENT     = 431551,
    SPELL_EX11_LIGHTS_JUDICATOR     = 1261525,
    SPELL_EX11_LIGHTS_DELIVERANCE   = 433674,

    // Вестник солнца
    SPELL_EX11_AURORA               = 439760,
    SPELL_EX11_WAKE_OF_ASHES        = 255937,
    SPELL_EX11_HOLY_PRISM           = 114165,
    SPELL_EX11_DIVINE_TOLL          = 375576,
    SPELL_EX11_DP_RET               = 408458,
    SPELL_EX11_DP_HOLY              = 223819,
    SPELL_EX11_DAWNLIGHT_DOT        = 431380,
    SPELL_EX11_DAWNLIGHT_AOE        = 431399,
    SPELL_EX11_DAWNLIGHT_META       = 431581, // E0 = 8 (%), E1 = 5 целей
    SPELL_EX11_SUNS_AVATAR          = 431425,
    SPELL_EX11_SUNS_AVATAR_DAMAGE   = 431911,
    SPELL_EX11_LINGERING_RADIANCE   = 431407,
    SPELL_EX11_GREATER_JUDGMENT     = 197277,

    // Кузнец света
    SPELL_EX11_HAMMER_AND_ANVIL     = 433718,
    SPELL_EX11_HAMMER_AND_ANVIL_DMG = 433717,
    SPELL_EX11_JUDGMENT_PROT        = 275779,
    SPELL_EX11_LAYING_DOWN_ARMS     = 432866,
    SPELL_EX11_SACRED_WEAPON_BUFF   = 432502,
    SPELL_EX11_HOLY_BULWARK_BUFF    = 432496,
    SPELL_EX11_LAY_ON_HANDS         = 633,
    SPELL_EX11_SHINING_LIGHT_FREE   = 327510,
    SPELL_EX11_INFUSION_OF_LIGHT    = 54149,
    SPELL_EX11_VALIANCE             = 432919,
    SPELL_EX11_HOLY_ARMAMENTS_A     = 432459,
    SPELL_EX11_HOLY_ARMAMENTS_B     = 1289728,
    SPELL_EX11_WORD_OF_GLORY        = 85673,
    SPELL_EX11_FLASH_OF_LIGHT       = 19750,
    SPELL_EX11_HOLY_LIGHT           = 82326,
    SPELL_EX11_JUDGMENT_HOLY        = 275773,

    // PAL_REV2: доводка до ретейла
    SPELL_EX11_SENTINEL             = 389539,
    SPELL_EX11_SOTR                 = 53600,
    SPELL_EX11_HAMMER_OF_LIGHT      = 427453,
    SPELL_EX11_MORNING_STAR         = 431482, // Periodic Dummy 5 с
    SPELL_EX11_MORNING_STAR_BUFF    = 431539, // +5% за стак, до 10
    SPELL_EX11_DAWNLIGHT_HOT        = 431381,
    SPELL_EX11_SUNS_AVATAR_HEAL     = 431939,
    SPELL_EX11_ETERNAL_FLAME        = 156322,
    SPELL_EX11_HAMMER_AND_ANVIL_HEAL= 433722, // Свет: до 5 раненых союзников
    SPELL_EX11_DIVINE_INSPIRATION   = 432964,
    SPELL_EX11_HOLY_BULWARK_ABSORB  = 432607,
    SPELL_EX11_MASTERWORK           = 1271387,
    SPELL_EX11_MASTERWORK_WEAPON    = 1271436,
    SPELL_EX11_MASTERWORK_BULWARK   = 1271383,
    SPELL_EX11_LESSER_WEAPON        = 1239091,
    SPELL_EX11_LESSER_BULWARK       = 1239002,
    SPELL_EX11_CRUSADER_STRIKE      = 35395,
    SPELL_EX11_HAMMER_RIGHTEOUS     = 53595,
    SPELL_EX11_BLESSED_HAMMER       = 204019,
    SPELL_EX11_HOLY_SHOCK           = 20473,
    SPELL_EX11_SHARED_RESOLVE       = 432821,
    SPELL_EX11_DEVOTION_AURA        = 465,
    SPELL_EX11_REFLECTION_RADIANCE  = 1271466,
    SPELL_EX11_SACRED_WEAPON_DMG    = 432616,
    SPELL_EX11_SACRED_WEAPON_HEAL   = 441590,
    SPELL_EX11_GRAND_CRUSADER_TAL   = 85043,
    SPELL_EX11_GRAND_CRUSADER_BUFF  = 85416,
    SPELL_EX11_AVENGERS_SHIELD      = 31935,
    SPELL_EX11_AWAKENING_READY      = 414193
};

namespace
{
    constexpr TriggerCastFlags EX11_TRIGGER = TriggerCastFlags(TRIGGERED_IGNORE_CAST_IN_PROGRESS
        | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR);

    [[nodiscard]] int32 Ex11AuraAmount(Unit const* unit, uint32 spellId, SpellEffIndex eff, int32 fallback, int32 minV, int32 maxV)
    {
        if (AuraEffect const* e = unit->GetAuraEffect(spellId, eff))
        {
            int32 v = int32(e->GetAmount());
            if (v >= minV && v <= maxV)
                return v;
        }
        return fallback;
    }

    [[nodiscard]] ChrSpecialization Ex11Spec(Unit const* unit)
    {
        if (Player const* p = unit->ToPlayer())
            return p->GetPrimarySpecialization();
        return ChrSpecialization::None;
    }

    // Святые доспехи: у Света и Прота разные кнопки (432459 / 1289728) — снижаем обе,
    // и обычный КД, и восстановление заряда.
    void Ex11ReduceHolyArmaments(Unit* caster, int32 ms)
    {
        if (!caster || ms <= 0)
            return;
        for (uint32 id : { uint32(SPELL_EX11_HOLY_ARMAMENTS_A), uint32(SPELL_EX11_HOLY_ARMAMENTS_B) })
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
            if (!info)
                continue;
            caster->GetSpellHistory()->ModifyCooldown(info, Milliseconds(-ms));
            if (info->ChargeCategoryId)
                caster->GetSpellHistory()->ModifyChargeRecoveryTime(info->ChargeCategoryId, Milliseconds(-ms));
        }
    }

    // --- Страж (389539): задержки распада от потраченной Силы Света -------------
    struct Ex11SentinelState
    {
        int32 HolyPowerAcc = 0; // остаток < 3
        int32 Delays = 0;       // сколько секундных распадов пропустить
    };
    std::mutex gEx11SentinelLock;
    std::unordered_map<ObjectGuid, Ex11SentinelState> gEx11Sentinel;

    // --- Утренняя звезда (431539): множитель следующего Рассвета ----------------
    void Ex11ApplyMorningStar(Unit* caster, SpellEffectValue& amount)
    {
        if (!caster)
            return;
        Aura* buff = caster->GetAura(SPELL_EX11_MORNING_STAR_BUFF);
        if (!buff)
            return;
        double pct = 0.0;
        if (AuraEffect const* e = buff->GetEffect(EFFECT_0))
            pct = e->GetAmount(); // уже умножено на стаки
        if (pct <= 0.0 || pct > 200.0)
            pct = 5.0 * buff->GetStackAmount();
        amount *= 1.0 + pct / 100.0;
        // Бафф расходуется на этот Рассвет (снимаем вне расчёта ауры).
        caster->m_Events.AddEventAtOffset([caster]()
        {
            caster->RemoveAurasDueToSpell(SPELL_EX11_MORNING_STAR_BUFF);
        }, 1ms);
    }

    // --- Аватар солнца (431425): лучи паладин -> Рассвет -------------------------
    struct Ex11BeamArg
    {
        Position From;
        Position To;
        float HalfWidth = 2.f;
        uint32 SoftCap = 8;
    };

    [[nodiscard]] bool Ex11InBeam(Ex11BeamArg const& beam, WorldObject const* obj)
    {
        float const ax = beam.From.GetPositionX(), ay = beam.From.GetPositionY();
        float const bx = beam.To.GetPositionX(), by = beam.To.GetPositionY();
        float const px = obj->GetPositionX(), py = obj->GetPositionY();
        float const dx = bx - ax, dy = by - ay;
        float const len2 = dx * dx + dy * dy;
        float t = len2 > 0.f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
        t = std::clamp(t, 0.f, 1.f);
        float const cx = ax + t * dx - px, cy = ay + t * dy - py;
        float reach = beam.HalfWidth;
        if (Unit const* unit = obj->ToUnit())
            reach += unit->GetCombatReach() * 0.5f;
        return cx * cx + cy * cy <= reach * reach;
    }

    // Урон врагам / лечение союзникам на отрезке паладин -> цель Рассвета (<= 30 м, E7).
    // Ослабление сверх E5 (Свет, 5) / E8 (Рет, 8) целей — sqrt(N / число целей).
    void Ex11SunsAvatarBeam(Unit* caster, Unit* dawnTarget, AuraEffect const* aurEff)
    {
        if (!caster || !dawnTarget || !caster->HasAura(SPELL_EX11_SUNS_AVATAR) || caster == dawnTarget)
            return;
        float const range = float(Ex11AuraAmount(caster, SPELL_EX11_SUNS_AVATAR, EFFECT_7, 30, 5, 60));
        if (!caster->IsWithinDistInMap(dawnTarget, range))
            return;

        Ex11BeamArg beam;
        beam.From = caster->GetPosition();
        beam.To = dawnTarget->GetPosition();
        beam.SoftCap = uint32(Ex11Spec(caster) == ChrSpecialization::PaladinHoly
            ? Ex11AuraAmount(caster, SPELL_EX11_SUNS_AVATAR, EFFECT_5, 5, 1, 20)
            : Ex11AuraAmount(caster, SPELL_EX11_SUNS_AVATAR, EFFECT_8, 8, 1, 20));

        std::vector<Unit*> nearby;
        Trinity::AnyUnitInObjectRangeCheck check(caster, range + 5.f);
        Trinity::UnitListSearcher searcher(caster, nearby, check);
        Cell::VisitAllObjects(caster, searcher, range + 5.f);

        Unit* firstEnemy = nullptr;
        Unit* firstAlly = nullptr;
        for (Unit* unit : nearby)
        {
            if (!unit->IsAlive() || !Ex11InBeam(beam, unit))
                continue;
            if (!firstEnemy && caster->IsValidAttackTarget(unit))
                firstEnemy = unit;
            else if (!firstAlly && caster->IsValidAssistTarget(unit))
                firstAlly = unit;
        }

        auto cast = [&](Unit* explicitTarget, uint32 spellId)
        {
            if (!explicitTarget || !sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
                return;
            CastSpellExtraArgs args(EX11_TRIGGER);
            args.SetTriggeringAura(aurEff);
            args.SetCustomArg(beam);
            caster->CastSpell(explicitTarget, spellId, args);
        };
        cast(firstEnemy, SPELL_EX11_SUNS_AVATAR_DAMAGE);
        cast(firstAlly, SPELL_EX11_SUNS_AVATAR_HEAL);
    }

    // --- Кузнец света: союзник для доспеха ---------------------------------------
    // Ближайший живой член группы в радиусе без этого баффа от паладина (себя — в последнюю очередь).
    Unit* Ex11FindArmamentAlly(Unit* paladin, uint32 buffId, float radius)
    {
        std::vector<Unit*> nearby;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(paladin, paladin, radius);
        Trinity::UnitListSearcher searcher(paladin, nearby, check);
        Cell::VisitAllObjects(paladin, searcher, radius);

        Unit* best = nullptr;
        float bestDist = radius + 1.f;
        for (Unit* unit : nearby)
        {
            if (unit == paladin || !unit->IsAlive() || !unit->IsInRaidWith(paladin))
                continue;
            if (unit->HasAura(buffId, paladin->GetGUID()))
                continue;
            float const d = paladin->GetDistance(unit);
            if (d < bestDist)
            {
                bestDist = d;
                best = unit;
            }
        }
        return best ? best : paladin;
    }

    void Ex11ApplyArmament(Unit* paladin, Unit* who, bool weapon)
    {
        CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
        paladin->CastSpell(who, weapon ? SPELL_EX11_SACRED_WEAPON_BUFF : SPELL_EX11_HOLY_BULWARK_BUFF, args);
        if (!weapon)
            paladin->CastSpell(who, SPELL_EX11_HOLY_BULWARK_ABSORB, args);
    }

    // --- Отражение сияния (1271466): Прот — Великий крестоносец, Свет — Пробуждение ---
    void Ex11ReflectionOfRadiance(Unit* paladin)
    {
        if (!paladin)
            return;
        switch (Ex11Spec(paladin))
        {
            case ChrSpecialization::PaladinProtection:
            {
                if (!paladin->HasAura(SPELL_EX11_GRAND_CRUSADER_TAL))
                    return;
                paladin->GetSpellHistory()->ResetCooldown(SPELL_EX11_AVENGERS_SHIELD, true);
                if (SpellInfo const* as = sSpellMgr->GetSpellInfo(SPELL_EX11_AVENGERS_SHIELD, DIFFICULTY_NONE))
                    if (as->ChargeCategoryId)
                        paladin->GetSpellHistory()->RestoreCharge(as->ChargeCategoryId);
                if (sSpellMgr->GetSpellInfo(SPELL_EX11_GRAND_CRUSADER_BUFF, DIFFICULTY_NONE))
                    paladin->CastSpell(paladin, SPELL_EX11_GRAND_CRUSADER_BUFF, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
                break;
            }
            case ChrSpecialization::PaladinHoly:
                paladin->CastSpell(paladin, SPELL_EX11_AWAKENING_READY, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
                break;
            default:
                break;
        }
    }
}

// --- ГИЛЬОТИНА ---------------------------------------------------------------

// 1291728 - Гильотина (аксессуар 270173). Прок (PPM из DB2) -> 1306604 по цели.
// Уровень предмета передаётся явно: TriggeringAura его в урон не переносит.
class spell_item_guillotine_proc_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX11_GUILLOTINE_DAMAGE });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        if (!caster)
            return;

        Unit* target = eventInfo.GetActionTarget();
        if (!target || target == caster || !caster->IsValidAttackTarget(target))
            target = caster->GetVictim();
        if (!target || !caster->IsValidAttackTarget(target))
            return;

        CastSpellExtraArgs args(aurEff);
        args.OriginalCastItemLevel = aurEff->GetBase()->GetCastItemLevel();
        caster->CastSpell(target, SPELL_EX11_GUILLOTINE_DAMAGE, args);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_item_guillotine_proc_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 1306604 - Гильотина: +2% урона за каждый 1% недостающего здоровья цели.
class spell_item_guillotine_damage_ex : public SpellScript
{
    void HandleCalcDamage(SpellEffectInfo const& /*spellEffectInfo*/, Unit* victim, int32& /*damage*/, int32& /*flatMod*/, float& pctMod)
    {
        if (!victim)
            return;
        float perPct = 2.f;
        if (Unit* caster = GetCaster())
            perPct = float(Ex11AuraAmount(caster, SPELL_EX11_GUILLOTINE_AURA, EFFECT_1, 2, 1, 10));
        float const missing = std::clamp(100.f - victim->GetHealthPct(), 0.f, 100.f);
        AddPct(pctMod, perPct * missing);
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_item_guillotine_damage_ex::HandleCalcDamage);
    }
};

// --- ХРАМОВНИК -----------------------------------------------------------------

// 431398 - Эмпирейский молот: криты -> Гнев нисхождения и Судия Света.
class spell_pal_empyrean_hammer_crit_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX11_EMPYREAN_HAMMER_WD, SPELL_EX11_LIGHTS_DELIVERANCE });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !IsHitCrit())
            return;

        // Гнев нисхождения: 50% (E1) урона крита — соседям.
        if (caster->HasAura(SPELL_EX11_WRATHFUL_DESCENT))
        {
            int32 const pct = Ex11AuraAmount(caster, SPELL_EX11_WRATHFUL_DESCENT, EFFECT_1, 50, 1, 200);
            uint32 const critDamage = Unit::SpellCriticalDamageBonus(caster, GetSpellInfo(), uint32(std::max(GetHitDamage(), 0)), target);
            int32 const splash = CalculatePct(int32(critDamage), pct);
            if (splash > 0)
            {
                CastSpellExtraArgs args(EX11_TRIGGER);
                args.SetTriggeringSpell(GetSpell());
                args.AddSpellMod(SPELLVALUE_BASE_POINT0, splash);
                args.SetCustomArg(target->GetGUID()); // основную цель не бьём (simc: secondary targets only)
                caster->CastSpell(target, SPELL_EX11_EMPYREAN_HAMMER_WD, args);
            }
        }

        // Судия Света: 50% — дополнительный стак Избавления Света.
        if (caster->HasAura(SPELL_EX11_LIGHTS_JUDICATOR) && roll_chance(50))
        {
            CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
            args.SetTriggeringSpell(GetSpell());
            caster->CastSpell(caster, SPELL_EX11_LIGHTS_DELIVERANCE, args);
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_empyrean_hammer_crit_ex::HandleAfterHit);
    }
};

// 431551 / 1261525 — родные проки талантов отключены: срабатывают из скрипта молота
// (иначе DBC-маска ловит любой крит любого спелла).
class spell_pal_hero_proc_disabled_ex : public AuraScript
{
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pal_hero_proc_disabled_ex::CheckProc);
    }
};

// 431625 (Гнев нисхождения) / 431399 (разлёт Рассвета): урон только по соседям.
// Основная цель передаётся в CustomArg. Дебафф (-5% урона) остаётся на всех.
class spell_pal_splash_skip_primary_ex : public SpellScript
{
    void HandleLaunchTarget(SpellEffIndex effIndex)
    {
        ObjectGuid const* primary = std::any_cast<ObjectGuid>(&GetSpell()->m_customArg);
        if (primary && GetHitUnit() && GetHitUnit()->GetGUID() == *primary)
            PreventHitDefaultEffect(effIndex);
    }

    void Register() override
    {
        OnEffectLaunchTarget += SpellEffectFn(spell_pal_splash_skip_primary_ex::HandleLaunchTarget, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// --- ВЕСТНИК СОЛНЦА -----------------------------------------------------------

// 255937 / 114165 / 375576 - Аврора: Божественная цель после Пробуждения зол (Рет)
// или Святой призмы / Звона (Свет). ВКД 2 с — как в DBC.
class spell_pal_aurora_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX11_AURORA))
            return;

        uint32 const id = GetSpellInfo()->Id;
        uint32 buff = 0;
        switch (Ex11Spec(caster))
        {
            case ChrSpecialization::PaladinRetribution:
                if (id == SPELL_EX11_WAKE_OF_ASHES)
                    buff = SPELL_EX11_DP_RET;
                break;
            case ChrSpecialization::PaladinHoly:
                if (id == SPELL_EX11_HOLY_PRISM || id == SPELL_EX11_DIVINE_TOLL)
                    buff = SPELL_EX11_DP_HOLY;
                break;
            default:
                break;
        }
        if (!buff || caster->GetSpellHistory()->HasCooldown(SPELL_EX11_AURORA))
            return;

        caster->GetSpellHistory()->AddCooldown(SPELL_EX11_AURORA, 0, Seconds(2));
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.SetTriggeringSpell(GetSpell());
        caster->CastSpell(caster, buff, args);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_aurora_ex::HandleAfterCast);
    }
};

// 431380 - Рассветный свет (DoT): разлёт 8% тика, лучи Аватара солнца, Затяжное сияние,
// Утренняя звезда (множитель при наложении).
class spell_pal_dawnlight_dot_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX11_DAWNLIGHT_AOE });
    }

    void HandlePeriodic(AuraEffect const* aurEff)
    {
        Unit* caster = GetCaster();
        Unit* target = GetTarget();
        if (!caster || !target || !caster->IsValidAttackTarget(target))
            return;

        // simc: 8% от ИТОГОВОГО тика (done + taken), без крита.
        int32 tick = caster->SpellDamageBonusDone(target, GetSpellInfo(), int32(aurEff->GetAmount()), DOT, aurEff->GetSpellEffectInfo());
        tick = target->SpellDamageBonusTaken(caster, GetSpellInfo(), tick, DOT);
        int32 pct = 8;
        if (SpellInfo const* meta = sSpellMgr->GetSpellInfo(SPELL_EX11_DAWNLIGHT_META, DIFFICULTY_NONE))
            if (meta->GetEffects().size() > 0)
            {
                int32 v = int32(meta->GetEffect(EFFECT_0).CalcValue(caster));
                if (v > 0 && v <= 50)
                    pct = v;
            }

        int32 const splash = CalculatePct(tick, pct);
        if (splash > 0)
        {
            CastSpellExtraArgs args(EX11_TRIGGER);
            args.SetTriggeringAura(aurEff);
            args.AddSpellMod(SPELLVALUE_BASE_POINT0, splash);
            args.SetCustomArg(target->GetGUID());
            caster->CastSpell(target, SPELL_EX11_DAWNLIGHT_AOE, args);
        }

        Ex11SunsAvatarBeam(caster, target, aurEff);
    }

    void CalcAmount(AuraEffect const* /*aurEff*/, SpellEffectValue& amount, bool& /*canBeRecalculated*/)
    {
        Ex11ApplyMorningStar(GetCaster(), amount);
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;
        Unit* caster = GetCaster();
        Unit* target = GetTarget();
        if (!caster || !target || !caster->HasAura(SPELL_EX11_LINGERING_RADIANCE) || !caster->IsValidAttackTarget(target))
            return;
        caster->CastSpell(target, SPELL_EX11_GREATER_JUDGMENT, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_pal_dawnlight_dot_ex::CalcAmount, EFFECT_ALL, SPELL_AURA_PERIODIC_DAMAGE);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_dawnlight_dot_ex::HandlePeriodic, EFFECT_ALL, SPELL_AURA_PERIODIC_DAMAGE);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_dawnlight_dot_ex::HandleRemove, EFFECT_ALL, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

// --- КУЗНЕЦ СВЕТА ----------------------------------------------------------------

// 275779 / 275773 - Правосудие (Прот / Свет): крит -> Молот и наковальня.
class spell_pal_hammer_and_anvil_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX11_HAMMER_AND_ANVIL_DMG });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !IsHitCrit() || !caster->HasAura(SPELL_EX11_HAMMER_AND_ANVIL))
            return;
        CastSpellExtraArgs args(EX11_TRIGGER);
        args.SetTriggeringSpell(GetSpell());
        // Прот (275779) — урон 433717 вокруг цели; Свет (275773) — исцеление 433722
        // до 5 раненых союзников рядом с целью.
        uint32 const wave = GetSpellInfo()->Id == SPELL_EX11_JUDGMENT_HOLY ? SPELL_EX11_HAMMER_AND_ANVIL_HEAL : SPELL_EX11_HAMMER_AND_ANVIL_DMG;
        caster->CastSpell(target, wave, args);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_hammer_and_anvil_ex::HandleAfterHit);
    }
};

// 432502 / 432496 - доспех спал с паладина -> Сложить оружие.
class spell_pal_laying_down_arms_ex : public AuraScript
{
    bool _done = false;

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (_done)
            return;
        _done = true;

        AuraRemoveMode const mode = GetTargetApplication()->GetRemoveMode();
        if (mode == AURA_REMOVE_BY_DEATH)
            return;

        Unit* target = GetTarget();
        if (!target || GetCasterGUID() != target->GetGUID() || !target->HasAura(SPELL_EX11_LAYING_DOWN_ARMS))
            return;

        int32 const ms = Ex11AuraAmount(target, SPELL_EX11_LAYING_DOWN_ARMS, EFFECT_0, 15000, 1000, 60000);
        target->GetSpellHistory()->ModifyCooldown(SPELL_EX11_LAY_ON_HANDS, Milliseconds(-ms));

        uint32 buff = 0;
        switch (Ex11Spec(target))
        {
            case ChrSpecialization::PaladinProtection: buff = SPELL_EX11_SHINING_LIGHT_FREE; break;
            case ChrSpecialization::PaladinHoly:       buff = SPELL_EX11_INFUSION_OF_LIGHT;  break;
            default: break;
        }
        if (buff)
            target->CastSpell(target, buff, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_laying_down_arms_ex::HandleRemove, EFFECT_ALL, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 85673 (Прот) / 19750, 82326, 275773 (Свет) - Доблесть 432919:
// трата Сияющего света / Наставления Света -> КД Святых доспехов -3 с.
class spell_pal_valiance_consume_ex : public SpellScript
{
    uint32 _buff = 0;

    void Snapshot()
    {
        _buff = 0;
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX11_VALIANCE))
            return;
        uint32 const id = GetSpellInfo()->Id;
        switch (Ex11Spec(caster))
        {
            case ChrSpecialization::PaladinProtection:
                if (id == SPELL_EX11_WORD_OF_GLORY && caster->HasAura(SPELL_EX11_SHINING_LIGHT_FREE))
                    _buff = SPELL_EX11_SHINING_LIGHT_FREE;
                break;
            case ChrSpecialization::PaladinHoly:
                if (id != SPELL_EX11_WORD_OF_GLORY && caster->HasAura(SPELL_EX11_INFUSION_OF_LIGHT))
                    _buff = SPELL_EX11_INFUSION_OF_LIGHT;
                break;
            default:
                break;
        }
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !_buff)
            return;
        // Считаем тратой: бафф исчез или потерял стак.
        Aura* aura = caster->GetAura(_buff);
        if (aura && !GetSpell()->m_appliedMods.count(aura))
            return;
        Ex11ReduceHolyArmaments(caster, Ex11AuraAmount(caster, SPELL_EX11_VALIANCE, EFFECT_0, 3000, 500, 10000));
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_pal_valiance_consume_ex::Snapshot);
        AfterCast += SpellCastFn(spell_pal_valiance_consume_ex::HandleAfterCast);
    }
};

// --- PAL_REV2: ЗАЩИТА -----------------------------------------------------------

// 389539 - Страж (wowhead 12.x): 15 стаков Божественной решимости (+1% здоровья,
// -2% урона за стак). Через 5 с теряет 1 стак в секунду; каждые 3 потраченные Силы Света
// откладывают следующую потерю на 1 с. Секундный тик — родной «Periodically trigger spell»
// ауры (E10); его триггер-спелл глушим и распад ведём здесь.
class spell_pal_sentinel_decay_ex : public AuraScript
{
    static constexpr uint8 SENTINEL_STACKS = 15;
    static constexpr int32 DECAY_DELAY_MS = 5000;

    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        {
            std::lock_guard<std::mutex> guard(gEx11SentinelLock);
            gEx11Sentinel[target->GetGUID()] = Ex11SentinelState{};
        }
        uint8 const stacks = GetSpellInfo()->StackAmount > 1 ? uint8(GetSpellInfo()->StackAmount) : SENTINEL_STACKS;
        // Стаки выставляем вне обработчика наложения (SetStackAmount пересчитывает эффекты).
        target->m_Events.AddEventAtOffset([target, stacks]()
        {
            if (Aura* aura = target->GetAura(SPELL_EX11_SENTINEL, target->GetGUID()))
                if (aura->GetStackAmount() < stacks)
                    aura->SetStackAmount(stacks);
        }, 1ms);
    }

    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();
        Aura* aura = GetAura();
        if (aura->GetMaxDuration() - aura->GetDuration() < DECAY_DELAY_MS - 100)
            return;
        {
            std::lock_guard<std::mutex> guard(gEx11SentinelLock);
            Ex11SentinelState& state = gEx11Sentinel[GetTarget()->GetGUID()];
            if (state.Delays > 0)
            {
                --state.Delays;
                return;
            }
        }
        if (aura->GetStackAmount() > 1)
            aura->SetStackAmount(aura->GetStackAmount() - 1);
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        std::lock_guard<std::mutex> guard(gEx11SentinelLock);
        gEx11Sentinel.erase(GetTarget()->GetGUID());
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_sentinel_decay_ex::HandleApply, EFFECT_ALL, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_sentinel_decay_ex::HandlePeriodic, EFFECT_ALL, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_sentinel_decay_ex::HandleRemove, EFFECT_ALL, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
    }
};

// 53600 / 85673 / 427453 - траты Силы Света Прота: копят задержку распада Стража.
class spell_pal_sentinel_spend_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX11_SENTINEL))
            return;
        Optional<int32> spent = GetHolyPowerCost(GetSpell());
        if (!spent || *spent <= 0)
            return;
        std::lock_guard<std::mutex> guard(gEx11SentinelLock);
        Ex11SentinelState& state = gEx11Sentinel[caster->GetGUID()];
        state.HolyPowerAcc += *spent;
        state.Delays += state.HolyPowerAcc / 3;
        state.HolyPowerAcc %= 3;
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_sentinel_spend_ex::HandleAfterCast);
    }
};

// --- PAL_REV2: ВЕСТНИК СОЛНЦА ---------------------------------------------------

// 431482 - Утренняя звезда: каждые 5 с (родной Periodic Dummy) +1 стак 431539
// (+5% к следующему Рассвету, до 10); вне боя — вдвое быстрее (+2 за тик).
class spell_pal_morning_star_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX11_MORNING_STAR_BUFF });
    }

    void HandlePeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        if (!target->IsAlive())
            return;
        int32 const stacks = target->IsInCombat() ? 1 : 2;
        for (int32 i = 0; i < stacks; ++i)
            target->CastSpell(target, SPELL_EX11_MORNING_STAR_BUFF, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_morning_star_ex::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 431381 - Рассвет на союзнике (Свет, HoT): Утренняя звезда, лучи Аватара солнца,
// Затяжное сияние -> Вечное пламя (только HoT, 6 с) по истечении.
class spell_pal_dawnlight_hot_ex : public AuraScript
{
    void CalcAmount(AuraEffect const* /*aurEff*/, SpellEffectValue& amount, bool& /*canBeRecalculated*/)
    {
        Ex11ApplyMorningStar(GetCaster(), amount);
    }

    void HandlePeriodic(AuraEffect const* aurEff)
    {
        Ex11SunsAvatarBeam(GetCaster(), GetTarget(), aurEff);
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX11_LINGERING_RADIANCE))
            PalLingeringRadianceFlame(caster, GetTarget());
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_pal_dawnlight_hot_ex::CalcAmount, EFFECT_ALL, SPELL_AURA_PERIODIC_HEAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_dawnlight_hot_ex::HandlePeriodic, EFFECT_ALL, SPELL_AURA_PERIODIC_HEAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_dawnlight_hot_ex::HandleRemove, EFFECT_ALL, SPELL_AURA_PERIODIC_HEAL, AURA_EFFECT_HANDLE_REAL);
    }
};

// 431911 (урон) / 431939 (лечение) - Аватар солнца: у спелла радиус 40 м, оставляем
// только цели на луче (Ex11BeamArg в CustomArg) и режем урон сверх N целей.
class spell_pal_suns_avatar_beam_ex : public SpellScript
{
    float _scale = 1.f;

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Ex11BeamArg const* beam = std::any_cast<Ex11BeamArg>(&GetSpell()->m_customArg);
        if (!beam)
            return;
        targets.remove_if([beam](WorldObject const* obj) { return !Ex11InBeam(*beam, obj); });
        if (beam->SoftCap > 0 && targets.size() > beam->SoftCap)
            _scale = std::sqrt(float(beam->SoftCap) / float(targets.size()));
    }

    void HandleCalcDamage(SpellEffectInfo const& /*spellEffectInfo*/, Unit* /*victim*/, int32& /*damage*/, int32& /*flatMod*/, float& pctMod)
    {
        pctMod *= _scale;
    }

    void HandleCalcHealing(SpellEffectInfo const& /*spellEffectInfo*/, Unit* /*victim*/, int32& /*healing*/, int32& /*flatMod*/, float& pctMod)
    {
        pctMod *= _scale;
    }

    void Register() override
    {
        if (m_scriptSpellId == SPELL_EX11_SUNS_AVATAR_HEAL)
        {
            OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pal_suns_avatar_beam_ex::FilterTargets, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ALLY);
            CalcHealing += SpellCalcHealingFn(spell_pal_suns_avatar_beam_ex::HandleCalcHealing);
        }
        else
        {
            OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pal_suns_avatar_beam_ex::FilterTargets, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ENEMY);
            CalcDamage += SpellCalcDamageFn(spell_pal_suns_avatar_beam_ex::HandleCalcDamage);
        }
    }
};

// 156322 - Вечное пламя от Затяжного сияния: без прямого лечения, только HoT (6 с).
class spell_pal_eternal_flame_lr_ex : public SpellScript
{
    void HandleHeal(SpellEffIndex effIndex)
    {
        if (std::any_cast<LingeringRadianceFlameMark>(&GetSpell()->m_customArg))
            PreventHitDefaultEffect(effIndex);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pal_eternal_flame_lr_ex::HandleHeal, EFFECT_ALL, SPELL_EFFECT_HEAL);
    }
};

// --- PAL_REV2: КУЗНЕЦ СВЕТА --------------------------------------------------------

// 432964 - Божественное вдохновение: заклинания и способности с шансом (RPPM 0.55 с
// хастом, simc) создают Святой доспех для союзника рядом. Доспехи чередуются;
// Мастерскую работу не запускает (simc: src != LS_DIVINE_INSPIRATION).
class spell_pal_divine_inspiration_ex : public AuraScript
{
    static constexpr float RPPM = 0.55f;

    uint32 _lastAttemptMs = 0;
    int8 _nextWeapon = -1;

    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        Unit* owner = GetTarget();
        uint32 const now = GameTime::GetGameTimeMS();
        uint32 elapsed = _lastAttemptMs ? now - _lastAttemptMs : 10000;
        _lastAttemptMs = now;
        elapsed = std::min<uint32>(elapsed, 10000);

        float haste = 1.f;
        float const mod = owner->m_unitData->ModSpellHaste;
        if (mod > 0.f)
            haste = 1.f / mod;
        float const chance = RPPM * haste * float(elapsed) / 60000.f;
        return roll_chance(chance * 100.f);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();
        Unit* paladin = GetTarget();
        if (_nextWeapon < 0)
            _nextWeapon = int8(urand(0, 1));
        bool const weapon = _nextWeapon == 1;
        _nextWeapon = weapon ? 0 : 1;

        Unit* ally = Ex11FindArmamentAlly(paladin, weapon ? SPELL_EX11_SACRED_WEAPON_BUFF : SPELL_EX11_HOLY_BULWARK_BUFF, 30.f);
        Ex11ApplyArmament(paladin, ally, weapon);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pal_divine_inspiration_ex::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_pal_divine_inspiration_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 35395 / 53595 / 204019 / 20473 - Мастерская работа: трата заряда (1271436 / 1271383)
// -> Малое оружие (1239091, 5 стаков) / Малый оплот (1239002) союзнику рядом.
// Прот — Удар воина Света и его замены; Свет — Шок небес или Удар воина Света.
class spell_pal_masterwork_consume_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX11_MASTERWORK))
            return;
        ChrSpecialization const spec = Ex11Spec(caster);
        uint32 const id = GetSpellInfo()->Id;
        if (id == SPELL_EX11_HOLY_SHOCK && spec != ChrSpecialization::PaladinHoly)
            return;
        if (spec != ChrSpecialization::PaladinHoly && spec != ChrSpecialization::PaladinProtection)
            return;

        for (bool weapon : { true, false })
        {
            Aura* counter = caster->GetAura(weapon ? SPELL_EX11_MASTERWORK_WEAPON : SPELL_EX11_MASTERWORK_BULWARK);
            if (!counter)
                continue;
            counter->ModStackAmount(-1);

            uint32 const lesser = weapon ? SPELL_EX11_LESSER_WEAPON : SPELL_EX11_LESSER_BULWARK;
            Unit* ally = Ex11FindArmamentAlly(caster, lesser, 30.f);
            caster->CastSpell(ally, lesser, CastSpellExtraArgs(TRIGGERED_FULL_MASK).SetTriggeringSpell(GetSpell()));
            if (weapon)
                if (Aura* buff = ally->GetAura(lesser, caster->GetGUID()))
                    if (buff->GetSpellInfo()->StackAmount >= 5)
                        buff->SetStackAmount(5);
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_masterwork_consume_ex::HandleAfterCast);
    }
};

// 465 - Аура благочестия + Общая решимость (432821): эффект ауры +33% на целях
// с вашими доспехами (432502 / 432496).
class spell_pal_shared_resolve_devotion_ex : public AuraScript
{
    void CalcAmount(AuraEffect const* /*aurEff*/, SpellEffectValue& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = true;
        Unit* caster = GetCaster();
        Unit* owner = GetUnitOwner();
        if (!caster || !owner || !caster->HasAura(SPELL_EX11_SHARED_RESOLVE))
            return;
        auto hasArmament = [&](uint32 id)
        {
            Aura const* aura = owner->GetAura(id, caster->GetGUID());
            return aura && !aura->IsRemoved();
        };
        if (hasArmament(SPELL_EX11_SACRED_WEAPON_BUFF) || hasArmament(SPELL_EX11_HOLY_BULWARK_BUFF))
            amount *= 1.33;
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_pal_shared_resolve_devotion_ex::CalcAmount, EFFECT_ALL, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN);
    }
};

// 432502 / 432496 - доспех наложен/снят: пересчитать Ауру благочестия носителя.
class spell_pal_shared_resolve_armament_ex : public AuraScript
{
    void Recalc(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* owner = GetTarget();
        owner->m_Events.AddEventAtOffset([owner]()
        {
            Unit::AuraApplicationMapBounds range = owner->GetAppliedAuras().equal_range(SPELL_EX11_DEVOTION_AURA);
            for (auto itr = range.first; itr != range.second; ++itr)
                for (AuraEffect* eff : itr->second->GetBase()->GetAuraEffects())
                    if (eff && eff->GetAuraType() == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN)
                        eff->RecalculateAmount();
        }, 1ms);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_shared_resolve_armament_ex::Recalc, EFFECT_0, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_shared_resolve_armament_ex::Recalc, EFFECT_0, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 432616 / 441590 - Святое оружие нанесло урон / лечение: Отражение сияния, 10% (simc).
class spell_pal_reflection_sacred_weapon_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* paladin = GetOriginalCaster();
        if (!paladin)
            paladin = GetCaster();
        if (paladin && paladin->HasAura(SPELL_EX11_REFLECTION_RADIANCE) && roll_chance(10))
            Ex11ReflectionOfRadiance(paladin);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_reflection_sacred_weapon_ex::HandleAfterCast);
    }
};

// 432607 - Святой оплот поглотил урон: Отражение сияния, 20% (simc).
class spell_pal_reflection_holy_bulwark_ex : public AuraScript
{
    void HandleAbsorb(AuraEffect* /*aurEff*/, DamageInfo& /*dmgInfo*/, uint32& absorbAmount)
    {
        if (!absorbAmount)
            return;
        Unit* paladin = GetCaster();
        if (paladin && paladin->HasAura(SPELL_EX11_REFLECTION_RADIANCE) && roll_chance(20))
            Ex11ReflectionOfRadiance(paladin);
    }

    void Register() override
    {
        AfterEffectAbsorb += AuraEffectAbsorbFn(spell_pal_reflection_holy_bulwark_ex::HandleAbsorb, EFFECT_ALL);
    }
};

void AddSC_paladin_spell_scripts_ex11()
{
    RegisterSpellScript(spell_pal_sentinel_decay_ex);
    RegisterSpellScript(spell_pal_sentinel_spend_ex);
    RegisterSpellScript(spell_pal_morning_star_ex);
    RegisterSpellScript(spell_pal_dawnlight_hot_ex);
    RegisterSpellScript(spell_pal_suns_avatar_beam_ex);
    RegisterSpellScript(spell_pal_eternal_flame_lr_ex);
    RegisterSpellScript(spell_pal_divine_inspiration_ex);
    RegisterSpellScript(spell_pal_masterwork_consume_ex);
    RegisterSpellScript(spell_pal_shared_resolve_devotion_ex);
    RegisterSpellScript(spell_pal_shared_resolve_armament_ex);
    RegisterSpellScript(spell_pal_reflection_sacred_weapon_ex);
    RegisterSpellScript(spell_pal_reflection_holy_bulwark_ex);
    RegisterSpellScript(spell_item_guillotine_proc_ex);
    RegisterSpellScript(spell_item_guillotine_damage_ex);
    RegisterSpellScript(spell_pal_empyrean_hammer_crit_ex);
    RegisterSpellScript(spell_pal_hero_proc_disabled_ex);
    RegisterSpellScript(spell_pal_splash_skip_primary_ex);
    RegisterSpellScript(spell_pal_aurora_ex);
    RegisterSpellScript(spell_pal_dawnlight_dot_ex);
    RegisterSpellScript(spell_pal_hammer_and_anvil_ex);
    RegisterSpellScript(spell_pal_laying_down_arms_ex);
    RegisterSpellScript(spell_pal_valiance_consume_ex);
}
