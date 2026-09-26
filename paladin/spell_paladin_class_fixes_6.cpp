// ============================================================================
// Paladin 12.1.0 class fixes — часть 5: классовое дерево (Divine Toll,
// Резонансы света, Золотая тропа, Бескорыстный целитель, Наказание,
// Исцеляющие длани, Наставляемая молитва, Ауры твердыни).
// Вставка после части 4b. Регистрация: AddSC_paladin_spell_scripts_ex6().
// Спутник: paladin_class_fixes_5.sql
// 26.09.2026 (PROC_FIX-дополнение): Звон (375576) выдает кнопку Молота Света
//   (427441), если талант Наставления Света (427445) известен. Маску прока
//   427445 обнулили (PROC_FIX.sql — прок срабатывал с ЛЮБОГО каста, включая
//   маунт), поэтому выдачу делаем узким путем — строго от каста Звона.
// ============================================================================

// === CUT HERE ===============================================================

enum PaladinEx6Spells
{
    SPELL_EX6_JUDGMENT_RET              = 20271,
    SPELL_EX6_JUDGMENT_PROT             = 275779,
    SPELL_EX6_JUDGMENT_HOLY             = 275773,
    SPELL_EX6_HOLY_SHOCK                = 20473,
    SPELL_EX6_AVENGERS_SHIELD           = 31935,
    SPELL_EX6_REBUKE                    = 96231,
    SPELL_EX6_CRUSADER_STRIKE           = 35395,
    SPELL_EX6_BLESSED_HAMMER            = 204019,
    SPELL_EX6_WORD_OF_GLORY             = 85673,
    SPELL_EX6_LAY_ON_HANDS              = 633,

    // таланты/баффы
    SPELL_EX6_DIVINE_TOLL               = 375576,
    SPELL_EX6_DIVINE_TOLL_RET_DEBUFF    = 375609,
    SPELL_EX6_DIVINE_RESONANCE_RET      = 384027,
    SPELL_EX6_DIVINE_RESONANCE_RET_BUFF = 1266308,
    SPELL_EX6_DIVINE_RESONANCE_PROT     = 386738,
    SPELL_EX6_DIVINE_RESONANCE_PROT_AURA = 386730,
    // Холи-токен Резонанса:379391 Quickened Invocation (wowhead:386731 «После
    // Призмы/Вооружения/Звона — Святая вспышка каждые5с»; Related = только он)
    SPELL_EX6_DIVINE_RESONANCE_HOLY     = 379391,
    SPELL_EX6_GOLDEN_PATH               = 377128,
    SPELL_EX6_GOLDEN_PATH_HEAL          = 339119,
    SPELL_EX6_SELFLESS_HEALER           = 469434,
    SPELL_EX6_PUNISHMENT                = 403530,
    SPELL_EX6_HEALING_HANDS             = 326734,
    SPELL_EX6_GUIDED_PRAYER             = 404357,
    SPELL_EX6_AURAS_OF_THE_RESOLUTE     = 385633,

    SPELL_EX6_AURA_DEVOTION             = 465,
    SPELL_EX6_AURA_CRUSADER             = 32223,
    SPELL_EX6_AURA_CONCENTRATION        = 317920,

    // Наставления Света (герой-талант): Звон выдает кнопку Молота Света
    SPELL_EX6_LIGHTS_GUIDANCE           = 427445,
    SPELL_EX6_HAMMER_OF_LIGHT_BUFF      = 427441
};

