// ============================================================================
// Paladin 12.1.0 class fixes — часть 1: Воздаяние (Retribution) + P0-фиксы.
//
// КАК СТАВИТЬ:
//   1) Всё содержимое ниже строки-маркера CUT HERE вставить в КОНЕЦ
//      src/server/scripts/Spells/spell_paladin.cpp ПЕРЕД функцией
//      AddSC_paladin_spell_scripts() (или после неё — до конца файла).
//   2) Отдельно в лоадер НЕ нужно: AddSC_paladin_spell_scripts_ex() вызывается
//      из AddSC_paladin_spell_scripts_ex2() (см. INSTALL.md, шаг 1.2). Если вызов
//      уже добавлен вручную — не страшно, повторная регистрация заблокирована.
//   3) Прогнать paladin/paladin_class_fixes.sql на world-базу.
//
// Все ID проверены по данным клиента 12.1.0 (build 69497/69933).
// ============================================================================

// === CUT HERE ===============================================================

// Поиск соседних врагов (Приговор казни) — стоковый spell_paladin.cpp этих заголовков не подключает.
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include "GameTime.h"
#include <unordered_map>
#include <unordered_set>

// MSVC не переваривает вложенные braced-списки в designated-инициализаторе
// CastSpellExtraArgsInit ( SpellValueOverrides = { {mod,val} } ) — собираем через хелпер.
// Две перегрузки: SpellValueModFloat (BASE_POINT0.., double) и SpellValueMod (DURATION и пр., int32).
[[nodiscard]] inline CastSpellExtraArgs MakeSpellArgs(TriggerCastFlags flags, Spell const* triggeringSpell, SpellValueModFloat mod, SpellEffectValue val)
{
    CastSpellExtraArgs args(flags);
    if (triggeringSpell)
        args.TriggeringSpell = triggeringSpell;
    args.SpellValueOverrides.emplace_back(mod, val);
    return args;
}

[[nodiscard]] inline CastSpellExtraArgs MakeSpellArgs(TriggerCastFlags flags, Spell const* triggeringSpell, SpellValueMod mod, int32 val)
{
    CastSpellExtraArgs args(flags);
    if (triggeringSpell)
        args.TriggeringSpell = triggeringSpell;
    args.SpellValueOverrides.emplace_back(mod, val);
    return args;
}


enum PaladinExSpells
{
    SPELL_EX_CRUSADER_STRIKE                  = 35395,
    SPELL_EX_BLADE_OF_JUSTICE                 = 184575,
    SPELL_EX_JUDGMENT_RET                     = 20271,
    SPELL_EX_JUDGMENT_PROT                    = 275779,
    SPELL_EX_JUDGMENT_HOLY                    = 275773,
    SPELL_EX_DIVINE_STORM                     = 53385,
    SPELL_EX_WORD_OF_GLORY                    = 85673, // (резерв)
    SPELL_EX_HAMMER_OF_WRATH                  = 24275,
    SPELL_EX_HAMMER_OF_WRATH_AW               = 1241413,
    SPELL_EX_TEMPLAR_STRIKE                   = 407480,
    SPELL_EX_TEMPLAR_SLASH                    = 406647,
    SPELL_EX_CRUSADING_STRIKE                 = 408385,
    SPELL_EX_CRUSADING_STRIKES_TALENT         = 406833,
    SPELL_EX_CONSECRATED_BLADE                = 404834,
    SPELL_EX_CONSECRATED_BLADE_ICD            = 407475,
    SPELL_EX_CONSECRATION                     = 26573,
    SPELL_EX_EXECUTION_SENTENCE               = 343527,
    SPELL_EX_EXECUTION_SENTENCE_DAMAGE        = 1260251,
    SPELL_EX_EXECUTION_SENTENCE_PAYOFF        = 387113,
    SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE        = 383921,
    SPELL_EX_AVENGING_WRATH                   = 31884,
    SPELL_EX_AVENGING_WRATH_8S                = 454351, // вариант АН от Сияющей славы
    SPELL_EX_CRUSADE                          = 231895, // аура Крестового похода (10 стаков)
    SPELL_EX_EXPURGATION_DOT                  = 383346,
    SPELL_EX_FINAL_VERDICT                    = 383328,
    SPELL_EX_TEMPLARS_VERDICT                 = 85256,
    SPELL_EX_WAKE_OF_ASHES                    = 255937,

    // таланты/баффы
    SPELL_EX_ART_OF_WAR                       = 406064,
    SPELL_EX_ART_OF_WAR_READY                 = 406086, // +80% к след. Клинку (метки DBC)
    SPELL_EX_RIGHTEOUS_CAUSE                  = 402912,
    SPELL_EX_RIGHTEOUS_CAUSE_READY            = 402916,
    SPELL_EX_EMPYREAN_POWER                   = 326732,
    SPELL_EX_EMPYREAN_POWER_READY             = 326733,
    SPELL_EX_GREATER_JUDGMENT                 = 231663,
    SPELL_EX_GREATER_JUDGMENT_DEBUFF          = 197277,
    SPELL_EX_GREATER_JUDGMENT_HOLY            = 231644, // Свет: E0 = 483% SP (уже с +250% из 12.0.5)
    SPELL_EX_UNWORTHY                         = 414022, // «Недостойный»: E0 dummy = остаток поглощения
    SPELL_EX_INFUSION_OF_LIGHT                = 54149,
    SPELL_EX_MASTERY_RETRIBUTION              = 267316,
    SPELL_EX_BOUNDLESS_JUDGMENT               = 405278,
    SPELL_EX_JUDGE_JURY_EXECUTIONER           = 406157,
    SPELL_EX_JJE_BUFF                         = 1253174,
    SPELL_EX_JJE_REFUND                       = 1253175, // ENERGIZE Holy Power
    SPELL_EX_LIGHT_WITHIN_DAMAGE              = 1261111,
    SPELL_EX_LIGHT_WITHIN_BLADE               = 1261159,
    SPELL_EX_LIGHT_WITHIN_WAVE                = 1261160,
    SPELL_EX_EMPIREAN_LEGACY                  = 387170, // Гнев карателя -> бафф, +25%
    SPELL_EX_EMPYREAN_LEGACY_JUDGMENT         = 1241358, // крит Правосудия -> Торжество, 30%
    SPELL_EX_EMPIREAN_LEGACY_READY            = 387178,
    SPELL_EX_LIGHT_OF_DAWN                    = 85222,
    SPELL_EX_ETERNAL_FLAME                    = 156322,
    SPELL_EX_HAMMER_OF_LIGHT                  = 427453,
    SPELL_EX_SPEC_RET                         = 137027,
    SPELL_EX_SPEC_PROT                        = 137028,
    SPELL_EX_SPEC_HOLY                        = 137029,
    SPELL_EX_WALK_INTO_LIGHT                  = 1263782,
    SPELL_EX_WALK_INTO_LIGHT_HP               = 1263963,
    SPELL_EX_BLESSING_OF_ANSHE_RET            = 445206,
    SPELL_EX_CRUSADE_TALENT                   = 1253598,
    SPELL_EX_RADIANT_GLORY                    = 458359,
    SPELL_EX_HOLY_FLAMES                      = 406545
};

// Комплекты T35 (12.0, «Luminant Verdict's Vestments») и T36 (12.1, «Radiance of the Consecrated Flame»).
enum PaladinExTierSpells
{
    SPELL_EX_T35_HOLY_2PC                     = 1264844, // Св. шок +15% хила — DBC
    SPELL_EX_T35_HOLY_4PC                     = 1264845, // Св. шок: +20% переноса в маяк (E0)
    SPELL_EX_T35_PROT_2PC                     = 1264846, // Щит праведника +20% — DBC
    SPELL_EX_T35_PROT_4PC                     = 1264847, // Щит праведника -> 1272298
    SPELL_EX_T35_RET_2PC                      = 1264848, // Поджигание +20% — DBC
    SPELL_EX_T35_RET_4PC                      = 1264849, // Приговор 100% / Буря 50% вешают Поджигание
    SPELL_EX_T36_HOLY_2PC                     = 1296656, // метки 54149 E0/E3 +100 — DBC
    SPELL_EX_T36_HOLY_4PC                     = 1296657, // Правосудие 20% / Св. свет 100% -> Вливание
    SPELL_EX_T36_PROT_2PC                     = 1296658, // E0 30: Освящение +30%; E1 крит по 204242 — DBC
    SPELL_EX_T36_PROT_4PC                     = 1296659, // Правосудие/Молоты/Удар воина Света: +20% Света, крит x2
    SPELL_EX_T36_RET_2PC                      = 1296660, // Цель +10% (DBC) + Божественная сила
    SPELL_EX_T36_RET_4PC                      = 1296661, // Божественный арбитр

    SPELL_EX_LIGHT_BLESSED_SHIELD             = 1272298, // след. Щит мстителя +5%, до 5 стаков
    SPELL_EX_DIVINE_POWER                     = 1305230, // +10% урона Светом, 12 с
    SPELL_EX_DIVINE_ARBITER_FOR_STORM         = 1306161, // «след. Буря и Молот Света»
    SPELL_EX_DIVINE_ARBITER_FOR_VERDICT       = 1306162, // «след. Приговор и Молот Света»
    SPELL_EX_DIVINE_ARBITER_FOR_STORM_ALT     = 1310461, // тот же текст, что 1306161
    SPELL_EX_DIVINE_ARBITER_DAMAGE            = 1306923, // 1012.5% AP в цель + 472.5% AP в 8 м
    SPELL_EX_DIVINE_PURPOSE_BUFF              = 223819, // Цель Света (15%)
    SPELL_EX_DIVINE_PURPOSE_BUFF_RET          = 408458, // Цель Воздаяния/Защиты (10%, прок 408459)
    SPELL_EX_DIVINE_POWER_STORM               = 1306159, // «Божественная сила: Буря» (+200% Бури) — справочно
    SPELL_EX_DIVINE_STORM_DAMAGE              = 224239,
    SPELL_EX_DIVINE_STORM_ALT                 = 423593,
    SPELL_EX_TEMPLARS_VERDICT_DAMAGE          = 224266,
    SPELL_EX_AVENGERS_SHIELD                  = 31935,
    SPELL_EX_SHIELD_OF_THE_RIGHTEOUS          = 53600,
    SPELL_EX_HOLY_LIGHT                       = 82326,
    SPELL_EX_HOLY_SHOCK_HEAL                  = 25914,
    SPELL_EX_SAVED_BY_THE_LIGHT               = 157047, // E1 = +300% по недост. здоровью, E2 = 9 с на цель
    SPELL_EX_SAVED_BY_THE_LIGHT_ABSORB        = 157128,
    SPELL_EX_BEACON_OF_LIGHT                  = 53563,
    SPELL_EX_BEACON_OF_LIGHT_HEAL             = 53652,
    SPELL_EX_LIGHTS_BEACON                    = 53651,  // прок-аура переноса на паладине
    SPELL_EX_BEACON_OF_FAITH                  = 156910, // второй маяк, E3 (Dummy 30) = -30% переноса
    SPELL_EX_BEACON_OF_VIRTUE                 = 200025  // маяк на 5 целей, 9 с
};

