// ============================================================================
// Paladin 12.1.0 class fixes — часть 4a: Свет (ядро хила).
// Вставка: конец spell_paladin.cpp ПОСЛЕ частей 1-3 (использует хелперы
// IsPaladinJudgment/GetHolyPowerCost из части 1). Регистрация: AddSC_paladin_spell_scripts_ex4().
// Спутник: paladin_class_fixes_4.sql
// ============================================================================

// === CUT HERE ===============================================================

enum PaladinEx4Spells
{
    SPELL_EX4_WORD_OF_GLORY             = 85673,
    SPELL_EX4_LIGHT_OF_DAWN             = 85222,
    SPELL_EX4_FLASH_OF_LIGHT            = 19750,
    SPELL_EX4_HOLY_LIGHT                = 82326,
    SPELL_EX4_ETERNAL_FLAME             = 156322,
    SPELL_EX4_HOLY_SHOCK                = 20473,
    SPELL_EX4_HOLY_SHOCK_HEAL           = 25914,
    SPELL_EX4_JUDGMENT_HOLY             = 275773,
    SPELL_EX4_JUDGMENT_RET              = 20271,
    SPELL_EX4_JUDGMENT_PROT             = 275779,
    SPELL_EX4_SOTR                      = 53600,
    SPELL_EX4_AVENGING_WRATH            = 31884,
    SPELL_EX4_CRUSADE_AURA              = 231895,
    SPELL_EX4_AW_8S                     = 454351,
    SPELL_EX4_CONSECRATION              = 26573,
    SPELL_EX4_DIVINE_PURPOSE_BUFF       = 223819, // бафф Пробуждения судьбы ( Holy)

    // таланты/баффы
    SPELL_EX4_MASTERY_LIGHTBRINGER      = 183997,
    SPELL_EX4_BEACON_OF_LIGHT           = 53563,
    SPELL_EX4_BEACON_HEAL_CARRIER       = 53652,  // базово-очковый хил-носитель (используется TC для маяка)
    SPELL_EX4_BEACON_OF_THE_LIGHTBRINGER = 197446,
    SPELL_EX4_UNENDING_LIGHT            = 1271221,
    SPELL_EX4_EXTRICATION               = 461278,
    SPELL_EX4_AWAKENING                 = 414195,
    SPELL_EX4_AWAKENING_READY           = 414193,
    SPELL_EX4_RIGHTEOUS_JUDGMENT        = 414113,
    SPELL_EX4_MOMENT_OF_COMPASSION      = 387786,
    SPELL_EX4_RESPLENDENT_LIGHT         = 392902,
    SPELL_EX4_LIGHTS_CONVICTION         = 414073,
    SPELL_EX4_RECLAMATION               = 415364,
    SPELL_EX4_GLORIOUS_DAWN             = 461246,
    SPELL_EX4_DIVINE_REVELATIONS        = 387808,
    SPELL_EX4_INFUSION_OF_LIGHT_BUFF    = 54149,
    SPELL_EX4_MANA_ENERGIZE_PCT         = 387812, // ENERGIZE_PCT: % макс. маны
    SPELL_EX4_IMBUED_INFUSIONS          = 392961,
    SPELL_EX4_VENERATION                = 392938,
    SPELL_EX4_PROTECTION_OF_TYR         = 200430,
    SPELL_EX4_PROTECTION_OF_TYR_AURA    = 211210,
    SPELL_EX4_AURA_MASTERY              = 31821,
    SPELL_EX4_TRUTH_PREVAILS            = 461273,
    SPELL_EX4_TRUTH_PREVAILS_HEAL       = 461546,
    SPELL_EX4_LIBERATION                = 461287,
    SPELL_EX4_DAMAGE_CARRIER            = 387113, // Execution Sentence: урон только из базовых очков (коэфф. 0)
    SPELL_EX4_SHINING_RIGHTEOUSNESS     = 414443,
    SPELL_EX4_SHINING_RIGHTEOUSNESS_DMG = 414448
};

namespace
{
    [[nodiscard]] Unit* GetNearestBeaconTargetOf(Player const* healer, Unit const* nearTo)
    {
        Unit* best = nullptr;
        float bestDist = 0.f;
        for (Aura* aura : const_cast<Player*>(healer)->GetSingleCastAuras())
        {
            if (aura->GetId() != SPELL_EX4_BEACON_OF_LIGHT)
                continue;
            std::vector<AuraApplication*> applications;
            aura->GetApplicationVector(applications);
            for (AuraApplication const* app : applications)
                if (Unit* target = app->GetTarget())
                {
                    float dist = target->GetDistance2d(nearTo);
                    if (!best || dist < bestDist)
                    {
                        best = target;
                        bestDist = dist;
                    }
                }
        }
        return best;
    }
}

