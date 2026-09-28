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
//       Аватар солнца 431425: каждый тик — луч 431911 по цели (упрощение «лучей»).
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
// PAL_REV_20260928

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <algorithm>
#include <any>

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
    SPELL_EX11_JUDGMENT_HOLY        = 275773
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

// 431380 - Рассветный свет (DoT): разлёт 8% тика, луч Аватара солнца, Затяжное сияние.
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

        int32 tick = caster->SpellDamageBonusDone(target, GetSpellInfo(), int32(aurEff->GetAmount()), DOT, aurEff->GetSpellEffectInfo());
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

        if (caster->HasAura(SPELL_EX11_SUNS_AVATAR) && caster->GetDistance(target) <= 30.f)
        {
            CastSpellExtraArgs args(EX11_TRIGGER);
            args.SetTriggeringAura(aurEff);
            caster->CastSpell(target, SPELL_EX11_SUNS_AVATAR_DAMAGE, args);
        }
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
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_dawnlight_dot_ex::HandlePeriodic, EFFECT_ALL, SPELL_AURA_PERIODIC_DAMAGE);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_dawnlight_dot_ex::HandleRemove, EFFECT_ALL, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

// --- КУЗНЕЦ СВЕТА ----------------------------------------------------------------

// 275779 - Правосудие (Прот): крит -> Молот и наковальня (433717 вокруг цели).
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
        caster->CastSpell(target, SPELL_EX11_HAMMER_AND_ANVIL_DMG, args);
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

void AddSC_paladin_spell_scripts_ex11()
{
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