namespace
{
    // Маяки паладина: 53563 (одна цель), 156910 Маяк веры (одна цель), 200025 Маяк добродетели (до 5).
    [[nodiscard]] bool IsPaladinBeaconOfEx(Unit const* unit, ObjectGuid const& casterGuid)
    {
        return unit->HasAura(SPELL_EX_BEACON_OF_LIGHT, casterGuid)
            || unit->HasAura(SPELL_EX_BEACON_OF_FAITH, casterGuid)
            || unit->HasAura(SPELL_EX_BEACON_OF_VIRTUE, casterGuid);
    }

    // Все живые цели с маяками этого паладина (без дублей).
    [[nodiscard]] std::vector<Unit*> CollectPaladinBeaconsEx(Unit* caster)
    {
        std::vector<Unit*> result;
        auto add = [&](Unit* unit)
        {
            if (!unit || !unit->IsAlive() || !unit->IsInMap(caster))
                return;
            if (!IsPaladinBeaconOfEx(unit, caster->GetGUID()))
                return;
            if (std::find(result.begin(), result.end(), unit) == result.end())
                result.push_back(unit);
        };

        for (Aura* aura : caster->GetSingleCastAuras())
        {
            std::vector<AuraApplication*> applications;
            aura->GetApplicationVector(applications);
            for (AuraApplication const* app : applications)
                add(app->GetTarget());
        }

        add(caster);
        if (Player* player = caster->ToPlayer())
            if (Group* group = player->GetGroup())
                for (GroupReference const& ref : group->GetMembers())
                    add(ref.GetSource());
        return result;
    }

    // «Сбросить откат» спеллу на зарядах: ResetCooldown в TC заряды не трогает.
    // У Клинка правосудия в данных 1 заряд (2 с Улучшенным Клинком 403745), поэтому
    // без RestoreCharge сброс от Искусства войны / Праведной причины не работал.
    void ResetSpellOrChargeEx(Unit* unit, uint32 spellId)
    {
        if (!unit)
            return;
        SpellHistory* history = unit->GetSpellHistory();
        history->ResetCooldown(spellId, true);
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
            if (info->ChargeCategoryId)
                history->RestoreCharge(info->ChargeCategoryId);
    }

    [[nodiscard]] Optional<int32> GetHolyPowerCost(Spell const* spell)
    {
        if (!spell)
            return std::nullopt;
        return spell->GetPowerTypeCostAmount(POWER_HOLY_POWER);
    }

    // 1253598: скорость = стаки * эффект 0 (2%), потолок — эффект 1 (20%). Стак 1 при входе в Гнев.
    struct CrusadeState
    {
        int32 Stacks = 0;
        int32 Fallback = 0;
    };

    std::unordered_map<ObjectGuid, CrusadeState> CrusadeByCaster;

    [[nodiscard]] bool HasCrusadeTalent(Unit* unit)
    {
        return unit && (unit->HasAura(SPELL_EX_CRUSADE_TALENT) || unit->HasSpell(SPELL_EX_CRUSADE_TALENT));
    }

    [[nodiscard]] bool HasAvengingWrathAura(Unit* unit)
    {
        return unit && (unit->HasAura(SPELL_EX_AVENGING_WRATH) || unit->HasAura(SPELL_EX_AVENGING_WRATH_8S));
    }

    void CrusadeRates(Unit* unit, int32& perPoint, int32& cap)
    {
        perPoint = 2;
        cap = 20;
        if (!unit)
            return;
        if (AuraEffect const* per = unit->GetAuraEffect(SPELL_EX_CRUSADE_TALENT, EFFECT_0))
            if (per->GetAmount() > 0.0)
                perPoint = int32(per->GetAmount());
        if (AuraEffect const* capEff = unit->GetAuraEffect(SPELL_EX_CRUSADE_TALENT, EFFECT_1))
            if (capEff->GetAmount() > 0.0)
                cap = int32(capEff->GetAmount());
        if (perPoint <= 0)
            perPoint = 2;
        if (cap < perPoint)
            cap = perPoint;
    }

    [[nodiscard]] int32 CrusadeHastePercent(Unit* unit, int32 stacks)
    {
        int32 perPoint = 2;
        int32 cap = 20;
        CrusadeRates(unit, perPoint, cap);
        int32 const maxStacks = std::max(1, cap / perPoint);
        stacks = std::clamp(stacks, 0, maxStacks);
        return std::min(cap, stacks * perPoint);
    }

    void ApplyFallbackHaste(Unit* unit, int32 percent)
    {
        if (!unit)
            return;
        CrusadeState& state = CrusadeByCaster[unit->GetGUID()];
        if (state.Fallback == percent)
            return;

        auto apply = [&](int32 value, bool on)
        {
            if (!value)
                return;
            unit->ApplyCastTimePercentMod(float(value), on);
            for (uint8 att = BASE_ATTACK; att < MAX_ATTACK; ++att)
                unit->ApplyAttackTimePercentMod(WeaponAttackType(att), float(value), on);
        };

        apply(state.Fallback, false);
        state.Fallback = percent;
        apply(percent, true);
    }

    // Эффект 10 Гнева (аура 193, в DBC 3%) — слот Крестового похода. Без таланта его гасим.
    bool SetCrusadeAuraHaste(Unit* unit, uint32 spellId, int32 percent)
    {
        if (!unit)
            return false;
        Aura* aura = unit->GetAura(spellId);
        if (!aura)
            return false;
        AuraEffect* haste = aura->GetEffect(EFFECT_10);
        if (!haste || haste->GetAuraType() != SPELL_AURA_MELEE_SLOW)
            return false;
        haste->SetCanBeRecalculated(false);
        if (int32(haste->GetAmount()) != percent)
            haste->ChangeAmount(percent);
        return true;
    }

    void UpdateCrusadeHaste(Unit* unit)
    {
        if (!unit)
            return;
        CrusadeState& state = CrusadeByCaster[unit->GetGUID()];
        int32 const percent = HasCrusadeTalent(unit) ? CrusadeHastePercent(unit, state.Stacks) : 0;
        bool onAura = SetCrusadeAuraHaste(unit, SPELL_EX_AVENGING_WRATH, percent);
        onAura = SetCrusadeAuraHaste(unit, SPELL_EX_AVENGING_WRATH_8S, percent) || onAura;
        if (onAura)
            ApplyFallbackHaste(unit, 0);
        else if (HasCrusadeTalent(unit) && HasAvengingWrathAura(unit))
            ApplyFallbackHaste(unit, percent);
        else
            ApplyFallbackHaste(unit, 0);
    }

    void ClearCrusadeState(Unit* unit)
    {
        if (!unit)
            return;
        ApplyFallbackHaste(unit, 0);
        CrusadeByCaster.erase(unit->GetGUID());
    }

    [[nodiscard]] bool IsProtectionPaladinEx(Unit* unit)
    {
        if (!unit)
            return false;
        if (unit->HasAura(SPELL_EX_SPEC_PROT))
            return true;
        if (Player* player = unit->ToPlayer())
            return player->GetPrimarySpecialization() == ChrSpecialization::PaladinProtection;
        return false;
    }

    [[nodiscard]] bool IsHolyPaladinEx(Unit* unit)
    {
        if (!unit)
            return false;
        if (unit->HasAura(SPELL_EX_SPEC_HOLY) || unit->HasAura(SPELL_EX_GREATER_JUDGMENT_HOLY))
            return true;
        if (Player* player = unit->ToPlayer())
            return player->GetPrimarySpecialization() == ChrSpecialization::PaladinHoly;
        return false;
    }

    // 231644 (Свет): «предотвращает следующие (SP * 483%) * (1 + Универсальность) урона цели».
    // Вливание света (54149, E3 = 250 → x2.5; с T36 2pc 350 → x3.5) увеличивает поглощение. PvP-множитель 0.3714.
    // Наложения суммируются («Multiple applications may overlap»), длительность 18 с (12.1.0).
    void ApplyUnworthyEx(Unit* caster, Unit* target, float infusionMult, Spell const* triggering)
    {
        if (!caster || !target)
            return;

        float pct = 483.f;
        if (AuraEffect const* talent = caster->GetAuraEffect(SPELL_EX_GREATER_JUDGMENT_HOLY, EFFECT_0))
            if (talent->GetAmount() > 0)
                pct = float(talent->GetAmount());

        float amount = caster->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_HOLY) * pct / 100.f;
        if (Player* player = caster->ToPlayer())
            AddPct(amount, player->GetRatingBonusValue(CR_VERSATILITY_DAMAGE_DONE)
                + player->GetTotalAuraModifier(SPELL_AURA_MOD_VERSATILITY));
        if (infusionMult > 1.f)
            amount *= infusionMult;
        if (target->GetAffectingPlayer())
            amount *= 0.3714f;
        if (amount < 1.f)
            return;