// 183997 - Мастерство: Светоносец — хилы усилены до 1.5%/очко по дистанции до цели
// (или до маяка с 197446). 1271221 Негасимый свет: Заря рассвета получает х1.2.
class spell_pal_mastery_lightbringer_ex : public SpellScript
{
    static constexpr float MASTERY_RANGE = 40.f;

    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Player* healer = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!healer || !victim)
            return;
        if (!healer->HasAura(SPELL_EX4_MASTERY_LIGHTBRINGER))
            return;

        float points = healer->GetTotalAuraModifier(SPELL_AURA_MASTERY)
            + healer->GetRatingBonusValue(CR_MASTERY);
        if (points <= 0.f)
            return;

        float dist = healer->GetDistance2d(victim);
        if (healer->HasAura(SPELL_EX4_BEACON_OF_THE_LIGHTBRINGER))
            if (Unit* beacon = GetNearestBeaconTargetOf(healer, victim))
                dist = std::min(dist, beacon->GetDistance2d(victim));

        float factor = std::clamp(1.f - dist / MASTERY_RANGE, 0.f, 1.f);
        float bonus = points * 1.5f * factor; // 1.5 = BonusCoefficient эффекта мастерства

        if (GetSpellInfo()->Id == SPELL_EX4_LIGHT_OF_DAWN && healer->HasAura(SPELL_EX4_UNENDING_LIGHT))
            bonus *= 1.2f;

        AddPct(pctMod, bonus);
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_mastery_lightbringer_ex::CalculateHealing);
    }
};

// 461278 - Избавление: +до 30% шанса крита Слову света/Заре по здоровью цели.
class spell_pal_extrication_ex : public SpellScript
{
    void CalcCritChance(Unit const* victim, float& critChance)
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || !caster->HasAura(SPELL_EX4_EXTRICATION))
            return;

        float missingFrac = 1.f - victim->GetHealthPct() / 100.f;
        critChance += 30.f * missingFrac;
    }

    void Register() override
    {
        OnCalcCritChance += SpellOnCalcCritChanceFn(spell_pal_extrication_ex::CalcCritChance);
    }
};

// 414195 - Пробуждение: траты Сила Света с шансом 15% дают 414193
// (след. Правосудие +30% урона и гарантированный крит).
class spell_pal_awakening_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_AWAKENING_READY });
    }

    bool CheckProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo) const
    {
        Optional<int32> holyPowerSpent = GetHolyPowerCost(eventInfo.GetProcSpell());
        if (!holyPowerSpent || *holyPowerSpent <= 0)
            return false;
        return roll_chance(aurEff->GetAmount());
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_EX4_AWAKENING_READY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = eventInfo.GetProcSpell()
        });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_awakening_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_awakening_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// Активация АН/Крестового похода тоже даёт Пробуждение («Activating Avenging Wrath activates Awakening»).
class spell_pal_awakening_on_aw_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_AWAKENING_READY });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX4_AWAKENING))
            caster->CastSpell(caster, SPELL_EX4_AWAKENING_READY, TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_awakening_on_aw_ex::HandleAfterCast);
    }
};

// Трата 414193 при касте Правосудия (стак).
class spell_pal_awakening_consume_ex : public SpellScript
{
    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;
        if (Aura* ready = caster->GetAura(SPELL_EX4_AWAKENING_READY))
            ready->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_awakening_consume_ex::HandleHitTarget);
    }
};

// 414113 - Праведное правосудие (Свет): Правосудие кастует Освящение в точку цели.
class spell_pal_righteous_judgment_holy_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_RIGHTEOUS_JUDGMENT, SPELL_EX4_CONSECRATION });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_RIGHTEOUS_JUDGMENT))
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX4_CONSECRATION,
            CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_righteous_judgment_holy_ex::HandleHitTarget);
    }
};

// 387786 - Мгновение сострадания: Вспышка света на цель с вашим маяком +50%.
class spell_pal_moment_of_compassion_ex : public SpellScript
{
    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || !caster->HasAura(SPELL_EX4_MOMENT_OF_COMPASSION))
            return;
        if (!victim->HasAura(SPELL_EX4_BEACON_OF_LIGHT, caster->GetGUID()))
            return;

        AddPct(pctMod, 50);
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_moment_of_compassion_ex::CalculateHealing);
    }
};

