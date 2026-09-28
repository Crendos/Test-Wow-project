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
    SPELL_EX6_HOLY_PRISM                = 114165,
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
    // 427441 подменяет только семейство Пробуждения зол (маска класса).
    // С 12.0.0 у Защиты кнопка — отдельная аура 1246643: MiscValue = 375576,
    // замена на 427453. Без неё Благовест на панели не становится Молотом Света.
    SPELL_EX6_HAMMER_OF_LIGHT_BUFF      = 427441,
    SPELL_EX6_HAMMER_OF_LIGHT_TOLL      = 1246643,
    SPELL_EX6_SPEC_AURA_PROT            = 137028
};

namespace
{
    [[nodiscard]] bool KnowsSpellOrAura(Unit const* unit, uint32 spellId)
    {
        return unit && (unit->HasAura(spellId) || unit->HasSpell(spellId));
    }

    [[nodiscard]] bool IsProtectionPaladin(Player const* player)
    {
        if (!player)
            return false;
        if (player->GetPrimarySpecialization() == ChrSpecialization::PaladinProtection)
            return true;
        // запасной гейт, если специализация на персонаже не проставлена
        return player->HasAura(SPELL_EX6_SPEC_AURA_PROT) || player->HasSpell(SPELL_EX6_JUDGMENT_PROT);
    }

    void ForceAuraDuration(Unit* unit, uint32 spellId, int32 durationMs)
    {
        Aura* aura = unit ? unit->GetAura(spellId) : nullptr;
        if (!aura || durationMs <= 0)
            return;
        if (aura->GetMaxDuration() < durationMs)
            aura->SetMaxDuration(durationMs);
        if (aura->GetDuration() < durationMs)
            aura->SetDuration(durationMs);
    }

    void EnsureStacks(Aura* aura, uint8 stacks)
    {
        if (!aura || stacks <= 1)
            return;
        uint32 cap = aura->GetSpellInfo()->StackAmount;
        if (cap > 0 && stacks > cap)
            stacks = static_cast<uint8>(cap);
        while (aura->GetStackAmount() < stacks)
            aura->ModStackAmount(1);
    }

    // Прот: 1246643 (Благовест → Молот). Если спелла нет в данных сервера — 427441
    // (иконка не сменится: у 427441 в клиенте нет Благовеста). Рет эту функцию не зовёт.
    uint32 GrantProtectionHammerButton(Unit* caster)
    {
        uint32 button = SPELL_EX6_HAMMER_OF_LIGHT_BUFF;
        if (sSpellMgr->GetSpellInfo(SPELL_EX6_HAMMER_OF_LIGHT_TOLL, DIFFICULTY_NONE))
            button = SPELL_EX6_HAMMER_OF_LIGHT_TOLL;
        else
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                TC_LOG_ERROR("scripts", "Paladin: спелл 1246643 отсутствует — Божественный благовест не станет Молотом Света. Нужны данные 12.0+.");
            }
        }

        caster->CastSpell(caster, button, CastSpellExtraArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR));
        ForceAuraDuration(caster, button, 20000);
        return button;
    }
}

