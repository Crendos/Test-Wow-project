// ============================================================================
// Paladin 12.1.0 class fixes — часть 2: остаток Воздаяния + ядро Защиты.
// Вставка: конец spell_paladin.cpp (рядом с частью 1), регистрация AddSC_paladin_spell_scripts_ex2().
// Спутник: paladin/paladin_class_fixes_2.sql
// ============================================================================

// === CUT HERE ===============================================================

enum PaladinEx2Spells
{
    SPELL_EX2_JUDGMENT_RET              = 20271,
    SPELL_EX2_JUDGMENT_PROT             = 275779,
    SPELL_EX2_JUDGMENT_HOLY             = 275773,
    SPELL_EX2_DIVINE_STORM              = 53385,
    SPELL_EX2_DIVINE_STORM_SINGLE       = 224239,
    SPELL_EX2_TEMPLARS_VERDICT          = 85256,
    SPELL_EX2_FINAL_VERDICT             = 383328,
    SPELL_EX2_SHIELD_OF_THE_RIGHTEOUS   = 53600,
    SPELL_EX2_WORD_OF_GLORY             = 85673,
    SPELL_EX2_AVENGERS_SHIELD           = 31935,
    SPELL_EX2_CONSECRRATION_TICK        = 81297,
    SPELL_EX2_BLESSED_HAMMER_IMPACT     = 204301,

    // баффы/дебаффы
    SPELL_EX2_JUDGMENT_OF_JUSTICE_SLOW  = 408383, // -30% скорость, 8с
    SPELL_EX2_SHINING_LIGHT_STACKS      = 182104, // счётчик Сияния (кап 4)
    SPELL_EX2_SHINING_LIGHT_FREE        = 327510, // бесплатное Слово света (DBC -100% стоимости)
    SPELL_EX2_BULWARK_OF_ORDER_SHIELD   = 209388, // щит Оплота порядка
    SPELL_EX2_LIGHT_OF_TITANS_HOT       = 378412, // ХОТ Света титанов
    SPELL_EX2_GOLDEN_PATH_HEAL          = 339119, // лечилка (для Отрады, bp override)
    SPELL_EX2_SEAL_OF_REPRISAL_DEBUFF   = 1302139,
    SPELL_EX2_EYE_FOR_AN_EYE_DAMAGE     = 469311,
    SPELL_EX2_LIGHTFORGED_HEAL          = 403460,

    // таланты
    SPELL_EX2_TEMPEST_OF_THE_LIGHTBRINGER = 383396,
    SPELL_EX2_JUDGMENT_OF_JUSTICE         = 403495,
    SPELL_EX2_GREATER_JUDGMENT_TALENT     = 231663,
    SPELL_EX2_BURN_TO_ASH                 = 446663,
    SPELL_EX2_SHINING_LIGHT_TALENT        = 321136,
    SPELL_EX2_BULWARK_OF_ORDER_TALENT     = 209389,
    SPELL_EX2_LIGHT_OF_THE_TITANS         = 378405,
    SPELL_EX2_SOLACE                      = 1245891,
    SPELL_EX2_VISION_OF_SANCTITY          = 1245354,
    SPELL_EX2_SEAL_OF_REPRISAL            = 377053,
    SPELL_EX2_EYE_FOR_AN_EYE              = 469309,
    SPELL_EX2_LIGHTFORGED_BLESSING_RET    = 403479,
    SPELL_EX2_LIGHTFORGED_BLESSING_PROT   = 406468
};

// --- Воздаяние ---------------------------------------------------------------

// 383396 - Буря Светоносца: каждая цель Бури света получает доп. волну
// (224239) на 20% урона Бури.
class spell_pal_tempest_of_the_lightbringer_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_TEMPEST_OF_THE_LIGHTBRINGER, SPELL_EX2_DIVINE_STORM_SINGLE });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_TEMPEST_OF_THE_LIGHTBRINGER))
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX2_DIVINE_STORM_SINGLE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_tempest_of_the_lightbringer_ex::HandleHitTarget);
    }
};

// 224239 - волна Бури Светоносца: 20% от урона Бури (только если вызвана ею).
class spell_pal_tempest_wave_ex : public SpellScript
{
    void HandleDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        SpellInfo const* triggeredBy = GetTriggeringSpell();
        if (!triggeredBy || triggeredBy->Id != SPELL_EX2_DIVINE_STORM)
            return;

        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_TEMPEST_OF_THE_LIGHTBRINGER))
            return;

        if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX2_TEMPEST_OF_THE_LIGHTBRINGER, EFFECT_1))
            AddPct(pctMod, bonus->GetAmount());
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_tempest_wave_ex::HandleDamage);
    }
};