        Aura* debuff = target->GetAura(SPELL_EX_UNWORTHY, caster->GetGUID());
        int32 previous = 0;
        if (debuff)
        {
            if (AuraEffect const* eff = debuff->GetEffect(EFFECT_0))
                previous = eff->GetAmount();
        }
        else
        {
            caster->CastSpell(target, SPELL_EX_UNWORTHY, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_FULL_MASK,
                .TriggeringSpell = triggering
            });
            debuff = target->GetAura(SPELL_EX_UNWORTHY, caster->GetGUID());
        }
        if (!debuff)
            return;

        if (AuraEffect* eff = debuff->GetEffect(EFFECT_0))
        {
            eff->SetCanBeRecalculated(false);
            int64 const total = int64(previous) + int64(amount);
            eff->ChangeAmount(int32(std::min<int64>(total, std::numeric_limits<int32>::max())));
        }

        constexpr int32 dur = 18000;
        if (debuff->GetMaxDuration() < dur)
            debuff->SetMaxDuration(dur);
        debuff->SetDuration(dur);
    }

    // Тултип 231663: Свет/Воздаяние 20%, Защита 50%. В эффекте таланта лежит только 20.
    [[nodiscard]] int32 GreaterJudgmentBonus(Unit* caster)
    {
        if (IsProtectionPaladinEx(caster))
            return 50;
        int32 bonus = 20;
        if (caster)
            if (AuraEffect const* talent = caster->GetAuraEffect(SPELL_EX_GREATER_JUDGMENT, EFFECT_0))
                if (talent->GetAmount() > 0.0)
                    bonus = int32(talent->GetAmount());
        return bonus > 0 ? bonus : 20;
    }

    struct ExecutionSentenceState
    {
        ObjectGuid Target;
        std::unordered_set<ObjectGuid> Marked;
        uint64 Damage = 0;
    };

    std::unordered_map<ObjectGuid, ExecutionSentenceState> ExecutionSentenceByCaster;
    std::unordered_map<ObjectGuid, uint32> ConsecratedBladeAt;

    void NoteExecutionSentenceDamage(Unit* attacker, Unit* victim, uint32 damage, SpellInfo const* spellInfo)
    {
        if (!attacker || !victim || !damage)
            return;
        if (spellInfo && (spellInfo->Id == SPELL_EX_EXECUTION_SENTENCE || spellInfo->Id == SPELL_EX_EXECUTION_SENTENCE_PAYOFF))
            return;
        if (spellInfo && !(spellInfo->GetSchoolMask() & SPELL_SCHOOL_MASK_HOLY))
            return;

        auto it = ExecutionSentenceByCaster.find(attacker->GetGUID());
        if (it == ExecutionSentenceByCaster.end())
            return;
        // Метка взрыва 1260251 на цели — как в ретейле. Список с OnApply — запасной путь.
        if (!victim->HasAura(SPELL_EX_EXECUTION_SENTENCE_DAMAGE, attacker->GetGUID())
            && it->second.Marked.find(victim->GetGUID()) == it->second.Marked.end())
            return;
        it->second.Damage += damage;
    }

    // Множитель передаётся в каст Бури/Света зари. 1.25 = +25%, 0.30 = 30% эффективности.
    struct EmpyreanLegacyMod
    {
        float Multiplier = 1.f;
    };

    void ApplyEmpyreanLegacyMod(Spell const* spell, float& pctMod)
    {
        EmpyreanLegacyMod const* mod = std::any_cast<EmpyreanLegacyMod>(&spell->m_customArg);
        if (!mod || mod->Multiplier <= 0.f)
            return;

        pctMod *= mod->Multiplier;
    }

    // Каст Бури/Света зари, выписанный Наследием Эмпиреев (бесплатный, триггерный):
    // не тратит и не выпускает Божественного арбитра, не считается тратой Цели.
    [[nodiscard]] bool IsEmpyreanLegacyCast(Spell const* spell)
    {
        return spell && std::any_cast<EmpyreanLegacyMod>(&spell->m_customArg) != nullptr;
    }

    // 125 = +25% (талант 387170). 30 = 30% эффективности (талант 1241358). Порог 100 их различает.
    void GrantEmpyreanLegacy(Unit* caster, Spell const* triggering, int32 storedPct)
    {
        caster->CastSpell(caster, SPELL_EX_EMPIREAN_LEGACY_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = triggering
        });
        if (AuraEffect* ready = caster->GetAuraEffect(SPELL_EX_EMPIREAN_LEGACY_READY, EFFECT_0))
        {
            ready->SetCanBeRecalculated(false);
            ready->ChangeAmount(storedPct);
        }
    }
}

// 406064 - Искусство войны: автоатаки с шансом 15% (25% при крите)
// сбрасывают КД Клинка правосудия и усиливают следующий Клинок (406086).
class spell_pal_art_of_war_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_BLADE_OF_JUSTICE, SPELL_EX_ART_OF_WAR_READY });
    }

    bool CheckProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        // только автоатаки (в т.ч. Крещендо ударов — замена автоатак)
        if (!(eventInfo.GetTypeMask() & PROC_FLAG_DEAL_MELEE_SWING))
            return false;

        // Тултип: 15%, крит «увеличивает шанс ещё на 10%» — пункты, то есть 25%, не 16.5%.
        float chance = aurEff->GetAmount();
        if (eventInfo.GetHitMask() & PROC_HIT_CRITICAL)
            if (AuraEffect const* critBonus = GetEffect(EFFECT_1))
                chance += critBonus->GetAmount();

        return roll_chance(chance);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        ResetSpellOrChargeEx(GetTarget(), SPELL_EX_BLADE_OF_JUSTICE);
        GetTarget()->CastSpell(GetTarget(), SPELL_EX_ART_OF_WAR_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = eventInfo.GetProcSpell()
        });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_art_of_war_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_art_of_war_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 402912 - Праведная причина: каждая трата Сила Света с шансом 6% за очко
// сбрасывает КД Клинка правосудия и усиливает следующий Клинок (402916).
// 12.1.5.69952: эффект 0 — PROC_TRIGGER_SPELL (триггер 0), не DUMMY.
// Хук на DUMMY не вызывался, поэтому сброс КД не работал.
class spell_pal_righteous_cause_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_BLADE_OF_JUSTICE, SPELL_EX_RIGHTEOUS_CAUSE_READY });
    }

    bool CheckProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        Optional<int32> holyPowerSpent = GetHolyPowerCost(eventInfo.GetProcSpell());
        if (!holyPowerSpent || *holyPowerSpent <= 0)
            return false;

        for (int32 i = 0; i < *holyPowerSpent; ++i)
            if (roll_chance(aurEff->GetAmount()))
                return true;
        return false;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        ResetSpellOrChargeEx(GetTarget(), SPELL_EX_BLADE_OF_JUSTICE);
        GetTarget()->CastSpell(GetTarget(), SPELL_EX_RIGHTEOUS_CAUSE_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = eventInfo.GetProcSpell()
        });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_righteous_cause_ex::CheckProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
        OnEffectProc += AuraEffectProcFn(spell_pal_righteous_cause_ex::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 326732 - Сила небес: Удар крестоносца/Удары храмовника (15%) и Крещендо ударов (5%)
// делают следующую Бурю света бесплатной и на 15% сильнее (326733).
class spell_pal_empyrean_power_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EMPYREAN_POWER_READY });
    }

    bool CheckProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo)
            return false;

        switch (spellInfo->Id)
        {
            case SPELL_EX_CRUSADER_STRIKE:
            case SPELL_EX_TEMPLAR_STRIKE:
            case SPELL_EX_TEMPLAR_SLASH:
                return roll_chance(aurEff->GetAmount());
            case SPELL_EX_CRUSADING_STRIKE:
                if (AuraEffect const* reducedChance = GetEffect(EFFECT_1))
                    return roll_chance(reducedChance->GetAmount());
                [[fallthrough]];
            default:
                return false;
        }
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_EX_EMPYREAN_POWER_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = eventInfo.GetProcSpell()
        });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_empyrean_power_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_empyrean_power_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 231663 - Великое правосудие: Правосудие вешает 197277.
// Каждое наложение добавляет эффект 0 таланта (20%) и обновляет 18 с.
// Трата — spell_pal_greater_judgment_consume_ex, одно наложение за удар.
class spell_pal_judgment_greater_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_GREATER_JUDGMENT_DEBUFF });
    }

    // 267316 не аура-прок: E0/E1 — модификатор урона, E3 — spell-effect DUMMY, не аура.
    // Старый хук SPELL_AURA_DUMMY никогда не вызывался, поэтому 383921 не бил.
    // Шанс с Wowhead 12.1.5: урон Света = очки * 1.35, прок = этот процент * 0.75
    // (тултип 11.0% урона → 8.3% шанс). Безграничное правосудие 405278 = +50.
    void HandleMasteryBlast()
    {
        Unit* caster = GetCaster();
        Unit* hit = GetHitUnit();
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player || !hit || !player->HasAura(SPELL_EX_MASTERY_RETRIBUTION))
            return;
        if (!sSpellMgr->GetSpellInfo(SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE, DIFFICULTY_NONE))
            return;

        float points = player->GetTotalAuraModifier(SPELL_AURA_MASTERY)
            + player->GetRatingBonusValue(CR_MASTERY);
        if (points <= 0.f)
            return;

        float chance = points * 1.35f * 0.75f;
        if (player->HasAura(SPELL_EX_BOUNDLESS_JUDGMENT))
        {
            float bonus = 50.f;
            if (AuraEffect const* boundless = player->GetAuraEffect(SPELL_EX_BOUNDLESS_JUDGMENT, EFFECT_0))
                bonus = float(boundless->GetAmount());
            chance *= 1.f + bonus / 100.f;
        }
        if (!roll_chance(chance))
            return;

        caster->CastSpell(hit, SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    // Свет: снимок Вливания света до попаданий. Вливание тратится этим Правосудием:
    // +150% к «Недостойному» и +1 Сила Света (54149 E4/E5, серверные эффекты).
    void SnapshotInfusion()
    {
        Unit* caster = GetCaster();
        _holy = IsHolyPaladinEx(caster);
        _infused = false;
        _infusionMult = 1.f;
        if (!_holy)
            return;
        if (Aura const* infusion = caster->GetAura(SPELL_EX_INFUSION_OF_LIGHT))
        {
            _infused = true;
            _infusionMult = 2.5f;
            // E3 (#4) = 250; T36 2pc добавляет +100 через метку.
            if (AuraEffect const* eff = infusion->GetEffect(EFFECT_3))
                if (eff->GetAmount() > 0)
                    _infusionMult = float(eff->GetAmount()) / 100.f;
        }
    }

    void ConsumeInfusion()
    {
        Unit* caster = GetCaster();
        if (!caster || !_holy)
            return;
        if (_infused)
        {
            caster->EnergizeBySpell(caster, GetSpellInfo(), 1, POWER_HOLY_POWER);
            caster->RemoveAurasDueToSpell(SPELL_EX_INFUSION_OF_LIGHT);
        }

        // T36 Holy 4pc: Правосудие с шансом 20% (E0) даёт Вливание света — после траты старого.
        if (AuraEffect const* tier = caster->GetAuraEffect(SPELL_EX_T36_HOLY_4PC, EFFECT_0))
            if (roll_chance(float(tier->GetAmount())))
                caster->CastSpell(caster, SPELL_EX_INFUSION_OF_LIGHT, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_FULL_MASK,
                    .TriggeringSpell = GetSpell()
                });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* hit = GetHitUnit();
        if (!caster || !hit)
            return;

        // Свет: Великое правосудие = поглощение «Недостойный», а не +20% от 197277.
        if (_holy)
        {
            if (caster->HasAura(SPELL_EX_GREATER_JUDGMENT_HOLY) || caster->HasSpell(SPELL_EX_GREATER_JUDGMENT_HOLY)
                || caster->HasAura(SPELL_EX_GREATER_JUDGMENT) || caster->HasSpell(SPELL_EX_GREATER_JUDGMENT))
                if (caster->IsValidAttackTarget(hit))
                    ApplyUnworthyEx(caster, hit, _infusionMult, GetSpell());
            return;
        }

        // Талант — пассив. HasAura иногда пуст (аура не села), HasSpell надёжнее.
        if (!caster->HasAura(SPELL_EX_GREATER_JUDGMENT) && !caster->HasSpell(SPELL_EX_GREATER_JUDGMENT))
            return;

        int32 const bonus = GreaterJudgmentBonus(caster);
        Aura* debuff = hit->GetAura(SPELL_EX_GREATER_JUDGMENT_DEBUFF, caster->GetGUID());
        bool const fresh = debuff == nullptr;
        if (!debuff)
        {
            caster->CastSpell(hit, SPELL_EX_GREATER_JUDGMENT_DEBUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
            debuff = hit->GetAura(SPELL_EX_GREATER_JUDGMENT_DEBUFF, caster->GetGUID());
        }

        if (!debuff)
            return;

        if (AuraEffect* bonusEff = debuff->GetEffect(EFFECT_0))
        {
            bonusEff->SetCanBeRecalculated(false);
            int32 const amount = fresh ? bonus : int32(bonusEff->GetAmount()) + bonus;
            bonusEff->ChangeAmount(amount);
        }

        constexpr int32 dur = 18000;
        if (debuff->GetMaxDuration() < dur)
            debuff->SetMaxDuration(dur);
        debuff->SetDuration(dur);
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_pal_judgment_greater_ex::SnapshotInfusion);
        AfterHit += SpellHitFn(spell_pal_judgment_greater_ex::HandleMasteryBlast);
        AfterHit += SpellHitFn(spell_pal_judgment_greater_ex::HandleHitTarget);
        AfterCast += SpellCastFn(spell_pal_judgment_greater_ex::ConsumeInfusion);
    }

    bool _holy = false;
    bool _infused = false;
    float _infusionMult = 1.f;
};

