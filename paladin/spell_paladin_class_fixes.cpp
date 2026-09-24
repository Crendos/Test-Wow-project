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
    SPELL_EX_EMPIREAN_LEGACY                  = 387170,
    SPELL_EX_EMPIREAN_LEGACY_READY            = 387178,
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
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_righteous_cause_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_righteous_cause_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
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

// 231663 - Великое правосудие: Правосудие вешает на цель стак 197277
// (+20% урона от способностей на Сила Света за стак, до 20 стаков).
class spell_pal_judgment_greater_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_GREATER_JUDGMENT_DEBUFF });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX_GREATER_JUDGMENT))
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX_GREATER_JUDGMENT_DEBUFF, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_judgment_greater_ex::HandleHitTarget);
    }
};

// 267316 - Мастерство: Правосудие Верховного лорда (проц-часть):
// Правосудие с шансом (очки мастерства / 2, +50% от «Безграничного правосудия»)
// бьёт цель Светом (383921). Пассивный бонус урона работает через DBC.
class spell_pal_highlords_judgment_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || !IsPaladinJudgment(spellInfo->Id))
            return false;

        Player* player = eventInfo.GetActor()->ToPlayer();
        if (!player)
            return false;

        float masteryPoints = player->GetTotalAuraModifier(SPELL_AURA_MASTERY)
            + player->GetRatingBonusValue(CR_MASTERY);
        float chance = masteryPoints / 2.f;
        if (player->HasAura(SPELL_EX_BOUNDLESS_JUDGMENT))
            chance *= 1.5f;

        return roll_chance(chance);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        if (Unit* target = eventInfo.GetActionTarget())
            eventInfo.GetActor()->CastSpell(target, SPELL_EX_HIGHLORDS_JUDGMENT_DAMAGE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_highlords_judgment_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_highlords_judgment_ex::HandleProc, EFFECT_3, SPELL_AURA_DUMMY);
    }
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

// 387170 - Неземное наследие: крит Правосудия по цели ниже 35% здоровья
// даёт 2 заряда бесплатной Бури света (387178, +25%).
class spell_pal_empyrean_legacy_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_EMPIREAN_LEGACY_READY });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || !IsPaladinJudgment(spellInfo->Id))
            return false;
        if (!(eventInfo.GetHitMask() & PROC_HIT_CRITICAL))
            return false;

        Unit* target = eventInfo.GetActionTarget();
        return target && target->HealthBelowPct(35);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_EX_EMPIREAN_LEGACY_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = eventInfo.GetProcSpell()
        });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_empyrean_legacy_ex::CheckProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
        OnEffectProc += AuraEffectProcFn(spell_pal_empyrean_legacy_ex::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 1253598 - Крестовый поход: каждая трата Сила Света в ауре Крестового похода (231895)
// добавляет стак (+3% скорости атаки за стак, максимум из E1).
class spell_pal_crusade_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX_CRUSADE });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        if (!GetTarget()->HasAura(SPELL_EX_CRUSADE))
            return false;

        Optional<int32> holyPowerSpent = GetHolyPowerCost(eventInfo.GetProcSpell());
        return holyPowerSpent && *holyPowerSpent > 0;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        if (Aura* crusade = GetTarget()->GetAura(SPELL_EX_CRUSADE))
            if (AuraEffect const* capEffect = GetEffect(EFFECT_1))
                if (crusade->GetStackAmount() < capEffect->GetAmount())
                    crusade->ModStackAmount(1, AURA_REMOVE_BY_ENEMY_SPELL);
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_crusade_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_crusade_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
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

// 406545 - Пламя света: +3% (ранг 1) / +7% (ранг 2) урона Света по целям
// с тикающим Поджиганием (383346).
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

        AddPct(pctMod, 3);
        if (caster->GetAuraEffect(SPELL_EX_HOLY_FLAMES, EFFECT_1))
            AddPct(pctMod, 4);
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_holy_flames_ex::CalculateDamage);
    }
};

void AddSC_paladin_spell_scripts_ex()
{
    RegisterSpellScript(spell_pal_art_of_war_ex);
    RegisterSpellScript(spell_pal_righteous_cause_ex);
    RegisterSpellScript(spell_pal_empyrean_power_ex);
    RegisterSpellScript(spell_pal_judgment_greater_ex);
    RegisterSpellScript(spell_pal_highlords_judgment_ex);
    RegisterSpellScript(spell_pal_judge_jury_executioner_ex);
    RegisterSpellScript(spell_pal_judge_jury_executioner_refund_ex);
    RegisterSpellScript(spell_pal_light_within_damage_ex);
    RegisterSpellScript(spell_pal_light_within_blade_ex);
    RegisterSpellScript(spell_pal_empyrean_legacy_ex);
    RegisterSpellScript(spell_pal_crusade_ex);
    RegisterSpellScript(spell_pal_templar_slash_crit_ex);
    RegisterSpellScript(spell_pal_radiant_glory_ex);
    RegisterSpellScript(spell_pal_divine_storm_cap_ex);
    RegisterSpellScript(spell_pal_holy_flames_ex);
}