// 392902 - Лучезарный свет: Св. свет лечит до 5 союзников рядом с целью на 8%.
class spell_pal_resplendent_light_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_RESPLENDENT_LIGHT, SPELL_EX4_BEACON_HEAL_CARRIER });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX4_RESPLENDENT_LIGHT))
            return;

        int32 heal = int32(CalculatePct(static_cast<int64>(GetHitHeal()), 8));
        if (heal <= 0)
            return;

        float const radius = 8.f;
        std::vector<Unit*> allies;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(target, caster, radius, true);
        Trinity::UnitListSearcher searcher(target, allies, check);
        Cell::VisitAllObjects(target, searcher, radius);

        allies.erase(std::remove_if(allies.begin(), allies.end(), [](Unit* u)
        {
            return !u->IsAlive() || u->IsFullHealth();
        }), allies.end());

        std::sort(allies.begin(), allies.end(), [](Unit* a, Unit* b)
        {
            return a->GetHealthPct() < b->GetHealthPct();
        });

        for (Unit* ally : allies)
        {
            if (ally == target)
                continue;
            caster->CastSpell(ally, SPELL_EX4_BEACON_HEAL_CARRIER, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_BASE_POINT0, heal));
            if (--_targets <= 0)
                break;
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_resplendent_light_ex::HandleHitTarget);
    }

    int32 _targets = 5;
};

// Возврат маны через 387812 (ENERGIZE_PCT, % от максимума маны).
namespace
{
    void RefundManaPct(Unit* caster, Spell const* triggeringSpell, int64 refund)
    {
        int64 maxMana = caster->GetMaxPower(POWER_MANA);
        if (maxMana <= 0 || refund <= 0)
            return;

        int32 pct = int32(std::min<int64>(100, (refund * 100 + maxMana / 2) / maxMana));
        caster->CastSpell(caster, SPELL_EX4_MANA_ENERGIZE_PCT, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR, triggeringSpell, SPELLVALUE_BASE_POINT0, pct));
    }
}

// 414073 - Убеждение света: Св. сияние по врагу возвращает 50% стоимости маны.
class spell_pal_lights_conviction_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_LIGHTS_CONVICTION))
            return;
        if (!caster->IsValidAttackTarget(GetExplTargetUnit()))
            return;

        if (Optional<int32> manaCost = GetSpell()->GetPowerTypeCostAmount(POWER_MANA))
            RefundManaPct(caster, GetSpell(), *manaCost / 2);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_lights_conviction_ex::HandleAfterCast);
    }
};

// 415364 - Возвращение: Св. сияние возвращает до 10% стоимости маны
// (по недостающему здоровью цели; +до 50% эффекта — DBC, аура 354).
class spell_pal_reclamation_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster || !caster->HasAura(SPELL_EX4_RECLAMATION) || !target)
            return;

        float missingFrac = std::clamp(1.f - target->GetHealthPct() / 100.f, 0.f, 1.f);
        if (Optional<int32> manaCost = GetSpell()->GetPowerTypeCostAmount(POWER_MANA))
            RefundManaPct(caster, GetSpell(), int64(*manaCost * 0.10f * missingFrac));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_reclamation_ex::HandleAfterCast);
    }
};

// 461246 - Славный рассвет: Св. сияние с шансом 12% возвращает заряд (+10% хила — DBC).
class spell_pal_glorious_dawn_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_GLORIOUS_DAWN))
            return;
        if (!roll_chance(12))
            return;

        caster->GetSpellHistory()->RestoreCharge(GetSpellInfo()->ChargeCategoryId);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_glorious_dawn_ex::HandleAfterCast);
    }
};

// 387808 - Божественные откровения:
//  * Вспышка света с Вливанием света +20% хила;
//  * Правосудие с Вливанием света возвращает 1% макс. маны (387812).
class spell_pal_divine_revelations_fol_ex : public SpellScript
{
    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* /*victim*/, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_DIVINE_REVELATIONS))
            return;
        if (!caster->HasAura(SPELL_EX4_INFUSION_OF_LIGHT_BUFF))
            return;

        AddPct(pctMod, 20);
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_divine_revelations_fol_ex::CalculateHealing);
    }
};

class spell_pal_divine_revelations_judg_ex : public SpellScript
{
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_DIVINE_REVELATIONS))
            return;
        if (!caster->HasAura(SPELL_EX4_INFUSION_OF_LIGHT_BUFF))
            return;

        caster->CastSpell(caster, SPELL_EX4_MANA_ENERGIZE_PCT, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_BASE_POINT0, 1));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_revelations_judg_ex::HandleAfterCast);
    }
};

// 392961 - Насыщенные вливания: трата Вливания света -1с КД Св. сияния.
class spell_pal_imbued_infusions_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_HOLY_SHOCK, SPELL_EX4_IMBUED_INFUSIONS });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player || !player->HasAura(SPELL_EX4_IMBUED_INFUSIONS))
            return;
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
            return;

        player->GetSpellHistory()->ModifyCooldown(SPELL_EX4_HOLY_SHOCK, Seconds(-1));
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_imbued_infusions_ex::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 392938 - Почтение (хил-часть): крит Вспышки/Св. света сбрасывает КД Правосудия.
class spell_pal_veneration_ex : public SpellScript
{
    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_VENERATION))
            return;
        if (!IsHitCrit())
            return;

        for (uint32 spellId : { SPELL_EX4_JUDGMENT_HOLY, SPELL_EX4_JUDGMENT_RET, SPELL_EX4_JUDGMENT_PROT })
            if (caster->HasSpell(spellId))
            {
                caster->GetSpellHistory()->ResetCooldown(spellId, true);
                break;
            }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_veneration_ex::HandleHitTarget);
    }
};