// 375576 - Гневилище: кастует основную способность по 5 ближайшим врагам.
//   Свет -> Святое сияние, Защита -> Щит мстителя, Воздаяние -> Правосудие
//   (+375609: следующие Правосудия +50% урона, 8с).
class spell_pal_divine_toll_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_HOLY_SHOCK, SPELL_EX6_AVENGERS_SHIELD,
            SPELL_EX6_JUDGMENT_RET, SPELL_EX6_DIVINE_TOLL_RET_DEBUFF,
            SPELL_EX6_LIGHTS_GUIDANCE, SPELL_EX6_HAMMER_OF_LIGHT_BUFF,
            SPELL_EX6_DIVINE_RESONANCE_RET_BUFF, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA,
            SPELL_EX6_DIVINE_RESONANCE_HOLY });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        float const radius = 30.f;
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, radius);
        Trinity::UnitListSearcher searcher(caster, enemies, check);
        Cell::VisitAllObjects(caster, searcher, radius);

        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [caster](Unit* enemy)
        {
            return !caster->IsValidAttackTarget(enemy) || !enemy->IsWithinLOSInMap(caster);
        }), enemies.end());

        std::sort(enemies.begin(), enemies.end(), [caster](Unit* a, Unit* b)
        {
            return a->GetDistance2d(caster) < b->GetDistance2d(caster);
        });
        if (enemies.size() > 5)
            enemies.resize(5);

        uint32 spellId = 0;
        if (Player* player = caster->ToPlayer())
        {
            switch (player->GetPrimarySpecialization())
            {
                case ChrSpecialization::PaladinHoly:
                    spellId = SPELL_EX6_HOLY_SHOCK;
                    break;
                case ChrSpecialization::PaladinProtection:
                    spellId = SPELL_EX6_AVENGERS_SHIELD;
                    break;
                case ChrSpecialization::PaladinRetribution:
                    spellId = SPELL_EX6_JUDGMENT_RET;
                    break;
                default:
                    spellId = SPELL_EX6_JUDGMENT_RET;
                    break;
            }
        }

        for (Unit* enemy : enemies)
            caster->CastSpell(enemy, spellId, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });

        // Воздаяние: усиленные Правосудия
        if (spellId == SPELL_EX6_JUDGMENT_RET)
            caster->CastSpell(caster, SPELL_EX6_DIVINE_TOLL_RET_DEBUFF,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));

        // Резонанс света
        if (caster->HasAura(SPELL_EX6_DIVINE_RESONANCE_RET))
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_RET_BUFF,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
        if (caster->HasAura(SPELL_EX6_DIVINE_RESONANCE_PROT))
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));
        // АУДИТ26.09: Холи-Резонанс не выдавался (нет своей талант-ауры в цепочке)
        // — добавлен гейт по379391; тик386730 для Холи → Святая вспышка
        // (spec-ветка в spell_pal_divine_resonance_prot_ex)
        if (caster->HasAura(SPELL_EX6_DIVINE_RESONANCE_HOLY))
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA,
                CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                    .SetTriggeringSpell(GetSpell()));

        // Свет наставления (427445, wowhead12.1): ПРОТ — Божественный звон
        // заменяется на Молот Света (427441) на20с. РЕТ получает Молот от
        // Пробуждения зол — spell_pal_lights_guidance_wake_ex (часть 6, fix_7).
        // После PROC_FIX (маска427445 обнулена) это узкий путь вместо прока.
        if (caster->HasAura(SPELL_EX6_LIGHTS_GUIDANCE))
            if (Player* p = caster->ToPlayer())
                if (p->GetPrimarySpecialization() == ChrSpecialization::PaladinProtection)
                    caster->CastSpell(caster, SPELL_EX6_HAMMER_OF_LIGHT_BUFF,
                        CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR)
                            .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_toll_ex::HandleAfterCast);
    }
};