// 197277 снимается одним наложением за удар способности, которую усиливает эффект 0.
// Не вешать на само Правосудие: оно только накладывает дебафф.
class spell_pal_greater_judgment_consume_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_GREATER_JUDGMENT_DEBUFF });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* hit = GetHitUnit();
        if (!caster || !hit || (!caster->HasAura(SPELL_EX_GREATER_JUDGMENT) && !caster->HasSpell(SPELL_EX_GREATER_JUDGMENT)))
            return;
        if (!_consumed.insert(hit->GetGUID()).second)
            return;

        Aura* debuff = hit->GetAura(SPELL_EX_GREATER_JUDGMENT_DEBUFF, caster->GetGUID());
        if (!debuff)
            return;

        int32 const bonus = GreaterJudgmentBonus(caster);
        AuraEffect* bonusEff = debuff->GetEffect(EFFECT_0);
        if (!bonusEff)
        {
            debuff->Remove();
            return;
        }

        double const next = bonusEff->GetAmount() - bonus;
        if (next < bonus * 0.5)
            debuff->Remove();
        else
            bonusEff->ChangeAmount(next);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_greater_judgment_consume_ex::HandleAfterHit);
    }

    std::unordered_set<ObjectGuid> _consumed;
};

// 267316 - Мастерство: Правосудие Верховного лорда.
// Пассивный бонус урона — DBC (E0/E1, коэф. 1.35). Удар 383921 кастует
// spell_pal_judgment_greater_ex: у 267316 нет ауры-прока, хук DUMMY не срабатывал.
class spell_pal_highlords_judgment_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE });
    }

    void Register() override { }
};

// 406157 - Судья, присяжные и палач: гейтим дефолт-триггер E1 (1253174) так,
// чтобы бафф выдавался только после Приговора (343527). +5% урона — DBC.
class spell_pal_judge_jury_executioner_ex : public AuraScript
{
    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        return spellInfo && spellInfo->Id == SPELL_EX_EXECUTION_SENTENCE;
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_judge_jury_executioner_ex::CheckProc, EFFECT_1, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 1253174 - Судья, присяжные и палач (бафф): следующая трата Сила Света
// возвращает её стоимость (1253175) и consumes стак.
class spell_pal_judge_jury_executioner_refund_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_JJE_REFUND });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        Optional<int32> holyPowerSpent = GetHolyPowerCost(eventInfo.GetProcSpell());
        return holyPowerSpent && *holyPowerSpent > 0;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        Unit* target = GetTarget();
        int32 holyPowerSpent = *GetHolyPowerCost(eventInfo.GetProcSpell());

        target->CastSpell(target, SPELL_EX_JJE_REFUND, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR, eventInfo.GetProcSpell(), SPELLVALUE_BASE_POINT0, holyPowerSpent));

        if (Aura* aura = GetAura())
            aura->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_judge_jury_executioner_refund_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_judge_jury_executioner_refund_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 1261111 - Свет внутри (урон): в АН Финальный приговор/Приговор храмовника и
// Буря света наносят на 10% больше урона.
class spell_pal_light_within_damage_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_LIGHT_WITHIN_DAMAGE, SPELL_EX_AVENGING_WRATH });
    }

    void CalculateDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (!caster->HasAura(SPELL_EX_LIGHT_WITHIN_DAMAGE))
            return;
        if (!caster->HasAura(SPELL_EX_AVENGING_WRATH) && !caster->HasAura(SPELL_EX_CRUSADE)
            && !caster->HasAura(SPELL_EX_AVENGING_WRATH_8S))
            return;

        if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX_LIGHT_WITHIN_DAMAGE, EFFECT_0))
            AddPct(pctMod, bonus->GetAmount());
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_light_within_damage_ex::CalculateDamage);
    }
};

// 1261159 - Свет внутри (Клинок): усиленный Клинок правосудия (с 406086/402916)
// выпускает волну Света (1261160, конус) и потребляет усиление.
class spell_pal_light_within_blade_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_LIGHT_WITHIN_WAVE, SPELL_EX_ART_OF_WAR_READY, SPELL_EX_RIGHTEOUS_CAUSE_READY });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_LIGHT_WITHIN_BLADE))
            return;

        bool artOfWar = caster->HasAura(SPELL_EX_ART_OF_WAR_READY);
        bool righteousCause = caster->HasAura(SPELL_EX_RIGHTEOUS_CAUSE_READY);
        if (!artOfWar && !righteousCause)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX_LIGHT_WITHIN_WAVE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        if (artOfWar)
            if (Aura* ready = caster->GetAura(SPELL_EX_ART_OF_WAR_READY))
                ready->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
        if (righteousCause)
            if (Aura* ready = caster->GetAura(SPELL_EX_RIGHTEOUS_CAUSE_READY))
                ready->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_light_within_blade_ex::HandleHitTarget);
    }
};

// 387170 - старый бинд. Крит Правосудия по цели <35% — механика Dragonflight, в 12.1 её нет.
// Прок глушим: выдачу баффа делает spell_pal_empyrean_legacy_aw_ex, трату — spend_ex.
// Строку spell_proc на 387170 SQL удаляет, иначе ядро само повесит 387178 не на то событие.
class spell_pal_empyrean_legacy_ex : public AuraScript
{
    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/) const
    {
        return false;
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_empyrean_legacy_ex::CheckProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 387170 - Гнев карателя (31884 / 8-сек 454351) вешает 387178.
// Крестовый поход 231895 тултип не называет, simc тоже не вешает бафф с него.
class spell_pal_empyrean_legacy_aw_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EMPIREAN_LEGACY, SPELL_EX_EMPIREAN_LEGACY_READY });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_EMPIREAN_LEGACY))
            return;

        int32 stored = 125;
        if (AuraEffect const* pct = caster->GetAuraEffect(SPELL_EX_EMPIREAN_LEGACY, EFFECT_1))
            if (pct->GetAmount() > 0.0)
                stored = 100 + int32(pct->GetAmount());

        GrantEmpyreanLegacy(caster, GetSpell(), stored);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_empyrean_legacy_aw_ex::HandleAfterCast);
    }
};

// 1241358 - крит Правосудия усиливает следующее Торжество. Триггер в DBC пустой.
class spell_pal_empyrean_legacy_judgment_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EMPYREAN_LEGACY_JUDGMENT, SPELL_EX_EMPIREAN_LEGACY_READY });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_EMPYREAN_LEGACY_JUDGMENT) || !IsHitCrit())
            return;

        int32 stored = 30;
        if (AuraEffect const* pct = caster->GetAuraEffect(SPELL_EX_EMPYREAN_LEGACY_JUDGMENT, EFFECT_1))
            if (pct->GetAmount() > 0.0 && pct->GetAmount() < 100.0)
                stored = int32(pct->GetAmount());

        GrantEmpyreanLegacy(caster, GetSpell(), stored);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_empyrean_legacy_judgment_ex::HandleAfterHit);
    }
};

