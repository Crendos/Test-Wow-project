// ============================================================================
// Paladin 12.1.0 class fixes — часть 4b: Свет (маяки + триггеры АН).
// Вставка после части 4a. Регистрация: AddSC_paladin_spell_scripts_ex5().
// Спутник: paladin_class_fixes_4.sql (внизу).
//
// ВАЖНО (маяки v1): TC-скрипт spell_pal_light_s_beacon (53651) переносит хил
// только на ОДИН маяк (applications.front()). Для двух маяков (Маяк веры)
// поправьте его HandleProc: перебирайте ВСЕ ауры 53563 кастера и лечите каждую
// (код ниже - GetBeaconTargetsOf). Остальное работает без правок.
// ============================================================================

// === CUT HERE ===============================================================

enum PaladinEx5Spells
{
    SPELL_EX5_BEACON_OF_LIGHT           = 53563,
    SPELL_EX5_BEACON_OF_LIGHT_HEAL      = 53652,
    SPELL_EX5_BEACON_OF_FAITH           = 156910,
    SPELL_EX5_BEACON_OF_VIRTUE          = 200025,

    SPELL_EX5_AVENGING_WRATH            = 31884,
    SPELL_EX5_CRUSADE_AURA              = 231895,
    SPELL_EX5_AW_8S                     = 454351,

    SPELL_EX5_TYRS_DELIVERANCE          = 1241275,
    SPELL_EX5_TYRS_DELIVERANCE_AURA     = 200652,
    SPELL_EX5_TYRS_DELIVERANCE_SELECT   = 200653,
    SPELL_EX5_TYRS_DELIVERANCE_HEAL     = 200654,

    SPELL_EX5_HAND_OF_DIVINITY          = 1242008,
    SPELL_EX5_HAND_OF_DIVINITY_BUFF     = 414273,

    SPELL_EX5_SAVED_BY_THE_LIGHT        = 157047,
    SPELL_EX5_SAVED_BY_THE_LIGHT_ABSORB = 157128,

    SPELL_EX5_REFINING_FIRE             = 469883,
    SPELL_EX5_REFINING_FIRE_DOT         = 469882
};

namespace
{
    std::vector<Unit*> GetBeaconTargetsOf(Unit const* healer)
    {
        std::vector<Unit*> result;
        for (Aura* aura : const_cast<Unit*>(healer)->GetSingleCastAuras())
        {
            if (aura->GetId() != SPELL_EX5_BEACON_OF_LIGHT)
                continue;
            std::vector<AuraApplication*> applications;
            aura->GetApplicationVector(applications);
            for (AuraApplication const* app : applications)
                if (Unit* target = app->GetTarget())
                    result.push_back(target);
        }
        return result;
    }
}

// 156910 - Маяк веры: кастует второй Маяк света на цель (v1: полный перенос;
// для 70% см. примечание в шапке файла).
class spell_pal_beacon_of_faith_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_BEACON_OF_FAITH, SPELL_EX5_BEACON_OF_LIGHT });
    }

    void HandleHitTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        caster->CastSpell(target, SPELL_EX5_BEACON_OF_LIGHT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pal_beacon_of_faith_ex::HandleHitTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 200025 - Маяк добродетели: маяк на цель + до 4 раненых союзников рядом (9с).
class spell_pal_beacon_of_virtue_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_BEACON_OF_VIRTUE, SPELL_EX5_BEACON_OF_LIGHT });
    }

    void SelectTargets(std::list<WorldObject*>& targets)
    {
        // раненые, ближайшие по проценту здоровья
        targets.remove_if([](WorldObject* obj)
        {
            Unit* unit = obj->ToUnit();
            return !unit || unit->IsFullHealth();
        });

        if (targets.size() > 4)
        {
            targets.sort([](WorldObject* a, WorldObject* b)
            {
                return a->ToUnit()->GetHealthPct() < b->ToUnit()->GetHealthPct();
            });
            targets.resize(4);
        }
    }

    void HandleHitAreaTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX5_BEACON_OF_LIGHT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void HandleHitMainTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX5_BEACON_OF_LIGHT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        // E1 - SRC_AREA_ALLY (4 раненых)
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pal_beacon_of_virtue_ex::SelectTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ALLY);
        OnEffectHitTarget += SpellEffectFn(spell_pal_beacon_of_virtue_ex::HandleHitAreaTarget, EFFECT_1, SPELL_EFFECT_DUMMY);
        // E0 - основная цель
        OnEffectHitTarget += SpellEffectFn(spell_pal_beacon_of_virtue_ex::HandleHitMainTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 1241275 - Избавление Тира: активация АН -> 200652 (аура-канал).
class spell_pal_tyrs_deliverance_trigger_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_TYRS_DELIVERANCE, SPELL_EX5_TYRS_DELIVERANCE_AURA });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX5_TYRS_DELIVERANCE))
            caster->CastSpell(caster, SPELL_EX5_TYRS_DELIVERANCE_AURA,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_tyrs_deliverance_trigger_ex::HandleAfterCast);
    }
};