// 1266308 - Резонанс света (Воздаяние): следующие 2 Правосудия кастуются
// повторно на 100% (стак потребляется, Правосудие кастуется снова бесплатно).
class spell_pal_divine_resonance_ret_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_JUDGMENT_RET, SPELL_EX6_JUDGMENT_PROT, SPELL_EX6_JUDGMENT_HOLY });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || (spellInfo->Id != SPELL_EX6_JUDGMENT_RET
            && spellInfo->Id != SPELL_EX6_JUDGMENT_PROT
            && spellInfo->Id != SPELL_EX6_JUDGMENT_HOLY))
            return false;

        // наши собственные ре-касты (triggered) не прокают повторно — иначе бесконечный цикл Правосудий
        if (Spell const* procSpell = eventInfo.GetProcSpell())
            if (procSpell->IsTriggered())
                return false;

        return true;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        Unit* caster = GetTarget();
        if (!caster)
            return;

        Aura* aura = GetAura();
        if (!aura) // аура потеряна — ре-каст без списания стаков дал бы бесконечный цикл
            return;

        aura->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
        if (aura->GetStackAmount() <= 0)
            return;

        if (Unit* target = eventInfo.GetActionTarget())
            caster->CastSpell(target, eventInfo.GetSpellInfo()->Id, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_divine_resonance_ret_ex::CheckProc, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectProc += AuraEffectProcFn(spell_pal_divine_resonance_ret_ex::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 386730 - Резонанс света (Защита): каждые 5с — бесплатный Щит мстителя
// (ядро триггерит пустой 386731; перехватываем и кастуем настоящий 31935).
class spell_pal_divine_resonance_prot_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_AVENGERS_SHIELD, SPELL_EX6_HOLY_SHOCK });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        if (!target)
            return;

        PreventDefaultAction();

        // АУДИТ26.09: spec-ветка тика — Холи → Святая вспышка (20473, wowhead386732:
        // «Holy: instantly cast Holy Shock»); Прот → Щит мстителя (31935);
        // Рет на этом бафе не висит (его Резонанс = отдельный1266308).
        uint32 tickSpell = SPELL_EX6_AVENGERS_SHIELD;
        if (Player* pl = target->ToPlayer())
            if (pl->GetPrimarySpecialization() == ChrSpecialization::PaladinHoly)
                tickSpell = SPELL_EX6_HOLY_SHOCK;

        float const radius = 30.f;
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, target, radius);
        Trinity::UnitListSearcher searcher(target, enemies, check);
        Cell::VisitAllObjects(target, searcher, radius);

        Unit* best = nullptr;
        float bestDist = 0.f;
        for (Unit* enemy : enemies)
        {
            if (!target->IsValidAttackTarget(enemy) || !enemy->IsWithinLOSInMap(target))
                continue;
            float dist = enemy->GetDistance2d(target);
            if (!best || dist < bestDist)
            {
                best = enemy;
                bestDist = dist;
            }
        }

        if (best)
            target->CastSpell(best, tickSpell, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringAura = GetEffect(EFFECT_0)
            });
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_divine_resonance_prot_ex::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

// 377128 - Золотая тропа: тик Освящения лечит вас и союзников рядом (339119).
class spell_pal_golden_path_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_GOLDEN_PATH, SPELL_EX6_GOLDEN_PATH_HEAL });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX6_GOLDEN_PATH))
            return;

        caster->CastSpell(caster, SPELL_EX6_GOLDEN_PATH_HEAL, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        // v1: ещё 4 самых раненых союзника рядом (в версии с АТ — по зоне Освящения)
        float const radius = 10.f;
        std::vector<Unit*> allies;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(caster, caster, radius, true);
        Trinity::UnitListSearcher searcher(caster, allies, check);
        Cell::VisitAllObjects(caster, searcher, radius);

        allies.erase(std::remove_if(allies.begin(), allies.end(), [](Unit* u)
        {
            return !u->IsAlive() || u->IsFullHealth();
        }), allies.end());
        std::sort(allies.begin(), allies.end(), [](Unit* a, Unit* b)
        {
            return a->GetHealthPct() < b->GetHealthPct();
        });

        int32 count = 4;
        for (Unit* ally : allies)
        {
            caster->CastSpell(ally, SPELL_EX6_GOLDEN_PATH_HEAL, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
            if (--count <= 0)
                break;
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_golden_path_ex::HandleHitTarget);
    }
};

// 469434 - Бескорыстный целитель: ФоЛ и Св. свет на союзников +30%,
// 40% их лечения идёт вам.
class spell_pal_selfless_healer_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_SELFLESS_HEALER, SPELL_EX6_GOLDEN_PATH_HEAL });
    }

    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || victim == caster || !caster->HasAura(SPELL_EX6_SELFLESS_HEALER))
            return;

        AddPct(pctMod, 30);
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || target == caster || !caster->HasAura(SPELL_EX6_SELFLESS_HEALER))
            return;

        int64 shared = CalculatePct(static_cast<int64>(GetHitHeal()), 40);
        if (shared <= 0)
            return;

        caster->CastSpell(caster, SPELL_EX6_GOLDEN_PATH_HEAL, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_BASE_POINT0, int32(shared)));
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_selfless_healer_ex::CalculateHealing);
        AfterHit += SpellHitFn(spell_pal_selfless_healer_ex::HandleHitTarget);
    }
};