// 200430 - Защита Тира: Аура света даёт 211210 (+10% получаемого лечения, 8с).
class spell_pal_protection_of_tyr_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_PROTECTION_OF_TYR, SPELL_EX4_PROTECTION_OF_TYR_AURA });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX4_PROTECTION_OF_TYR))
            caster->CastSpell(caster, SPELL_EX4_PROTECTION_OF_TYR_AURA,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_protection_of_tyr_ex::HandleAfterCast);
    }
};

// 461273 - Истина превыше всего: Правосудие лечит вас (461546).
class spell_pal_truth_prevails_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_TRUTH_PREVAILS, SPELL_EX4_TRUTH_PREVAILS_HEAL });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_TRUTH_PREVAILS))
            return;

        caster->CastSpell(caster, SPELL_EX4_TRUTH_PREVAILS_HEAL, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_truth_prevails_ex::HandleHitTarget);
    }
};

// 461287 - Освобождение: 12% хила Слова света/Заря -> урон Света ближнему врагу.
class spell_pal_liberation_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_LIBERATION, SPELL_EX4_DAMAGE_CARRIER });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX4_LIBERATION))
            return;

        int64 damage = CalculatePct(static_cast<int64>(GetHitHeal()), 12);
        if (damage <= 0)
            return;

        float const radius = 8.f;
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, radius);
        Trinity::UnitListSearcher searcher(target, enemies, check);
        Cell::VisitAllObjects(target, searcher, radius);

        for (Unit* enemy : enemies)
        {
            if (!caster->IsValidAttackTarget(enemy) || !enemy->IsWithinLOSInMap(caster))
                continue;
            caster->CastSpell(enemy, SPELL_EX4_DAMAGE_CARRIER, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_BASE_POINT0, int32(damage)));
            break;
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_liberation_ex::HandleHitTarget);
    }
};

// 414443 - Сияющая праведность: Щит праведника бьёт первую цель (414448)
// и с шансом 35% даёт Пробуждение судьбы (223819 — обрабатывается скриптом TC).
class spell_pal_shining_righteousness_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX4_SHINING_RIGHTEOUSNESS, SPELL_EX4_SHINING_RIGHTEOUSNESS_DMG, SPELL_EX4_DIVINE_PURPOSE_BUFF });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX4_SHINING_RIGHTEOUSNESS))
            return;

        if (!GetHitUnit()->HasAura(SPELL_EX4_SHINING_RIGHTEOUSNESS_DMG, caster->GetGUID()))
            caster->CastSpell(GetHitUnit(), SPELL_EX4_SHINING_RIGHTEOUSNESS_DMG, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });

        if (roll_chance(35))
            caster->CastSpell(caster, SPELL_EX4_DIVINE_PURPOSE_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_shining_righteousness_ex::HandleHitTarget);
    }
};

void AddSC_paladin_spell_scripts_ex4()
{
    RegisterSpellScript(spell_pal_mastery_lightbringer_ex);
    RegisterSpellScript(spell_pal_extrication_ex);
    RegisterSpellScript(spell_pal_awakening_ex);
    RegisterSpellScript(spell_pal_awakening_on_aw_ex);
    RegisterSpellScript(spell_pal_awakening_consume_ex);
    RegisterSpellScript(spell_pal_righteous_judgment_holy_ex);
    RegisterSpellScript(spell_pal_moment_of_compassion_ex);
    RegisterSpellScript(spell_pal_resplendent_light_ex);
    RegisterSpellScript(spell_pal_lights_conviction_ex);
    RegisterSpellScript(spell_pal_reclamation_ex);
    RegisterSpellScript(spell_pal_glorious_dawn_ex);
    RegisterSpellScript(spell_pal_divine_revelations_fol_ex);
    RegisterSpellScript(spell_pal_divine_revelations_judg_ex);
    RegisterSpellScript(spell_pal_imbued_infusions_ex);
    RegisterSpellScript(spell_pal_veneration_ex);
    RegisterSpellScript(spell_pal_protection_of_tyr_ex);
    RegisterSpellScript(spell_pal_truth_prevails_ex);
    RegisterSpellScript(spell_pal_liberation_ex);
    RegisterSpellScript(spell_pal_shining_righteousness_ex);
}
