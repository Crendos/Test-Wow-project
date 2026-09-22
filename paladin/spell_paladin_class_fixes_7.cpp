// ============================================================================
// Paladin 12.1.0 class fixes — часть 6: ГЕРОЙСКИЕ ДЕРЕВЬЯ (v1, камни и ключевые).
//   Храмовник (Templar, Прот/Воздаяние): Свет наставления (Молот Света).
//   Вестник солнца (Herald of the Sun, Свет/Воздаяние): Рассветный свет, Второй восход.
//   Ламповщик (Lightsmith, Прот/Свет): Доблесть.
// Авто-проц-ветки (Gleaming Rays, Solar Grace, Endless Wrath, Unrelenting Charger,
// Forewarning, Will of the Dawn, Shake the Heavens, Undisputed Ruling, Sanctification,
// Divine Hammer, Born in Sunlight, Light's Judicator, Blessed Assurance) работают
// через DBC (SPELL_AURA_PROC_TRIGGER_SPELL в белом списке авто-генерации proc-ов).
// Вставка после части 5. Регистрация: AddSC_paladin_spell_scripts_ex7().
// Спутник: paladin_class_fixes_6.sql
// ============================================================================

// === CUT HERE ===============================================================

enum PaladinEx7Spells
{
    SPELL_EX7_JUDGMENT_RET              = 20271,
    SPELL_EX7_JUDGMENT_PROT             = 275779,
    SPELL_EX7_JUDGMENT_HOLY             = 275773,
    SPELL_EX7_HOLY_SHOCK                = 20473,
    SPELL_EX7_WORD_OF_GLORY             = 85673,
    SPELL_EX7_HAMMER_OF_WRATH           = 24275,
    SPELL_EX7_HAMMER_OF_WRATH_AW        = 1241413,

    // Храмовник: Молот Света
    SPELL_EX7_LIGHTS_GUIDANCE           = 427445, // талант
    SPELL_EX7_HOL_DRIVER                = 427453, // кнопка (тратит СС по DBC)
    SPELL_EX7_HOL_DAMAGE                = 429826, // урон по главной
    SPELL_EX7_EMPIREAN_HAMMER           = 431398, // летящий молоток
    SPELL_EX7_SACROSANCT_CRUSADE        = 431730,
    SPELL_EX7_SACROSANCT_CRUSADE_HEAL   = 461885,
    SPELL_EX7_HAMMERFALL                = 432463,

    // Вестник солнца
    SPELL_EX7_DAWNLIGHT_TALENT          = 431377,
    SPELL_EX7_DAWNLIGHT_DOT             = 431380,
    SPELL_EX7_SECOND_SUNRISE            = 431474,

    // Ламповщик
    SPELL_EX7_VALIANCE                  = 432919,
    SPELL_EX7_BOP                       = 1022,
    SPELL_EX7_SPELLWARDING              = 204018,
    SPELL_EX7_DIVINE_SHIELD             = 642,
    SPELL_EX7_BOS                       = 6940
};

// --- ХРАМОВНИК ---------------------------------------------------------------