// 403530 - Наказание: успешный интеррапт (Реприманд/Щит мстителя) —
// бесплатный финишер спека (Удар крестоносца / Благословенный молот / Св. сияние).
class spell_pal_punishment_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_CRUSADER_STRIKE, SPELL_EX6_BLESSED_HAMMER, SPELL_EX6_HOLY_SHOCK });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        return spellInfo && (spellInfo->Id == SPELL_EX6_REBUKE || spellInfo->Id == SPELL_EX6_AVENGERS_SHIELD);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        Unit* target = GetTarget();
        if (!target)
            return;

        uint32 filler = SPELL_EX6_CRUSADER_STRIKE;
        if (Player* player = target->ToPlayer())
            switch (player->GetPrimarySpecialization())
            {
                case ChrSpecialization::PaladinProtection:
                    filler = SPELL_EX6_BLESSED_HAMMER;
                    break;
                case ChrSpecialization::PaladinHoly:
                    filler = SPELL_EX6_HOLY_SHOCK;
                    break;
                default:
                    break;
            }

        if (Unit* victim = target->GetVictim())
            target->CastSpell(victim, filler, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringAura = GetEffect(EFFECT_0)
            });
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_punishment_ex::CheckProc, EFFECT_1, SPELL_AURA_PROC_TRIGGER_SPELL);
        OnEffectProc += AuraEffectProcFn(spell_pal_punishment_ex::HandleProc, EFFECT_1, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 326734 - Исцеляющие длани: КД ЛаО снижается до 60% по недостающему
// здоровью цели; Слово света на себя +до 30%.
class spell_pal_healing_hands_loh_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_HEALING_HANDS, SPELL_EX6_LAY_ON_HANDS });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster || !caster->HasAura(SPELL_EX6_HEALING_HANDS))
            return;

        float missingFrac = target ? std::clamp(1.f - target->GetHealthPct() / 100.f, 0.f, 1.f) : 1.f;
        AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX6_HEALING_HANDS, EFFECT_0);
        if (!bonus)
            return;

        int32 reductionMs = int32(bonus->GetAmount() * missingFrac * 6000.f);
        if (reductionMs > 0)
            caster->GetSpellHistory()->ModifyCooldown(SPELL_EX6_LAY_ON_HANDS, Milliseconds(-reductionMs));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_healing_hands_loh_ex::HandleAfterCast);
    }
};

class spell_pal_healing_hands_wog_ex : public SpellScript
{
    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim || victim != caster || !caster->HasAura(SPELL_EX6_HEALING_HANDS))
            return;

        float missingFrac = 1.f - caster->GetHealthPct() / 100.f;
        if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX6_HEALING_HANDS, EFFECT_1))
            AddPct(pctMod, bonus->GetAmount() * missingFrac);
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_healing_hands_wog_ex::CalculateHealing);
    }
};

// 404357 - Наставляемая молитва: падение ниже 25% здоровья -> бесплатное
// Слово света (v1: полной силы; ICD 60с в spell_proc).
class spell_pal_guided_prayer_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_WORD_OF_GLORY });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        return GetTarget()->HealthBelowPct(25);
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_EX6_WORD_OF_GLORY,
            CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR)
                .SetTriggeringSpell(eventInfo.GetProcSpell()));
    }

    void Register() override
    {
        DoCheckEffectProc += AuraCheckEffectProcFn(spell_pal_guided_prayer_ex::CheckProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
        OnEffectProc += AuraEffectProcFn(spell_pal_guided_prayer_ex::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 385633 - Ауры твердыни: выдаёт Концентрацию, Благословение и Крестоносца.
class spell_pal_auras_of_the_resolute_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_AURA_DEVOTION, SPELL_EX6_AURA_CRUSADER, SPELL_EX6_AURA_CONCENTRATION });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        for (uint32 auraSpell : { SPELL_EX6_AURA_DEVOTION, SPELL_EX6_AURA_CRUSADER, SPELL_EX6_AURA_CONCENTRATION })
            if (!player->HasSpell(auraSpell))
                player->LearnSpell(auraSpell, false);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_auras_of_the_resolute_ex::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_paladin_spell_scripts_ex6()
{
    RegisterSpellScript(spell_pal_divine_toll_ex);
    RegisterSpellScript(spell_pal_divine_resonance_ret_ex);
    RegisterSpellScript(spell_pal_divine_resonance_prot_ex);
    RegisterSpellScript(spell_pal_golden_path_ex);
    RegisterSpellScript(spell_pal_selfless_healer_ex);
    RegisterSpellScript(spell_pal_punishment_ex);
    RegisterSpellScript(spell_pal_healing_hands_loh_ex);
    RegisterSpellScript(spell_pal_healing_hands_wog_ex);
    RegisterSpellScript(spell_pal_guided_prayer_ex);
    RegisterSpellScript(spell_pal_auras_of_the_resolute_ex);
}
