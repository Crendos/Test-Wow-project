// ============================================================================
// Paladin 12.1.0 class fixes — часть 1: Воздаяние (Retribution) + P0-фиксы.
//
// КАК СТАВИТЬ:
//   1) Всё содержимое ниже строки-маркера CUT HERE вставить в КОНЕЦ
//      src/server/scripts/Spells/spell_paladin.cpp ПЕРЕД функцией
//      AddSC_paladin_spell_scripts() (или после неё — до конца файла).
//   2) В src/server/scripts/Spells/spell_script_loader.cpp добавить:
//         void AddSC_paladin_spell_scripts_ex();
//      ...и вызов AddSC_paladin_spell_scripts_ex(); внутри AddSpellsScripts().
//   3) Прогнать paladin/paladin_class_fixes.sql на world-базу.
//
// Все ID проверены по данным клиента 12.1.0 (build 69497/69933).
// ============================================================================

// === CUT HERE ===============================================================

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
    SPELL_EX_CONSECRATION                     = 26573,
    SPELL_EX_EXECUTION_SENTENCE               = 343527,
    SPELL_EX_EXECUTION_SENTENCE_DAMAGE        = 1260251,
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

namespace
{
    [[nodiscard]] bool IsPaladinJudgment(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_EX_JUDGMENT_RET:
            case SPELL_EX_JUDGMENT_PROT:
            case SPELL_EX_JUDGMENT_HOLY:
                return true;
            default:
                return false;
        }
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
    std::unordered_map<ObjectGuid, uint32> CrusadingStrikeHits;
    std::unordered_map<ObjectGuid, uint32> ConsecratedBladeAt;

    void NoteExecutionSentenceDamage(Unit* attacker, Unit* victim, uint32 damage, SpellInfo const* spellInfo)
    {
        if (!attacker || !victim || !damage)
            return;
        if (spellInfo && spellInfo->Id == SPELL_EX_EXECUTION_SENTENCE)
            return;
        if (spellInfo && !(spellInfo->GetSchoolMask() & SPELL_SCHOOL_MASK_HOLY))
            return;

        auto it = ExecutionSentenceByCaster.find(attacker->GetGUID());
        if (it == ExecutionSentenceByCaster.end())
            return;
        if (it->second.Marked.find(victim->GetGUID()) == it->second.Marked.end())
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

// 406064 - Искусство войны: автоатаки с шансом 15% (+10% относительного бонуса за крит)
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

        float chance = aurEff->GetAmount();
        if (eventInfo.GetHitMask() & PROC_HIT_CRITICAL)
            if (AuraEffect const* critBonus = GetEffect(EFFECT_1))
                chance *= 1.f + critBonus->GetAmount() / 100.f;

        return roll_chance(chance);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->GetSpellHistory()->ResetCooldown(SPELL_EX_BLADE_OF_JUSTICE, true);
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
        GetTarget()->GetSpellHistory()->ResetCooldown(SPELL_EX_BLADE_OF_JUSTICE, true);
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

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* hit = GetHitUnit();
        // Талант — пассив. HasAura иногда пуст (аура не села), HasSpell надёжнее.
        if (!caster || !hit || (!caster->HasAura(SPELL_EX_GREATER_JUDGMENT) && !caster->HasSpell(SPELL_EX_GREATER_JUDGMENT)))
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
        AfterHit += SpellHitFn(spell_pal_judgment_greater_ex::HandleMasteryBlast);
        AfterHit += SpellHitFn(spell_pal_judgment_greater_ex::HandleHitTarget);
    }
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

        uint32 const now = getMSTime();
        uint32& last = ConsecratedBladeAt[caster->GetGUID()];
        if (last && now - last < 10000)
            return;
        last = now;

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

// 408385 - Крещендо ударов: 1 ед. Силы Света через удар. Урон — сам спелл.
class spell_pal_crusading_strikes_hp_ex : public SpellScript
{
    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        uint32& hits = CrusadingStrikeHits[caster->GetGUID()];
        ++hits;
        if ((hits % 2) != 0)
            return;

        caster->ModifyPower(POWER_HOLY_POWER, 1);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_crusading_strikes_hp_ex::HandleAfterHit);
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
        if (!dealt || !target->IsAlive())
            return;

        int32 pct = 20;
        if (AuraEffect const* eff = GetEffect(EFFECT_1))
            if (eff->GetAmount() > 0.0)
                pct = int32(eff->GetAmount());

        uint32 const amount = uint32(dealt * uint64(pct) / 100);
        if (!amount)
            return;

        SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EX_EXECUTION_SENTENCE, DIFFICULTY_NONE);
        Unit::DealDamage(caster, target, amount, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_HOLY, info, false);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_pal_execution_sentence_ex::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_execution_sentence_ex::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
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
    RegisterSpellScript(spell_pal_art_of_war_ex);
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
    RegisterSpellScript(spell_pal_crusading_strikes_hp_ex);
    RegisterSpellScript(spell_pal_execution_sentence_ex);
    new spell_pal_execution_sentence_tracker();
}