// 387178 - следующий подходящий спендер кастует Бурю света или Свет зари и съедает бафф.
// 387170: Свет/Защита — Торжество -> Свет зари на +25%. Воздаяние — одиночный урон СС -> Буря на +25%.
// 1241358: Торжество -> Свет зари на 30% эффективности, не +30%.
class spell_pal_empyrean_legacy_spend_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EMPIREAN_LEGACY_READY, SPELL_EX_DIVINE_STORM, SPELL_EX_LIGHT_OF_DAWN });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Spell const* spell = GetSpell();
        if (!caster || !spell || spell->IsTriggered() || !caster->HasAura(SPELL_EX_EMPIREAN_LEGACY_READY))
            return;

        AuraEffect const* ready = caster->GetAuraEffect(SPELL_EX_EMPIREAN_LEGACY_READY, EFFECT_0);
        if (!ready)
            return;

        // Выдача пишет 125 (+25%) или 30 (30% эффективности). Сырые 25 из DBC сюда не доходят.
        double const stored = ready->GetAmount();
        bool const judgmentVersion = stored < 100.0;
        float const multiplier = stored > 0.0 ? float(stored / 100.0) : 1.25f;

        uint32 const spellId = GetSpellInfo()->Id;
        bool const wordOfGlory = spellId == SPELL_EX_WORD_OF_GLORY || spellId == SPELL_EX_ETERNAL_FLAME;
        bool const retSpender = spellId == SPELL_EX_TEMPLARS_VERDICT || spellId == SPELL_EX_FINAL_VERDICT || spellId == SPELL_EX_HAMMER_OF_LIGHT;

        uint32 followUp = 0;
        if (wordOfGlory && judgmentVersion)
            followUp = SPELL_EX_LIGHT_OF_DAWN;
        else if (!judgmentVersion && wordOfGlory && (caster->HasAura(SPELL_EX_SPEC_HOLY) || caster->HasAura(SPELL_EX_SPEC_PROT)))
            followUp = SPELL_EX_LIGHT_OF_DAWN;
        else if (!judgmentVersion && retSpender && caster->HasAura(SPELL_EX_SPEC_RET))
            followUp = SPELL_EX_DIVINE_STORM;

        if (!followUp || multiplier <= 0.f)
            return;

        caster->RemoveAurasDueToSpell(SPELL_EX_EMPIREAN_LEGACY_READY);

        CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR);
        args.TriggeringSpell = spell;
        args.CustomArg = EmpyreanLegacyMod{ multiplier };
        caster->CastSpell(caster, followUp, args);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_empyrean_legacy_spend_ex::HandleAfterCast);
    }
};

// Множитель только у каста, который выписал spend_ex. Обычная Буря/Свет зари не усиливаются.
class spell_pal_empyrean_legacy_mod_ex : public SpellScript
{
    void CalculateDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        ApplyEmpyreanLegacyMod(GetSpell(), pctMod);
    }

    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        ApplyEmpyreanLegacyMod(GetSpell(), pctMod);
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_empyrean_legacy_mod_ex::CalculateDamage);
        CalcHealing += SpellCalcHealingFn(spell_pal_empyrean_legacy_mod_ex::CalculateHealing);
    }
};

// 1263782 - Выйти на свет, ветка Воздаяния: Гнев карателя даёт Благословение Ан'ше и 2 ед. энергии Света.
// Святая ветка (Прилив Света чаще) здесь не скриптуется: прок 53576 живёт в DBC, множитель шанса не выдумываем.
class spell_pal_walk_into_light_aw_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_WALK_INTO_LIGHT, SPELL_EX_BLESSING_OF_ANSHE_RET });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_WALK_INTO_LIGHT) || !caster->HasAura(SPELL_EX_SPEC_RET))
            return;

        if (AuraEffect const* chance = caster->GetAuraEffect(SPELL_EX_WALK_INTO_LIGHT, EFFECT_0))
        {
            if (!roll_chance(chance->GetAmount()))
                return;
        }

        caster->CastSpell(caster, SPELL_EX_BLESSING_OF_ANSHE_RET, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        int32 holyPower = 2;
        if (AuraEffect const* hp = caster->GetAuraEffect(SPELL_EX_WALK_INTO_LIGHT, EFFECT_1))
            holyPower = hp->GetAmount();
        if (holyPower <= 0)
            return;

        if (sSpellMgr->GetSpellInfo(SPELL_EX_WALK_INTO_LIGHT_HP, DIFFICULTY_NONE))
        {
            for (int32 i = 0; i < holyPower; ++i)
                caster->CastSpell(caster, SPELL_EX_WALK_INTO_LIGHT_HP, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell()
                });
        }
        else
            caster->ModifyPower(POWER_HOLY_POWER, holyPower);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_walk_into_light_aw_ex::HandleAfterCast);
    }
};

// Во время Гнева карателя Молот гнева также применяет Клинок справедливости на 100%.
class spell_pal_walk_into_light_how_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_WALK_INTO_LIGHT, SPELL_EX_BLADE_OF_JUSTICE });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX_WALK_INTO_LIGHT))
            return;
        if (!caster->HasAura(SPELL_EX_AVENGING_WRATH) && !caster->HasAura(SPELL_EX_AVENGING_WRATH_8S))
            return;

        caster->CastSpell(target, SPELL_EX_BLADE_OF_JUSTICE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_walk_into_light_how_ex::HandleAfterHit);
    }
};

// 1253598 - Крестовый поход: во время Гнева карателя (31884 / 454351) скорость
// равна стакам * эффект 0 таланта (2%), потолок — эффект 1 (20%, 10 стаков).
// Стак 1 при входе в Гнев, плюс стак за каждое очко Силы Света.
// Не стакаем 231895 и не стакаем сам 31884: у Гнева есть слот скорости (эффект 10).
class spell_pal_crusade_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_CRUSADE_TALENT, SPELL_EX_AVENGING_WRATH, SPELL_EX_AVENGING_WRATH_8S });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        if (!HasAvengingWrathAura(GetTarget()))
            return false;

        Optional<int32> holyPowerSpent = GetHolyPowerCost(eventInfo.GetProcSpell());
        return holyPowerSpent && *holyPowerSpent > 0;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        Unit* unit = GetTarget();
        if (!unit)
            return;

        int32 const spent = *GetHolyPowerCost(eventInfo.GetProcSpell());
        CrusadeState& state = CrusadeByCaster[unit->GetGUID()];
        if (state.Stacks < 1)
            state.Stacks = 1;
        state.Stacks += spent;

        int32 perPoint = 2;
        int32 cap = 20;
        CrusadeRates(unit, perPoint, cap);
        state.Stacks = std::min(state.Stacks, std::max(1, cap / perPoint));
        UpdateCrusadeHaste(unit);
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_crusade_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_crusade_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// Гнев карателя: стартовый стак Крестового похода и снятие запасной скорости.
// Эффект 10 (аура 193) — слот скорости. Эффект 0 нужен, чтобы снять запасную скорость,
// если в DBC этого слота нет.
class spell_pal_crusade_aw_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_AVENGING_WRATH, SPELL_EX_AVENGING_WRATH_8S });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* unit = GetTarget();
        if (!unit)
            return;
        if (!HasCrusadeTalent(unit))
        {
            SetCrusadeAuraHaste(unit, GetId(), 0);
            return;
        }

        CrusadeState& state = CrusadeByCaster[unit->GetGUID()];
        if (state.Stacks < 1)
            state.Stacks = 1;
        UpdateCrusadeHaste(unit);
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* unit = GetTarget();
        if (!unit)
            return;

        uint32 const other = GetId() == SPELL_EX_AVENGING_WRATH ? SPELL_EX_AVENGING_WRATH_8S : SPELL_EX_AVENGING_WRATH;
        if (unit->HasAura(other))
        {
            int32 const percent = HasCrusadeTalent(unit)
                ? CrusadeHastePercent(unit, CrusadeByCaster[unit->GetGUID()].Stacks)
                : 0;
            SetCrusadeAuraHaste(unit, other, percent);
            return;
        }
        ClearCrusadeState(unit);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_pal_crusade_aw_ex::OnApply, EFFECT_10, SPELL_AURA_MELEE_SLOW, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_crusade_aw_ex::OnRemove, EFFECT_10, SPELL_AURA_MELEE_SLOW, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_crusade_aw_ex::OnRemove, EFFECT_0, SPELL_AURA_ADD_PCT_MODIFIER, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_pal_crusade_aw_cast_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;
        if (!HasCrusadeTalent(caster))
        {
            SetCrusadeAuraHaste(caster, GetSpellInfo()->Id, 0);
            return;
        }

        CrusadeState& state = CrusadeByCaster[caster->GetGUID()];
        if (state.Stacks < 1)
            state.Stacks = 1;
        UpdateCrusadeHaste(caster);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_crusade_aw_cast_ex::HandleAfterCast);
    }
};

// 406647 - Темплар Слэш (замах из «Ударов храмовника») всегда критует.
class spell_pal_templar_slash_crit_ex : public SpellScript
{
    void CalcCritChance(Unit const* /*victim*/, float& critChance)
    {
        critChance = 100.0f;
    }

    void Register() override
    {
        OnCalcCritChance += SpellOnCalcCritChanceFn(spell_pal_templar_slash_crit_ex::CalcCritChance);
    }
};

// 458359 - Сияющая слава: Всплеск пепла активирует Авестящий гнев на 8 с (454351).
class spell_pal_radiant_glory_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_RADIANT_GLORY, SPELL_EX_AVENGING_WRATH_8S });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX_RADIANT_GLORY))
            caster->CastSpell(caster, SPELL_EX_AVENGING_WRATH_8S,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_radiant_glory_ex::HandleAfterCast);
    }
};

// 53385 - Буря света: урон полностью только по 5 целям (E1 DUMMY bp=5).
class spell_pal_divine_storm_cap_ex : public SpellScript
{
    void SelectTargets(std::list<WorldObject*>& targets)
    {
        if (targets.size() > 5)
            Trinity::Containers::RandomResize(targets, 5);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pal_divine_storm_cap_ex::SelectTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// 406545 - Пламя света: +5% урона Света по целям с тикающим Поджиганием (383346).
class spell_pal_holy_flames_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EXPURGATION_DOT, SPELL_EX_HOLY_FLAMES });
    }

    void CalculateDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || !victim->HasAura(SPELL_EX_EXPURGATION_DOT, caster->GetGUID()))
            return;

        // Тултип 12.0: +5%. Эффекты 3 и 4 — старый ранговый сплит, их сумма 7% больше не совпадает с описанием.
        AddPct(pctMod, 5);
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_holy_flames_ex::CalculateDamage);
    }
};

// 404834 - Освящённый клинок: Клинок правосудия ставит Освящение в точку цели.
// Не чаще раза в 10 с. Эффект таланта — серверный dummy, сам ничего не кастует.
class spell_pal_consecrated_blade_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_CONSECRATED_BLADE, SPELL_EX_CONSECRATION });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* hit = GetHitUnit();
        if (!caster || !hit)
            return;
        if (!caster->HasSpell(SPELL_EX_CONSECRATED_BLADE) && !caster->HasAura(SPELL_EX_CONSECRATED_BLADE))
            return;

        // Откат 10 с — скрытый дебафф 407475 на паладине. Карта — запасной путь, если его нет в DB2.
        if (caster->HasAura(SPELL_EX_CONSECRATED_BLADE_ICD))
            return;
        uint32 const now = getMSTime();
        uint32& last = ConsecratedBladeAt[caster->GetGUID()];
        if (last && now - last < 10000)
            return;
        last = now;
        if (sSpellMgr->GetSpellInfo(SPELL_EX_CONSECRATED_BLADE_ICD, DIFFICULTY_NONE))
            caster->CastSpell(caster, SPELL_EX_CONSECRATED_BLADE_ICD, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_FULL_MASK,
                .TriggeringSpell = GetSpell()
            });

        caster->CastSpell(hit, SPELL_EX_CONSECRATION, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_consecrated_blade_ex::HandleAfterHit);
    }
};