// 375576 - Гневилище: кастует основную способность по 5 ближайшим врагам.
//   Свет -> Святое сияние, Защита -> Щит мстителя, Воздаяние -> Правосудие
//   (+375609: следующие Правосудия +50% урона, 8с).
class spell_pal_divine_toll_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        // 1246643 в Validate не ставим: нет спелла в старых данных — скрипт Звона
        // не должен выгрузиться целиком. Проверка — в GrantProtectionHammerButton.
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

        // Резонанс света. Длительность не берём из данных как есть: клиент пишет
        // 15с, а серверная длительность 386730 бывает 4с или 10с — иконка гаснет
        // раньше (лог: ~4–5с; в игре отмена на 10-й секунде при таймере 15).
        CastSpellExtraArgs resonanceArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR);
        resonanceArgs.SetTriggeringSpell(GetSpell());
        if (KnowsSpellOrAura(caster, SPELL_EX6_DIVINE_RESONANCE_RET))
        {
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_RET_BUFF, resonanceArgs);
            // 12.0: следующие 3 Правосудия, 30с (не 2 и не 15).
            ForceAuraDuration(caster, SPELL_EX6_DIVINE_RESONANCE_RET_BUFF, 30000);
            EnsureStacks(caster->GetAura(SPELL_EX6_DIVINE_RESONANCE_RET_BUFF), 3);
        }
        if (KnowsSpellOrAura(caster, SPELL_EX6_DIVINE_RESONANCE_PROT))
        {
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, resonanceArgs);
            ForceAuraDuration(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, 15000);
        }
        // АУДИТ26.09: Холи-Резонанс не выдавался (нет своей талант-ауры в цепочке)
        // — гейт по379391 оставлен как был; тик386730 для Холи → Святая вспышка
        // (spec-ветка в spell_pal_divine_resonance_prot_ex). 379391 не расширяем.
        if (KnowsSpellOrAura(caster, SPELL_EX6_DIVINE_RESONANCE_HOLY))
        {
            caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, resonanceArgs);
            ForceAuraDuration(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, 15000);
        }

        // Свет наставления (427445): ПРОТ — Благовест заменяется на Молот Света
        // на 20с аурой 1246643 (не 427441: та подменяет только Пробуждение зол).
        // РЕТ получает Молот от Пробуждения зол — spell_pal_lights_guidance_wake_ex.
        if (KnowsSpellOrAura(caster, SPELL_EX6_LIGHTS_GUIDANCE))
            if (Player* p = caster->ToPlayer())
                if (IsProtectionPaladin(p))
                    GrantProtectionHammerButton(caster);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_toll_ex::HandleAfterCast);
    }
};