// 427453 - Молот Света (кнопка «Света наставления»): урон по цели + 2 летящих
// молотка (431398) по соседям; с Молотовым молотом (432463) — ещё один;
// с Сакросанктным крестовым походом (431730) — лечение.
class spell_pal_hammer_of_light_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_LIGHTS_GUIDANCE, SPELL_EX7_HOL_DAMAGE, SPELL_EX7_EMPIREAN_HAMMER });
    }

    void CastEmpyreanHammers(int32 count)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit() ? GetHitUnit() : GetExplTargetUnit();
        if (!caster || !target || count <= 0)
            return;

        float const radius = 20.f;
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, radius);
        Trinity::UnitListSearcher searcher(target, enemies, check);
        Cell::VisitAllObjects(target, searcher, radius);

        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [caster, target](Unit* enemy)
        {
            return enemy == target || !caster->IsValidAttackTarget(enemy) || !enemy->IsWithinLOSInMap(caster);
        }), enemies.end());

        Trinity::Containers::RandomShuffle(enemies);

        for (int32 i = 0; i < count; ++i)
        {
            Unit* dest = i < int32(enemies.size()) ? enemies[i] : target;
            caster->CastSpell(dest, SPELL_EX7_EMPIREAN_HAMMER, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
        }
    }

    void HandleHitTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX7_LIGHTS_GUIDANCE))
            return;

        // основной урон
        caster->CastSpell(GetHitUnit(), SPELL_EX7_HOL_DAMAGE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        // 2 молотка (E2 «Света наставления»)
        CastEmpyreanHammers(2);

        // Сакросанктный крестовый поход: лечение (% макс. HP + за цель, кап 5)
        if (caster->HasAura(SPELL_EX7_SACROSANCT_CRUSADE))
        {
            if (AuraEffect const* healPct = caster->GetAuraEffect(SPELL_EX7_SACROSANCT_CRUSADE, EFFECT_4))
            {
                int32 pct = healPct->GetAmount();
                int32 targetsHit = std::min<int32>(GetUnitTargetCountForEffect(EFFECT_0), 5);
                if (AuraEffect const* perTarget = caster->GetAuraEffect(SPELL_EX7_SACROSANCT_CRUSADE, EFFECT_5))
                    pct += perTarget->GetAmount() * targetsHit;

                int64 heal = caster->CountPctFromMaxHealth(pct);
                caster->CastSpell(caster, SPELL_EX7_SACROSANCT_CRUSADE_HEAL, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell(),
                    .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, int32(heal) } }
                });
            }
        }
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        // Молотопад (Hammerfall): ещё один молоток
        if (caster && caster->HasAura(SPELL_EX7_HAMMERFALL))
            CastEmpyreanHammers(1);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pal_hammer_of_light_ex::HandleHitTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
        AfterCast += SpellCastFn(spell_pal_hammer_of_light_ex::HandleAfterCast);
    }
};

// --- ВЕСТНИК СОЛНЦА ----------------------------------------------------------

// 431377 - Рассветный свет: Правосудие (Рет) и Святое сияние (Свет) оставляют
// Рассветный свет на цели (DoT; взрыв по области — v1 без взрыва).
class spell_pal_dawnlight_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_DAWNLIGHT_TALENT, SPELL_EX7_DAWNLIGHT_DOT });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX7_DAWNLIGHT_TALENT))
            return;

        SpellInfo const* spellInfo = GetSpellInfo();
        if (!spellInfo)
            return;

        // Рет: Правосудие; Свет: Святое сияние
        bool applicable = spellInfo->Id == SPELL_EX7_JUDGMENT_RET || spellInfo->Id == SPELL_EX7_HOLY_SHOCK;
        if (!applicable)
            return;

        if (target->HasAura(SPELL_EX7_DAWNLIGHT_DOT, caster->GetGUID()))
            return;

        caster->CastSpell(target, SPELL_EX7_DAWNLIGHT_DOT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_dawnlight_ex::HandleHitTarget);
    }
};

// 431474 - Второй восход: Молот гнева с шансом 15% отыгрывается эхом
// (шанс/ICD — в spell_proc; эхо = повторный каст).
class spell_pal_second_sunrise_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_HAMMER_OF_WRATH });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        return spellInfo && (spellInfo->Id == SPELL_EX7_HAMMER_OF_WRATH || spellInfo->Id == SPELL_EX7_HAMMER_OF_WRATH_AW);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        if (Unit* target = eventInfo.GetActionTarget())
            GetTarget()->CastSpell(target, eventInfo.GetSpellInfo()->Id, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_second_sunrise_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_second_sunrise_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// --- ЛАМПОВЩИК ---------------------------------------------------------------

// 432919 - Доблесть (Прот): Слово света снижает КД БЗ/БП/БС/ЩБ на 3с.
class spell_pal_valiance_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_VALIANCE });
    }

    void HandleAfterCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player || !player->HasAura(SPELL_EX7_VALIANCE))
            return;
        if (player->GetPrimarySpecialization() != ChrSpecialization::PaladinProtection)
            return;

        for (uint32 spellId : { SPELL_EX7_BOS, SPELL_EX7_BOP, SPELL_EX7_SPELLWARDING, SPELL_EX7_DIVINE_SHIELD })
            player->GetSpellHistory()->ModifyCooldown(spellId, Seconds(-3));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_valiance_ex::HandleAfterCast);
    }
};

void AddSC_paladin_spell_scripts_ex7()
{
    RegisterSpellScript(spell_pal_hammer_of_light_ex);
    RegisterSpellScript(spell_pal_dawnlight_ex);
    RegisterSpellScript(spell_pal_second_sunrise_ex);
    RegisterSpellScript(spell_pal_valiance_ex);
}