// 200653 - Избавление Тира: выбирает до 5 раненых союзников и лечит их (200654).
class spell_pal_tyrs_deliverance_select_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_TYRS_DELIVERANCE_HEAL });
    }

    void SelectTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* obj)
        {
            Unit* unit = obj->ToUnit();
            return !unit || unit->IsFullHealth();
        });

        targets.sort([](WorldObject* a, WorldObject* b)
        {
            return a->ToUnit()->GetHealthPct() < b->ToUnit()->GetHealthPct();
        });

        if (targets.size() > 5)
            targets.resize(5);
    }

    void HandleHitTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX5_TYRS_DELIVERANCE_HEAL, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_pal_tyrs_deliverance_select_ex::SelectTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ALLY);
        OnEffectHitTarget += SpellEffectFn(spell_pal_tyrs_deliverance_select_ex::HandleHitTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 1242008 - Длань божественности: АН -> след. 2 Св. света мгновенны и без маны (414273).
class spell_pal_hand_of_divinity_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_HAND_OF_DIVINITY, SPELL_EX5_HAND_OF_DIVINITY_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX5_HAND_OF_DIVINITY))
            caster->CastSpell(caster, SPELL_EX5_HAND_OF_DIVINITY_BUFF,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_hand_of_divinity_ex::HandleAfterCast);
    }
};

// 157047 - Спасение светом: союзник с маяком получил урон -> щит 157128
// (300 базово, до +9% по низкому здоровью; внутренний КД 30с - в spell_proc).
class spell_pal_saved_by_the_light_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_SAVED_BY_THE_LIGHT, SPELL_EX5_SAVED_BY_THE_LIGHT_ABSORB });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        Unit* caster = GetCaster();
        Unit* victim = eventInfo.GetActionTarget();
        if (!caster || !victim)
            return false;
        if (!victim->HasAura(SPELL_EX5_BEACON_OF_LIGHT, caster->GetGUID()))
            return false;
        if (!eventInfo.GetDamageInfo() || eventInfo.GetDamageInfo()->GetDamage() <= 0)
            return false;
        return true;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        Unit* target = GetTarget();
        float missingFrac = 1.f - target->GetHealthPct() / 100.f;
        int32 absorb = int32(300.f * (1.f + 0.09f * missingFrac));

        target->CastSpell(target, SPELL_EX5_SAVED_BY_THE_LIGHT_ABSORB, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR, eventInfo.GetProcSpell(), SPELLVALUE_BASE_POINT0, absorb));
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_saved_by_the_light_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_saved_by_the_light_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 469883 - Очищающий огонь: Щит мстителя поджигает цель (469882).
class spell_pal_refining_fire_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_REFINING_FIRE_DOT });
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        if (Unit* target = eventInfo.GetActionTarget())
            GetTarget()->CastSpell(target, SPELL_EX5_REFINING_FIRE_DOT, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_pal_refining_fire_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

void AddSC_paladin_spell_scripts_ex5()
{
    RegisterSpellScript(spell_pal_beacon_of_faith_ex);
    RegisterSpellScript(spell_pal_beacon_of_virtue_ex);
    RegisterSpellScript(spell_pal_tyrs_deliverance_trigger_ex);
    RegisterSpellScript(spell_pal_tyrs_deliverance_select_ex);
    RegisterSpellScript(spell_pal_hand_of_divinity_ex);
    RegisterSpellScript(spell_pal_saved_by_the_light_ex);
    RegisterSpellScript(spell_pal_refining_fire_ex);
}