// 1266308 - Резонанс света (Воздаяние): следующие 3 Правосудия кастуются
// повторно на 100% (стак списывается ПОСЛЕ эха, иначе последний — и единственный —
// стак съедался без повторного каста).
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

        if (Unit* target = eventInfo.GetActionTarget())
            caster->CastSpell(target, eventInfo.GetSpellInfo()->Id, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_GCD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = eventInfo.GetProcSpell()
            });

        // эхо уже ушло; triggered-каст CheckProc не пропускает, цикла нет
        if (Aura* still = GetAura())
            still->ModStackAmount(-1, AURA_REMOVE_BY_ENEMY_SPELL);
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

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // Клиентский тултип 386730 — 15с (DurationIndex 8). Серверные данные
        // короче: аура снимается на 4с или на 10-й секунде, а иконка ещё пишет 15.
        ForceAuraDuration(GetTarget(), SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, 15000);
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
        OnEffectApply += AuraEffectApplyFn(spell_pal_divine_resonance_prot_ex::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
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

        // Тултип: вы и ещё максимум 5 союзников. Кастера лечим отдельно выше.
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

        int32 count = 5;
        if (AuraEffect const* cap = caster->GetAuraEffect(SPELL_EX6_GOLDEN_PATH, EFFECT_1))
            if (cap->GetAmount() > 0.0)
                count = int32(cap->GetAmount());

        for (Unit* ally : allies)
        {
            if (ally == caster)
                continue;
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

        // Свет небес — только у Света. Защита и Воздаяние усиливают только Вспышку Света.
        if (GetSpellInfo()->Id == 82326)
            if (Player const* player = caster->ToPlayer())
                if (player->GetPrimarySpecialization() != ChrSpecialization::PaladinHoly)
                    return;

        float bonus = 30.f;
        if (AuraEffect const* pct = caster->GetAuraEffect(SPELL_EX6_SELFLESS_HEALER, EFFECT_0))
            if (pct->GetAmount() > 0.0)
                bonus = float(pct->GetAmount());
        AddPct(pctMod, bonus);
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || target == caster || !caster->HasAura(SPELL_EX6_SELFLESS_HEALER))
            return;

        if (GetSpellInfo()->Id == 82326)
            if (Player const* player = caster->ToPlayer())
                if (player->GetPrimarySpecialization() != ChrSpecialization::PaladinHoly)
                    return;

        float sharedPct = 40.f;
        if (AuraEffect const* pct = caster->GetAuraEffect(SPELL_EX6_SELFLESS_HEALER, EFFECT_1))
            if (pct->GetAmount() > 0.0)
                sharedPct = float(pct->GetAmount());

        int64 shared = CalculatePct(static_cast<int64>(GetHitHeal()), sharedPct);
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

// 403530 - Наказание: успешный интеррапт Укора.
// Воздаяние и Защита — Удар воина Света. Свет — Шок небес.
class spell_pal_punishment_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_CRUSADER_STRIKE, SPELL_EX6_HOLY_SHOCK, SPELL_EX6_REBUKE });
    }

    bool CheckProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || spellInfo->Id != SPELL_EX6_REBUKE)
            return false;

        return (eventInfo.GetHitMask() & PROC_HIT_INTERRUPT) != 0;
    }

    void HandleProc(AuraEffect* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        Unit* caster = GetTarget();
        if (!caster)
            return;

        uint32 followUp = SPELL_EX6_CRUSADER_STRIKE;
        if (Player* player = caster->ToPlayer())
            if (player->GetPrimarySpecialization() == ChrSpecialization::PaladinHoly)
                followUp = SPELL_EX6_HOLY_SHOCK;

        Unit* victim = eventInfo.GetProcTarget();
        if (!victim)
            victim = caster->GetVictim();
        if (!victim)
            return;

        caster->CastSpell(victim, followUp, CastSpellExtraArgsInit{
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

struct GuidedPrayerMod
{
    float Multiplier = 0.6f;
};

// 326734 - Исцеляющие длани: КД ЛаО снижается до 60% по недостающему
// здоровью цели; Торжество на себя усиливается максимум на 100%.
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
        if (GetSpell())
            if (auto const* mod = std::any_cast<GuidedPrayerMod>(&GetSpell()->m_customArg))
                pctMod *= mod->Multiplier;

        Unit* caster = GetCaster();
        if (!caster || !victim || victim != caster || !caster->HasAura(SPELL_EX6_HEALING_HANDS))
            return;

        float missingFrac = 1.f - caster->GetHealthPct() / 100.f;
        // Эффект 1 = 30 (старое значение). Тултип и эффект 2 = до 100%.
        if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX6_HEALING_HANDS, EFFECT_2))
            AddPct(pctMod, bonus->GetAmount() * missingFrac);
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_healing_hands_wog_ex::CalculateHealing);
    }
};

// 404357 - Наставляемая молитва: ниже 25% здоровья — бесплатное Торжество
// с эффективностью 60% (эффект 1). ICD 60с в spell_proc.
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
        float mult = 0.6f;
        if (AuraEffect const* pct = GetEffect(EFFECT_1))
            if (pct->GetAmount() > 0.0)
                mult = float(pct->GetAmount() / 100.0);

        CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_DONT_REPORT_CAST_ERROR);
        args.SetTriggeringSpell(eventInfo.GetProcSpell());
        args.CustomArg = GuidedPrayerMod{ mult };
        GetTarget()->CastSpell(GetTarget(), SPELL_EX6_WORD_OF_GLORY, args);
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

// 114165 Святая призма (Холи): wowhead 386732 — Резонанс также от Призмы, не только от Звона.
class spell_pal_divine_resonance_prism_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX6_DIVINE_RESONANCE_HOLY, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX6_DIVINE_RESONANCE_HOLY))
            return;
        if (Player* player = caster->ToPlayer())
            if (player->GetPrimarySpecialization() != ChrSpecialization::PaladinHoly)
                return;

        caster->CastSpell(caster, SPELL_EX6_DIVINE_RESONANCE_PROT_AURA, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_resonance_prism_ex::HandleAfterCast);
    }
};

void AddSC_paladin_spell_scripts_ex6()
{
    RegisterSpellScript(spell_pal_divine_toll_ex);
    RegisterSpellScript(spell_pal_divine_resonance_prism_ex);
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