// 403495 - Правосудие справедливости: с Великим правосудием Правосудие
// замедляет цель на 30% на 8 с (408383).
class spell_pal_judgment_of_justice_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_JUDGMENT_OF_JUSTICE_SLOW });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;
        if (!caster->HasAura(SPELL_EX2_JUDGMENT_OF_JUSTICE) || !caster->HasAura(SPELL_EX2_GREATER_JUDGMENT_TALENT))
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX2_JUDGMENT_OF_JUSTICE_SLOW, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_judgment_of_justice_ex::HandleHitTarget);
    }
};

// 446663 - Сжигание праха: Поджигание наносит на 2% больше урона за каждый тик.
class spell_pal_burn_to_ash_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_BURN_TO_ASH });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        _baseAmount = GetEffect(EFFECT_0)->GetAmount();
        _ticks = 0;
    }

    void OnPeriodic(AuraEffect const* aurEff)
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_BURN_TO_ASH))
            return;

        ++_ticks;
        const_cast<AuraEffect*>(aurEff)->ChangeAmount(_baseAmount * (1.f + 0.02f * _ticks));
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_burn_to_ash_ex::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_burn_to_ash_ex::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }

    float _baseAmount = 0.f;
    int32 _ticks = 0;
};

// --- Защита ------------------------------------------------------------------

// 321136 - Сияние: каждые 3 Щита праведника — следующее Слово света бесплатно.
// Стак 182104 (визуал, кап 4) + 327510 (DBC снимает стоимость). Трата — в скрипте Слова света.
class spell_pal_shining_light_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_SHINING_LIGHT_STACKS, SPELL_EX2_SHINING_LIGHT_FREE, SPELL_EX2_SHIELD_OF_THE_RIGHTEOUS });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo) const
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        return spellInfo && spellInfo->Id == SPELL_EX2_SHIELD_OF_THE_RIGHTEOUS;
    }

    void HandleProc(AuraEffect* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        if (++_sotRCastCount < aurEff->GetAmount())
            return;

        _sotRCastCount = 0;
        Unit* target = GetTarget();

        if (Aura* stacks = target->GetAura(SPELL_EX2_SHINING_LIGHT_STACKS))
        {
            if (stacks->GetStackAmount() < 4)
                stacks->ModStackAmount(1, AURA_REMOVE_BY_ENEMY_SPELL);
        }
        else
            target->CastSpell(target, SPELL_EX2_SHINING_LIGHT_STACKS, TRIGGERED_IGNORE_CAST_IN_PROGRESS);

        if (Aura* freeWoG = target->GetAura(SPELL_EX2_SHINING_LIGHT_FREE))
        {
            if (freeWoG->GetStackAmount() < freeWoG->GetSpellInfo()->StackAmount)
                freeWoG->ModStackAmount(1, AURA_REMOVE_BY_ENEMY_SPELL);
        }
        else
            target->CastSpell(target, SPELL_EX2_SHINING_LIGHT_FREE, TRIGGERED_IGNORE_CAST_IN_PROGRESS);
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_shining_light_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_shining_light_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }

    int32 _sotRCastCount = 0;
};

// Трата стака Сияния после бесплатного Слова света.
class spell_pal_shining_light_consume_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (Aura* freeWoG = caster->GetAura(SPELL_EX2_SHINING_LIGHT_FREE))
        {
            freeWoG->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
            if (Aura* stacks = caster->GetAura(SPELL_EX2_SHINING_LIGHT_STACKS))
                stacks->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_shining_light_consume_ex::HandleAfterCast);
    }
};

// 209389 - Оплот порядка: Щит мстителя даёт щит (209388) на 75% урона
// (по главному попаданию), максимум 50% здоровья.
class spell_pal_bulwark_of_order_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_BULWARK_OF_ORDER_TALENT, SPELL_EX2_BULWARK_OF_ORDER_SHIELD });
    }

    void HandleHitTarget()
    {
        if (_shieldDone)
            return;
        _shieldDone = true;

        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_BULWARK_OF_ORDER_TALENT))
            return;

        int64 shield = CalculatePct(static_cast<int64>(GetHitDamage()), 75);
        shield = std::min<int64>(shield, caster->CountPctFromMaxHealth(50));

        caster->CastSpell(caster, SPELL_EX2_BULWARK_OF_ORDER_SHIELD, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, int32(shield) } }
        });
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_pal_bulwark_of_order_ex::HandleHitTarget);
    }

    bool _shieldDone = false;
};