// 343527 - Приговор: через 10 с цель получает 20% светлого урона,
// нанесённого поражённым взрывом (10 м, включая радиус цели).
class spell_pal_execution_sentence_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EXECUTION_SENTENCE });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetTarget();
        if (!caster || !target)
            return;

        ExecutionSentenceState& state = ExecutionSentenceByCaster[caster->GetGUID()];
        state.Target = target->GetGUID();
        state.Damage = 0;
        state.Marked.clear();
        state.Marked.insert(target->GetGUID());

        float const radius = 10.f + target->GetCombatReach();
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, radius);
        Trinity::UnitListSearcher searcher(target, enemies, check);
        Cell::VisitAllObjects(target, searcher, radius);
        for (Unit* enemy : enemies)
            if (enemy && caster->IsValidAttackTarget(enemy))
                state.Marked.insert(enemy->GetGUID());
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetTarget();
        if (!caster || !target)
            return;

        auto it = ExecutionSentenceByCaster.find(caster->GetGUID());
        if (it == ExecutionSentenceByCaster.end())
            return;

        uint64 const dealt = it->second.Damage;
        ExecutionSentenceByCaster.erase(it);
        // Молот падает только по истечении 10 с, не при рассеивании или смерти.
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;
        if (!dealt || !target->IsAlive())
            return;

        int32 pct = 20;
        if (AuraEffect const* eff = GetEffect(EFFECT_1))
            if (eff->GetAmount() > 0.0)
                pct = int32(eff->GetAmount());

        uint32 const amount = uint32(dealt * uint64(pct) / 100);
        if (!amount)
            return;

        // Через лог урона, иначе удар не видно ни в чате боя, ни в WCL. Id выплаты — 387113,
        // как в ретейл-логах. Не кастуем его: бонусы урона уже внутри накопленной суммы.
        // Стоковый spell_pal_execution_sentence снят в SQL, иначе молот бьёт дважды.
        SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EX_EXECUTION_SENTENCE_PAYOFF, DIFFICULTY_NONE);
        if (!info)
            info = GetSpellInfo();
        SpellNonMeleeDamage log(caster, target, info, { caster->GetCastSpellXSpellVisualId(info), 0 }, SPELL_SCHOOL_MASK_HOLY);
        caster->CalculateSpellDamageTaken(&log, int32(std::min<uint32>(amount, uint32(std::numeric_limits<int32>::max()))), info);
        caster->SendSpellNonMeleeDamageLog(&log);
        caster->DealSpellDamage(&log, false);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_pal_execution_sentence_ex::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_execution_sentence_ex::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// ============================================================================
// Комплекты T35 (12.0) и T36 (12.1). Бонусы-модификаторы (2pc Holy/Prot/Ret T35,
// метки T36 Holy 2pc, +10% шанса Цели T36 Ret 2pc, крит по 204242 T36 Prot 2pc)
// работают из DBC. Здесь — эффекты с пометкой «Server-side script».
// ============================================================================

namespace
{
    [[nodiscard]] Unit* PrimaryEnemyTargetEx(Spell const* spell, Unit* caster)
    {
        if (!caster)
            return nullptr;
        Unit* target = spell ? spell->m_targets.GetUnitTarget() : nullptr;
        if (!target || !caster->IsValidAttackTarget(target))
        {
            target = nullptr;
            if (Player* player = caster->ToPlayer())
                target = player->GetSelectedUnit();
            if (!target || !caster->IsValidAttackTarget(target))
                target = caster->GetVictim();
        }
        return target && caster->IsValidAttackTarget(target) ? target : nullptr;
    }
}

// T36 Ret 2pc / 4pc — траты Силы Света (Приговор, Вердикт, Буря, Молот Света, Слово славы,
// Возмездие поборника). Трата Божественной цели (223819) определяется по m_appliedMods:
// её модификатор стоимости участвовал в касте.
//  * 2pc: трата Цели -> Божественная сила (1305230).
//  * 4pc: трата Цели -> Божественный арбитр, если ни одного арбитра нет:
//    Бурей -> 1306162 (выстрелит следующий Приговор/Молот Света),
//    иначе -> 1306161 (выстрелит следующая Буря/Молот Света).
//    Каст «своего» спендера при баффе -> 1306923 в основную цель, бафф снимается.
class spell_pal_t36_ret_divine_purpose_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_DIVINE_POWER, SPELL_EX_DIVINE_ARBITER_FOR_STORM,
            SPELL_EX_DIVINE_ARBITER_FOR_VERDICT, SPELL_EX_DIVINE_ARBITER_DAMAGE });
    }

    [[nodiscard]] bool IsStorm() const
    {
        return GetSpellInfo()->Id == SPELL_EX_DIVINE_STORM;
    }

    [[nodiscard]] bool IsVerdict() const
    {
        uint32 const id = GetSpellInfo()->Id;
        return id == SPELL_EX_FINAL_VERDICT || id == SPELL_EX_TEMPLARS_VERDICT;
    }

    [[nodiscard]] bool IsHammerOfLight() const
    {
        return GetSpellInfo()->Id == SPELL_EX_HAMMER_OF_LIGHT;
    }

    void Snapshot()
    {
        Unit* caster = GetCaster();
        // Рет-Цель — 408458 (wowhead 12.1: ей модифицируются Приговор/Буря/Арбитр);
        // 223819 — Цель Света, оставлена на случай старых баз.
        _divinePurposeId = SPELL_EX_DIVINE_PURPOSE_BUFF_RET;
        _divinePurpose = caster->GetAura(SPELL_EX_DIVINE_PURPOSE_BUFF_RET);
        if (!_divinePurpose)
        {
            _divinePurposeId = SPELL_EX_DIVINE_PURPOSE_BUFF;
            _divinePurpose = caster->GetAura(SPELL_EX_DIVINE_PURPOSE_BUFF);
        }
        _arbiterBuff = 0;
        if (IsEmpyreanLegacyCast(GetSpell()))
        {
            _divinePurpose = nullptr;
            return;
        }
        if (!caster->HasAura(SPELL_EX_T36_RET_4PC))
            return;

        bool const forStorm = caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM)
            || caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM_ALT);
        bool const forVerdict = caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_VERDICT);
        if ((IsStorm() || IsHammerOfLight()) && forStorm)
            _arbiterBuff = caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM)
                ? SPELL_EX_DIVINE_ARBITER_FOR_STORM : SPELL_EX_DIVINE_ARBITER_FOR_STORM_ALT;
        else if ((IsVerdict() || IsHammerOfLight()) && forVerdict)
            _arbiterBuff = SPELL_EX_DIVINE_ARBITER_FOR_VERDICT;
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        // Выстрел арбитра: сначала снимаем бафф (новый можно получить этим же кастом),
        // затем 1306923 в основную цель. Урон арбитра — из DBC (10.125 AP + 4.725 AP в 8 м,
        // PvP 0.5). Бонус +200%/+100% самой Бури/Приговора — spell_pal_t36_divine_arbiter_bonus_ex.
        if (_arbiterBuff)
        {
            caster->RemoveAurasDueToSpell(_arbiterBuff);
            if (Unit* target = PrimaryEnemyTargetEx(GetSpell(), caster))
                caster->CastSpell(target, SPELL_EX_DIVINE_ARBITER_DAMAGE, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD
                        | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell()
                });
        }

        bool const consumed = _divinePurpose
            && (GetSpell()->m_appliedMods.count(_divinePurpose) != 0
                || !caster->HasAura(_divinePurposeId));
        if (!consumed)
            return;

        if (caster->HasAura(SPELL_EX_T36_RET_2PC))
            caster->CastSpell(caster, SPELL_EX_DIVINE_POWER, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_FULL_MASK,
                .TriggeringSpell = GetSpell()
            });

        if (caster->HasAura(SPELL_EX_T36_RET_4PC)
            && !caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM)
            && !caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM_ALT)
            && !caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_VERDICT))
        {
            uint32 const grant = IsStorm() ? SPELL_EX_DIVINE_ARBITER_FOR_VERDICT : SPELL_EX_DIVINE_ARBITER_FOR_STORM;
            caster->CastSpell(caster, grant, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_FULL_MASK,
                .TriggeringSpell = GetSpell()
            });
        }
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_pal_t36_ret_divine_purpose_ex::Snapshot);
        AfterCast += SpellCastFn(spell_pal_t36_ret_divine_purpose_ex::HandleAfterCast);
    }

    Aura* _divinePurpose = nullptr;
    uint32 _divinePurposeId = 0;
    uint32 _arbiterBuff = 0;
};

// T36 Ret 4pc (1296661), бонусы спендера, выпускающего арбитра (ретейл, поверх БД):
//   E0 = 200: Божественная буря, выпускающая арбитра, наносит +200% урона —
//             это и есть «Божественная сила: Буря» (1306159: +200% урона Бури от заклинателя).
//   E1 = 100: Окончательный приговор / Вердикт тамплиера, выпускающий арбитра, +100%.
//   PvP-множитель 0.5 для обоих. Молот Света бонуса не получает.
// Бафф арбитра ещё висит, пока считается урон (снимается в AfterCast основного спендера),
// поэтому проверяем его прямо в CalcDamage. Ауру 1306159 на цель НЕ вешаем: её 12 с
// усиливали бы и следующие Бури, а на ретейле бонус — только у выпускающего каста.
class spell_pal_t36_divine_arbiter_bonus_ex : public SpellScript
{
    static constexpr float STORM_BONUS_PCT = 200.f;
    static constexpr float VERDICT_BONUS_PCT = 100.f;
    static constexpr float PVP_MULT = 0.5f;

    [[nodiscard]] bool IsStormFamily() const
    {
        uint32 const id = GetSpellInfo()->Id;
        return id == SPELL_EX_DIVINE_STORM || id == SPELL_EX_DIVINE_STORM_DAMAGE || id == SPELL_EX_DIVINE_STORM_ALT;
    }