// 378405 - Свет титанов: Слово света лечит ещё 40% ХОТ-ом (378412, 5 тиков);
// на себя — втрое больше.
class spell_pal_light_of_the_titans_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_LIGHT_OF_THE_TITANS, SPELL_EX2_LIGHT_OF_TITANS_HOT });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_LIGHT_OF_THE_TITANS))
            return;

        float mult = 1.f;
        if (GetHitUnit() == caster)
            mult = 3.f; // +200% (E1)

        int32 hotBase = int32(CalculatePct(GetHitHeal(), 40 * mult) / 5); // 5 тиков по 2с
        if (hotBase <= 0)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX2_LIGHT_OF_TITANS_HOT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, hotBase } }
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_light_of_the_titans_ex::HandleHitTarget);
    }
};

// 1245891 Отрада (Отвага): Освящение лечит на 3.75% урона.
// 1245354 Зрение святости: при одной цели урон Освящения x2.
class spell_pal_consecration_prot_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_SOLACE, SPELL_EX2_VISION_OF_SANCTITY, SPELL_EX2_GOLDEN_PATH_HEAL });
    }

    void HandleDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (caster->HasAura(SPELL_EX2_VISION_OF_SANCTITY) && GetUnitTargetCountForEffect(EFFECT_0) == 1)
            AddPct(pctMod, 100);
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_SOLACE))
            return;

        int64 heal = CalculatePct(static_cast<int64>(GetHitDamage()), 3.75f);
        // 339119 имеет ap-коэффициент 0.05 — вычитаем его, чтобы не задваивать
        heal -= int64(0.05f * caster->GetTotalAttackPowerValue(BASE_ATTACK));
        if (heal <= 0)
            return;

        caster->CastSpell(caster, SPELL_EX2_GOLDEN_PATH_HEAL, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, int32(heal) } }
        });
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_consecration_prot_ex::HandleDamage);
        AfterHit += SpellHitFn(spell_pal_consecration_prot_ex::HandleHitTarget);
    }
};

// 377053 - Печать возмездия: Благословенный молот вешает 1302139
// (-10% урона врага по вам, 8с). Для Молота праведника триггер уже в DBC.
class spell_pal_seal_of_reprisal_bh_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_SEAL_OF_REPRISAL, SPELL_EX2_SEAL_OF_REPRISAL_DEBUFF });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX2_SEAL_OF_REPRISAL))
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX2_SEAL_OF_REPRISAL_DEBUFF, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_seal_of_reprisal_bh_ex::HandleHitTarget);
    }
};

// 469309 - Око за око: во время Божественного щита / Божественной защиты /
// Стойкого защитника атакующие вас получают урон Света (469311).
class spell_pal_eye_for_an_eye_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_EYE_FOR_AN_EYE_DAMAGE });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        Unit* target = GetTarget();
        return target->HasAura(642) || target->HasAura(498) || target->HasAura(403876) || target->HasAura(31850);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        if (Unit* attacker = eventInfo.GetActor())
            GetTarget()->CastSpell(attacker, SPELL_EX2_EYE_FOR_AN_EYE_DAMAGE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_eye_for_an_eye_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_eye_for_an_eye_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 403479/406468 - Благословение Света: Буря света лечит вас и союзников рядом (403460).
class spell_pal_lightforged_blessing_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX2_LIGHTFORGED_BLESSING_RET, SPELL_EX2_LIGHTFORGED_BLESSING_PROT, SPELL_EX2_LIGHTFORGED_HEAL });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;
        if (!caster->HasAura(SPELL_EX2_LIGHTFORGED_BLESSING_RET) && !caster->HasAura(SPELL_EX2_LIGHTFORGED_BLESSING_PROT))
            return;

        caster->CastSpell(caster, SPELL_EX2_LIGHTFORGED_HEAL, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_lightforged_blessing_ex::HandleAfterCast);
    }
};

void AddSC_paladin_spell_scripts_ex2()
{
    RegisterSpellScript(spell_pal_tempest_of_the_lightbringer_ex);
    RegisterSpellScript(spell_pal_tempest_wave_ex);
    RegisterSpellScript(spell_pal_judgment_of_justice_ex);
    RegisterSpellScript(spell_pal_burn_to_ash_ex);
    RegisterSpellScript(spell_pal_shining_light_ex);
    RegisterSpellScript(spell_pal_shining_light_consume_ex);
    RegisterSpellScript(spell_pal_bulwark_of_order_ex);
    RegisterSpellScript(spell_pal_light_of_the_titans_ex);
    RegisterSpellScript(spell_pal_consecration_prot_ex);
    RegisterSpellScript(spell_pal_seal_of_reprisal_bh_ex);
    RegisterSpellScript(spell_pal_eye_for_an_eye_ex);
    RegisterSpellScript(spell_pal_lightforged_blessing_ex);
}