    void HandleCalcDamage(SpellEffectInfo const& /*spellEffectInfo*/, Unit* victim, int32& /*damage*/, int32& /*flatMod*/, float& pctMod)
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || !caster->HasAura(SPELL_EX_T36_RET_4PC) || IsEmpyreanLegacyCast(GetSpell()))
            return;

        float bonus = 0.f;
        if (IsStormFamily())
        {
            if (caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM) || caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_STORM_ALT))
                bonus = STORM_BONUS_PCT;
        }
        else if (caster->HasAura(SPELL_EX_DIVINE_ARBITER_FOR_VERDICT))
            bonus = VERDICT_BONUS_PCT;

        if (bonus <= 0.f)
            return;
        if (victim->IsControlledByPlayer())
            bonus *= PVP_MULT;
        AddPct(pctMod, bonus);
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_t36_divine_arbiter_bonus_ex::HandleCalcDamage);
    }
};

// 1306159 «Божественная сила: Буря»: если что-то из DBC всё же наложит её на цель,
// эффект обнуляется — бонус считает spell_pal_t36_divine_arbiter_bonus_ex (без двойного учёта
// и без 12-секундного хвоста на последующие Бури).
class spell_pal_t36_divine_power_storm_ex : public AuraScript
{
    void CalcAmount(AuraEffect const* /*aurEff*/, SpellEffectValue& amount, bool& /*canBeRecalculated*/)
    {
        amount = 0;
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_pal_t36_divine_power_storm_ex::CalcAmount, EFFECT_ALL, SPELL_AURA_ANY);
    }
};

// T35 Ret 4pc: Приговор / Вердикт вешают Поджигание (383346) на 100% (E0),
// Божественная буря — на 50% (E1) по каждой цели. Существующий DoT не ослабляется.
class spell_pal_t35_ret_expurgation_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EXPURGATION_DOT });
    }

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        bool const storm = GetSpellInfo()->Id == SPELL_EX_DIVINE_STORM;
        AuraEffect const* tier = caster->GetAuraEffect(SPELL_EX_T35_RET_4PC, storm ? EFFECT_1 : EFFECT_0);
        if (!tier || tier->GetAmount() <= 0)
            return;

        bool const existed = target->HasAura(SPELL_EX_EXPURGATION_DOT, caster->GetGUID());
        caster->CastSpell(target, SPELL_EX_EXPURGATION_DOT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        float const pct = float(tier->GetAmount());
        if (existed || pct >= 100.f)
            return;

        if (Aura* dot = target->GetAura(SPELL_EX_EXPURGATION_DOT, caster->GetGUID()))
            for (AuraEffect* eff : dot->GetAuraEffects())
                if (eff && eff->GetAuraType() == SPELL_AURA_PERIODIC_DAMAGE)
                {
                    eff->SetCanBeRecalculated(false);
                    eff->ChangeAmount(int32(CalculatePct(float(eff->GetAmount()), pct)));
                }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_t35_ret_expurgation_ex::HandleHit);
    }
};

// 53651 - Свет маяка: перенос прямого исцеления на ВСЕ маяки паладина
// (53563 / 156910 Маяк веры / 200025 Маяк добродетели), кроме исцелённой цели.
// Заменяет стоковый spell_pal_light_s_beacon: тот лечит только первый 53563.
// Маяк веры: «оба маяка, но на 30% слабее» (156910 E3 = 30).
class spell_pal_light_s_beacon_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_BEACON_OF_LIGHT_HEAL });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        if (!eventInfo.GetActionTarget() || eventInfo.GetActor() != GetTarget())
            return false;
        HealInfo* healInfo = eventInfo.GetHealInfo();
        if (!healInfo || !healInfo->GetHeal())
            return false;
        // сам перенос (и сплэши через тот же носитель 53652) не переносится повторно
        if (SpellInfo const* spell = healInfo->GetSpellInfo())
            if (spell->Id == SPELL_EX_BEACON_OF_LIGHT_HEAL)
                return false;
        return true;
    }

    void HandleProc(AuraEffect* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        Unit* paladin = GetTarget();
        Unit* healed = eventInfo.GetActionTarget();
        std::vector<Unit*> beacons = CollectPaladinBeaconsEx(paladin);
        beacons.erase(std::remove(beacons.begin(), beacons.end(), healed), beacons.end());
        if (beacons.empty())
            return;

        float pct = float(aurEff->GetAmount());
        bool const faith = std::any_of(beacons.begin(), beacons.end(), [paladin](Unit const* u)
        {
            return u->HasAura(SPELL_EX_BEACON_OF_FAITH, paladin->GetGUID());
        }) || healed->HasAura(SPELL_EX_BEACON_OF_FAITH, paladin->GetGUID());
        if (faith)
        {
            float reduction = 30.f;
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EX_BEACON_OF_FAITH, DIFFICULTY_NONE))
                if (info->GetEffects().size() > EFFECT_3)
                    if (float value = float(info->GetEffect(EFFECT_3).CalcValue()); value > 0.f)
                        reduction = value;
            pct = CalculatePct(pct, 100.f - reduction);
        }

        int32 const heal = int32(CalculatePct(float(eventInfo.GetHealInfo()->GetHeal()), pct));
        if (heal <= 0)
            return;

        for (Unit* beacon : beacons)
        {
            CastSpellExtraArgs args(aurEff);
            args.AddSpellMod(SPELLVALUE_BASE_POINT0, heal);
            paladin->CastSpell(beacon, SPELL_EX_BEACON_OF_LIGHT_HEAL, args);
        }
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pal_light_s_beacon_ex::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_pal_light_s_beacon_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// T35 Holy 4pc: Святой шок (хил 25914) переносит в маяк ещё 20% (E0).
// Базовый перенос делает spell_pal_light_s_beacon_ex (53651); здесь — только добавка.
class spell_pal_t35_holy_beacon_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_BEACON_OF_LIGHT, SPELL_EX_BEACON_OF_LIGHT_HEAL });
    }

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* healed = GetHitUnit();
        if (!caster || !healed || GetHitHeal() <= 0)
            return;

        AuraEffect const* tier = caster->GetAuraEffect(SPELL_EX_T35_HOLY_4PC, EFFECT_0);
        if (!tier || tier->GetAmount() <= 0)
            return;

        int32 const bonus = int32(CalculatePct(float(GetHitHeal()), float(tier->GetAmount())));
        if (bonus <= 0)
            return;

        for (Unit* beacon : CollectPaladinBeaconsEx(caster))
        {
            if (beacon == healed)
                continue;
            caster->CastSpell(beacon, SPELL_EX_BEACON_OF_LIGHT_HEAL,
                MakeSpellArgs(TRIGGERED_FULL_MASK, GetSpell(), SPELLVALUE_BASE_POINT0, bonus));
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_t35_holy_beacon_ex::HandleHit);
    }
};

// T36 Holy 4pc: Свет небес всегда (E1 = 100%) даёт Вливание света.
// Часть с Правосудием (20%) — в spell_pal_judgment_greater_ex::ConsumeInfusion.
class spell_pal_t36_holy_light_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        AuraEffect const* tier = caster ? caster->GetAuraEffect(SPELL_EX_T36_HOLY_4PC, EFFECT_1) : nullptr;
        if (!tier || !roll_chance(float(tier->GetAmount())))
            return;

        caster->CastSpell(caster, SPELL_EX_INFUSION_OF_LIGHT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_t36_holy_light_ex::HandleAfterCast);
    }
};

// T35 Prot 4pc: после Щита праведника следующий Щит мстителя +5% (1272298, до 5 стаков).
// Аура-сет сама не прокает: без строки spell_proc DBC-флаги вешали бы бафф на что попало.
class spell_pal_t35_prot_4pc_ex : public AuraScript
{
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pal_t35_prot_4pc_ex::CheckProc);
    }
};

class spell_pal_t35_prot_sotr_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_LIGHT_BLESSED_SHIELD });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_T35_PROT_4PC))
            return;

        if (Aura* buff = caster->GetAura(SPELL_EX_LIGHT_BLESSED_SHIELD))
        {
            uint32 cap = buff->GetSpellInfo()->StackAmount ? buff->GetSpellInfo()->StackAmount : 5;
            if (buff->GetStackAmount() < cap)
                buff->ModStackAmount(1);
            else
                buff->RefreshDuration();
            return;
        }

        caster->CastSpell(caster, SPELL_EX_LIGHT_BLESSED_SHIELD, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_t35_prot_sotr_ex::HandleAfterCast);
    }
};

// Трата 1272298 Щитом мстителя. Щит летит и отскакивает, поэтому бафф снимается
// через 3 с (после всех попаданий) и только те стаки, что были при касте.
class spell_pal_t35_prot_avengers_shield_ex : public SpellScript
{
    void Snapshot()
    {
        _stacks = 0;
        if (Aura const* buff = GetCaster()->GetAura(SPELL_EX_LIGHT_BLESSED_SHIELD))
            _stacks = buff->GetStackAmount();
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !_stacks)
            return;

        uint16 const stacks = uint16(_stacks);
        caster->m_Events.AddEventAtOffset([caster, stacks]()
        {
            caster->RemoveAuraFromStack(SPELL_EX_LIGHT_BLESSED_SHIELD, ObjectGuid::Empty, AURA_REMOVE_BY_DEFAULT, stacks);
        }, Milliseconds(3000));
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_pal_t35_prot_avengers_shield_ex::Snapshot);
        AfterCast += SpellCastFn(spell_pal_t35_prot_avengers_shield_ex::HandleAfterCast);
    }

    uint8 _stacks = 0;
};

// T36 Prot 2pc: Освящение на 30% больше (E0). Урон 81297 растёт меткой радиуса (E2, DBC),
// а сам areatrigger — только масштабом: SetExtraScaleCurve умножает радиус поиска целей.
class spell_pal_t36_prot_consecration_ex : public AuraScript
{
    void ScaleAreaTriggers()
    {
        if (_scaled)
            return;
        Unit* target = GetTarget();
        AuraEffect const* tier = target->GetAuraEffect(SPELL_EX_T36_PROT_2PC, EFFECT_0);
        if (!tier || tier->GetAmount() <= 0)
        {
            _scaled = true;
            return;
        }

        float const scale = 1.f + float(tier->GetAmount()) / 100.f;
        for (AreaTrigger* at : target->GetAreaTriggers(SPELL_EX_CONSECRATION))
        {
            if (!at || at->GetCasterGuid() != target->GetGUID())
                continue;
            at->SetExtraScaleCurve(scale);
            _scaled = true;
        }
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        ScaleAreaTriggers();
    }

    void OnTick(AuraEffect const* /*aurEff*/)
    {
        ScaleAreaTriggers();
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_t36_prot_consecration_ex::AfterApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_t36_prot_consecration_ex::OnTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }

    bool _scaled = false;
};

// T36 Prot 4pc: цели Правосудия / Благословенного молота / Молота праведника /
// Удара воина Света получают +20% (E0) урона Светом; при крите добавка +100% (E1), то есть 40%.
class spell_pal_t36_prot_4pc_ex : public SpellScript
{
    void HandleHit()
    {
        Unit* caster = GetCaster();
        if (!caster || GetHitDamage() <= 0)
            return;

        AuraEffect const* tier = caster->GetAuraEffect(SPELL_EX_T36_PROT_4PC, EFFECT_0);
        if (!tier || tier->GetAmount() <= 0)
            return;

        float pct = float(tier->GetAmount());
        if (IsHitCrit())
        {
            float critBonus = 100.f;
            if (AuraEffect const* crit = caster->GetAuraEffect(SPELL_EX_T36_PROT_4PC, EFFECT_1))
                critBonus = float(crit->GetAmount());
            AddPct(pct, critBonus);
        }

        int32 const damage = GetHitDamage();
        SetHitDamage(damage + int32(CalculatePct(float(damage), pct)));
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_pal_t36_prot_4pc_ex::HandleHit);
    }
};

// 19750 - Вспышка света: тратит Вливание света (54149), если оно было при касте.
// Стоковая строка spell_proc / скрипт 54149 лежат только в базовом дампе TDB.
class spell_pal_infusion_of_light_fol_ex : public SpellScript
{
    void Snapshot()
    {
        _infused = GetCaster()->HasAura(SPELL_EX_INFUSION_OF_LIGHT);
    }

    void Consume()
    {
        if (_infused)
            GetCaster()->RemoveAurasDueToSpell(SPELL_EX_INFUSION_OF_LIGHT);
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_pal_infusion_of_light_fol_ex::Snapshot);
        AfterCast += SpellCastFn(spell_pal_infusion_of_light_fol_ex::Consume);
    }

    bool _infused = false;
};

// 414022 - «Недостойный»: урон, наносимый носителем, поглощается остатком E0.
class spell_pal_unworthy_tracker : public UnitScript
{
public:
    spell_pal_unworthy_tracker() : UnitScript("spell_pal_unworthy_tracker") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || attacker == victim || !damage)
            return;

        while (damage)
        {
            AuraEffect* eff = attacker->GetAuraEffect(SPELL_EX_UNWORTHY, EFFECT_0);
            if (!eff)
                break;

            int32 const left = eff->GetAmount();
            if (left <= 0)
            {
                eff->GetBase()->Remove(AURA_REMOVE_BY_ENEMY_SPELL);
                continue;
            }

            if (uint32(left) > damage)
            {
                eff->ChangeAmount(left - int32(damage));
                damage = 0;
            }
            else
            {
                damage -= uint32(left);
                eff->GetBase()->Remove(AURA_REMOVE_BY_ENEMY_SPELL);
            }
        }
    }
};

// 157047 - Спасённый Светом (wowhead 12.x): союзник с ВАШИМ маяком (53563/156910/200025)
// получает урон -> паладин вешает ему щит 157128 (22.5% SP, PvP 0.67 — из DBC),
// щит увеличен до +300% (E1) по недостающему здоровью цели. Одна цель — раз в 9 с (E2).
// Прок-аура на паладине ловит только события самого паладина, поэтому урон по союзнику
// отслеживается UnitScript'ом. Старый AuraScript spell_pal_saved_by_the_light_ex удалён.
namespace
{
    std::mutex gSavedByTheLightLock;
    std::unordered_map<ObjectGuid, uint32> gSavedByTheLightNext; // цель -> GameTimeMS разблокировки
}

class spell_pal_saved_by_the_light_tracker : public UnitScript
{
public:
    spell_pal_saved_by_the_light_tracker() : UnitScript("spell_pal_saved_by_the_light_tracker") { }

    static Unit* FindBeaconOwner(Unit* victim)
    {
        for (uint32 beacon : { uint32(SPELL_EX_BEACON_OF_LIGHT), uint32(SPELL_EX_BEACON_OF_FAITH), uint32(SPELL_EX_BEACON_OF_VIRTUE) })
        {
            Unit::AuraApplicationMapBounds range = victim->GetAppliedAuras().equal_range(beacon);
            for (auto itr = range.first; itr != range.second; ++itr)
            {
                Aura const* aura = itr->second->GetBase();
                Unit* owner = ObjectAccessor::GetUnit(*victim, aura->GetCasterGUID());
                if (owner && owner->IsAlive() && owner->HasAura(SPELL_EX_SAVED_BY_THE_LIGHT))
                    return owner;
            }
        }
        return nullptr;
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!victim || !damage || !victim->IsAlive() || attacker == victim)
            return;
        if (damage >= victim->GetHealth())
            return; // смертельный удар — щит уже не поможет
        if (!victim->HasAura(SPELL_EX_BEACON_OF_LIGHT) && !victim->HasAura(SPELL_EX_BEACON_OF_FAITH)
            && !victim->HasAura(SPELL_EX_BEACON_OF_VIRTUE))
            return;

        Unit* paladin = FindBeaconOwner(victim);
        if (!paladin)
            return;

        uint32 lockoutMs = 9000;
        if (AuraEffect const* e = paladin->GetAuraEffect(SPELL_EX_SAVED_BY_THE_LIGHT, EFFECT_2))
            if (e->GetAmount() > 0 && e->GetAmount() <= 120)
                lockoutMs = uint32(e->GetAmount()) * IN_MILLISECONDS;

        uint32 const now = GameTime::GetGameTimeMS();
        {
            std::lock_guard<std::mutex> guard(gSavedByTheLightLock);
            if (gSavedByTheLightNext.size() > 2048)
                std::erase_if(gSavedByTheLightNext, [now](auto const& entry) { return int32(entry.second - now) <= 0; });
            auto itr = gSavedByTheLightNext.find(victim->GetGUID());
            if (itr != gSavedByTheLightNext.end() && int32(itr->second - now) > 0)
                return;
            gSavedByTheLightNext[victim->GetGUID()] = now + lockoutMs;
        }

        float bonusPct = 300.f;
        if (AuraEffect const* e = paladin->GetAuraEffect(SPELL_EX_SAVED_BY_THE_LIGHT, EFFECT_1))
            if (e->GetAmount() > 0 && e->GetAmount() <= 1000)
                bonusPct = float(e->GetAmount());
        float const healthAfter = float(victim->GetHealth() - damage);
        float const missingFrac = std::clamp(1.f - healthAfter / float(std::max<uint64>(victim->GetMaxHealth(), 1)), 0.f, 1.f);

        paladin->CastSpell(victim, SPELL_EX_SAVED_BY_THE_LIGHT_ABSORB, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
        if (AuraEffect* shield = victim->GetAuraEffect(SPELL_EX_SAVED_BY_THE_LIGHT_ABSORB, EFFECT_0, paladin->GetGUID()))
            shield->ChangeAmount(int32(shield->GetAmount() * (1.f + bonusPct / 100.f * missingFrac)));
    }
};

class spell_pal_execution_sentence_tracker : public UnitScript
{
public:
    spell_pal_execution_sentence_tracker() : UnitScript("spell_pal_execution_sentence_tracker") { }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (damage <= 0)
            return;
        NoteExecutionSentenceDamage(attacker, target, uint32(damage), spellInfo);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        NoteExecutionSentenceDamage(attacker, target, damage, nullptr);
    }
};

void AddSC_paladin_spell_scripts_ex()
{
    // Вызывается из AddSC_paladin_spell_scripts_ex2(); защита от двойной регистрации,
    // если этот вызов уже добавлен в spell_script_loader.cpp вручную.
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    RegisterSpellScript(spell_pal_art_of_war_ex);
    RegisterSpellScript(spell_pal_light_s_beacon_ex);
    RegisterSpellScript(spell_pal_righteous_cause_ex);
    RegisterSpellScript(spell_pal_empyrean_power_ex);
    RegisterSpellScript(spell_pal_judgment_greater_ex);
    RegisterSpellScript(spell_pal_greater_judgment_consume_ex);
    RegisterSpellScript(spell_pal_highlords_judgment_ex);
    RegisterSpellScript(spell_pal_judge_jury_executioner_ex);
    RegisterSpellScript(spell_pal_judge_jury_executioner_refund_ex);
    RegisterSpellScript(spell_pal_light_within_damage_ex);
    RegisterSpellScript(spell_pal_light_within_blade_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_aw_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_judgment_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_spend_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_mod_ex);
    RegisterSpellScript(spell_pal_walk_into_light_aw_ex);
    RegisterSpellScript(spell_pal_walk_into_light_how_ex);
    RegisterSpellScript(spell_pal_crusade_ex);
    RegisterSpellScript(spell_pal_crusade_aw_ex);
    RegisterSpellScript(spell_pal_crusade_aw_cast_ex);
    RegisterSpellScript(spell_pal_templar_slash_crit_ex);
    RegisterSpellScript(spell_pal_radiant_glory_ex);
    RegisterSpellScript(spell_pal_divine_storm_cap_ex);
    RegisterSpellScript(spell_pal_holy_flames_ex);
    RegisterSpellScript(spell_pal_consecrated_blade_ex);
    RegisterSpellScript(spell_pal_execution_sentence_ex);
    new spell_pal_execution_sentence_tracker();
    RegisterSpellScript(spell_pal_infusion_of_light_fol_ex);
    new spell_pal_unworthy_tracker();
    new spell_pal_saved_by_the_light_tracker();
    RegisterSpellScript(spell_pal_t36_ret_divine_purpose_ex);
    RegisterSpellScript(spell_pal_t36_divine_arbiter_bonus_ex);
    RegisterSpellScript(spell_pal_t36_divine_power_storm_ex);
    RegisterSpellScript(spell_pal_t35_ret_expurgation_ex);
    RegisterSpellScript(spell_pal_t35_holy_beacon_ex);
    RegisterSpellScript(spell_pal_t36_holy_light_ex);
    RegisterSpellScript(spell_pal_t35_prot_4pc_ex);
    RegisterSpellScript(spell_pal_t35_prot_sotr_ex);
    RegisterSpellScript(spell_pal_t35_prot_avengers_shield_ex);
    RegisterSpellScript(spell_pal_t36_prot_consecration_ex);
    RegisterSpellScript(spell_pal_t36_prot_4pc_ex);
}
