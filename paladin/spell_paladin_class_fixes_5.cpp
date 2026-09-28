// ============================================================================
// Paladin 12.1.0 class fixes — часть 4b: Свет (маяки + триггеры АН).
// Вставка после части 4a. Регистрация: AddSC_paladin_spell_scripts_ex5().
// Спутник: paladin_class_fixes_4.sql (внизу).
//
// Маяки: перенос на все маяки (53563/156910/200025) делает spell_pal_light_s_beacon_ex
// из части 1. Маяк веры (156910) сам является маяком (своя аура + триггер 53651),
// отдельный скрипт ему не нужен.
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

    SPELL_EX5_AVENGING_CRUSADER         = 216331,  // Рыцарь мститель (баф, 15 с)
    SPELL_EX5_AVENGING_CRUSADER_TALENT  = 394088,  // скрытая пассивка таланта
    SPELL_EX5_AVENGING_CRUSADER_HEAL    = 216371,  // лечение от Крестового удара/Правосудия
    SPELL_EX5_CRUSADER_STRIKE           = 35395,
    SPELL_EX5_JUDGMENT_RET              = 20271,
    SPELL_EX5_JUDGMENT_PROT             = 275779,
    SPELL_EX5_JUDGMENT_HOLY             = 275773,

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


// 200025 - Маяк добродетели: аура-маяк на цель + до 4 раненых союзников (9 с).
// Эффекты спелла — Apply Aura (не SPELL_EFFECT_DUMMY), тип области в данных может
// отличаться, поэтому цели не фильтруем в выборке, а подрезаем после каста:
// основная цель + E1 (4) самых раненых, с остальных аура снимается; недостающих
// раненых членов группы в 30 м добираем сами.
// Сам 200025 не триггерит 53651, поэтому прок-аура переноса вешается на паладина.
class spell_pal_beacon_of_virtue_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_BEACON_OF_VIRTUE, SPELL_EX_LIGHTS_BEACON });
    }

    void CollectTarget()
    {
        if (Unit* target = GetHitUnit())
            _hitTargets.push_back(target->GetGUID());
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        size_t extra = 4;
        if (GetSpellInfo()->GetEffects().size() > EFFECT_1)
            if (int32 value = GetSpellInfo()->GetEffect(EFFECT_1).CalcValue(caster); value > 0)
                extra = size_t(value);

        Unit* mainTarget = GetExplTargetUnit();
        std::vector<Unit*> others;
        for (ObjectGuid const& guid : _hitTargets)
            if (Unit* unit = ObjectAccessor::GetUnit(*caster, guid))
                if (unit != mainTarget && std::find(others.begin(), others.end(), unit) == others.end())
                    others.push_back(unit);

        std::stable_sort(others.begin(), others.end(), [](Unit const* a, Unit const* b)
        {
            return a->GetHealthPct() < b->GetHealthPct();
        });

        size_t kept = 0;
        for (Unit* unit : others)
        {
            if (kept < extra && !unit->IsFullHealth())
            {
                ++kept;
                continue;
            }
            unit->RemoveAurasDueToSpell(SPELL_EX5_BEACON_OF_VIRTUE, caster->GetGUID());
        }

        // Ретейл: цель + E1 раненых союзников в 30 м. Если область из данных дала меньше —
        // добираем самых раненых членов группы/рейда в 30 м от основной цели.
        if (kept < extra && mainTarget)
        {
            float const radius = 30.f;
            std::vector<Unit*> nearby;
            Trinity::AnyFriendlyUnitInObjectRangeCheck check(mainTarget, caster, radius);
            Trinity::UnitListSearcher searcher(mainTarget, nearby, check);
            Cell::VisitAllObjects(mainTarget, searcher, radius);

            std::vector<Unit*> candidates;
            for (Unit* unit : nearby)
            {
                if (unit == mainTarget || !unit->IsAlive() || unit->IsFullHealth())
                    continue;
                if (unit->HasAura(SPELL_EX5_BEACON_OF_VIRTUE, caster->GetGUID()))
                    continue;
                if (unit != caster && !unit->IsInRaidWith(caster))
                    continue;
                candidates.push_back(unit);
            }
            std::stable_sort(candidates.begin(), candidates.end(), [](Unit const* a, Unit const* b)
            {
                return a->GetHealthPct() < b->GetHealthPct();
            });
            for (Unit* unit : candidates)
            {
                if (kept >= extra)
                    break;
                if (caster->AddAura(SPELL_EX5_BEACON_OF_VIRTUE, unit))
                    ++kept;
            }
        }

        caster->CastSpell(caster, SPELL_EX_LIGHTS_BEACON, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_beacon_of_virtue_ex::CollectTarget);
        AfterCast += SpellCastFn(spell_pal_beacon_of_virtue_ex::HandleAfterCast);
    }

    std::vector<ObjectGuid> _hitTargets;
};

// PAL_TYRS_FIX_20260928 — Избавление Тира (талант 1241275, Свет) по тултипу 12.x:
//   «Activating Avenging Wrath releases the Light within yourself, healing 5 injured allies
//    instantly and an injured ally every 1 sec within 40 yds for (35.1% of Spell Power).
//    Allies healed also receive 10% increased healing from your Holy Light, Flash of Light,
//    and Holy Shock spells for 12 sec.»
// Что было не так раньше: скрипт висел на самом таланте (1241275) с хуком AfterCast, но
// 1241275 — пассивка-«Apply Aura: Proc Trigger Spell», её никто не кастует, поэтому хук не
// срабатывал никогда, и Избавление Тира просто ничего не делало.
// Теперь: активация Гнева карателя (31884/454351/216331/231895) выдаёт 200652; аура 200652
// сразу лечит 5 раненых союзников и далее по одному каждую секунду (свою периодику ауры
// глушим — тики ведём таймером, чтобы длительность и число тиков не зависели от данных).
class spell_pal_tyrs_deliverance_aw_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_TYRS_DELIVERANCE, SPELL_EX5_TYRS_DELIVERANCE_AURA });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX5_TYRS_DELIVERANCE))
            return;
        if (!sSpellMgr->GetSpellInfo(SPELL_EX5_TYRS_DELIVERANCE_AURA, DIFFICULTY_NONE))
        {
            TC_LOG_ERROR("scripts", "Paladin: Избавление Тира — нет данных 200652, эффект пропущен");
            return;
        }

        CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR);
        args.SetTriggeringSpell(GetSpell());
        caster->CastSpell(caster, SPELL_EX5_TYRS_DELIVERANCE_AURA, args);

        static bool logged = false;
        if (!logged)
        {
            TC_LOG_INFO("scripts", "Paladin: Избавление Тира — Свет выпущен (200652, 12 с: 5 сразу + 1/сек)");
            logged = true;
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_tyrs_deliverance_aw_ex::HandleAfterCast);
    }
};

// 200652 — аура Избавления Тира (12 с): сразу 5 раненых союзников в 40 м, затем по одному
// в секунду. Лечим заклинанием 200654 (в нём же и +10% к лечению от Св. света/Вспышки/Шока).
class spell_pal_tyrs_deliverance_aura_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_TYRS_DELIVERANCE_HEAL });
    }

    void HealInjured(Unit* caster, uint32 count)
    {
        if (!caster || !count)
            return;
        if (!sSpellMgr->GetSpellInfo(SPELL_EX5_TYRS_DELIVERANCE_HEAL, DIFFICULTY_NONE))
            return;

        AuraEffect* trigger = GetEffect(EFFECT_0);
        for (Unit* ally : ExCollectInjuredAllies(caster, 40.f, count))
        {
            CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR);
            if (trigger)
                args.SetTriggeringAura(trigger);
            caster->CastSpell(ally, SPELL_EX5_TYRS_DELIVERANCE_HEAL, args);
        }
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetTarget();
        Aura* aura = GetAura();
        if (!caster || !aura)
            return;

        // В данных длительность 12 с; если пришло что-то другое (0/бесконечность) — правим,
        // иначе лечение могло бы идти вечно.
        int32 const dataDuration = aura->GetMaxDuration();
        if (dataDuration <= 0 || dataDuration > 60000)
        {
            aura->SetMaxDuration(12000);
            aura->SetDuration(12000);
            static bool logged = false;
            if (!logged)
            {
                TC_LOG_INFO("scripts", "Paladin: Избавление Тира — в данных {} мс, ставлю 12 с", dataDuration);
                logged = true;
            }
        }

        int32 const duration = aura->GetDuration() > 0 ? aura->GetDuration() : 12000;

        // Мгновенно 5 раненых союзников.
        HealInjured(caster, 5);

        // Далее по одному союзнику в секунду; последний тик — чуть раньше края ауры
        // (событие и истечение ауры обрабатываются в одном тике мира).
        int32 const ticks = std::clamp(duration / 1000, 1, 30);
        for (int32 i = 1; i <= ticks; ++i)
        {
            int32 const offset = (i == ticks) ? std::max(250, duration - 250) : i * 1000;
            if (offset <= 0 || offset > 60000)
                continue;

            caster->m_Events.AddEventAtOffset([caster]()
            {
                if (!caster->IsAlive() || !caster->HasAura(SPELL_EX5_TYRS_DELIVERANCE_AURA))
                    return;

                Aura* aura2 = caster->GetAura(SPELL_EX5_TYRS_DELIVERANCE_AURA);
                if (!aura2 || !sSpellMgr->GetSpellInfo(SPELL_EX5_TYRS_DELIVERANCE_HEAL, DIFFICULTY_NONE))
                    return;

                AuraEffect* trigger2 = aura2->GetEffect(EFFECT_0);
                for (Unit* ally : ExCollectInjuredAllies(caster, 40.f, 1))
                {
                    CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR);
                    if (trigger2)
                        args.SetTriggeringAura(trigger2);
                    caster->CastSpell(ally, SPELL_EX5_TYRS_DELIVERANCE_HEAL, args);
                }
            }, Milliseconds(offset));
        }
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction(); // тики ведёт таймер из OnApply (см. пояснение выше)
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_pal_tyrs_deliverance_aura_ex::OnApply, EFFECT_FIRST_FOUND, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_tyrs_deliverance_aura_ex::OnPeriodic, EFFECT_ALL, SPELL_AURA_ANY);
    }
};

// PAL_AVENGING_CRUSADER_20260928 — Рыцарь мститель (216331, Свет; кнопка заменяет Гнев
// карателя): пока баф висит, Крестовый удар и Правосудие лечат до 5 раненых союзников в
// 40 м на 55% нанесённого урона, разделённые поровну (значения — из эффектов E4/E5 бафа).
// Урон автоатак (+1000%) и прочие модификаторы делает сама аура из данных; серверных
// эффектов у неё (Dummy) нет — лечение делаем мы.
class spell_pal_avenging_crusader_heal_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX5_AVENGING_CRUSADER, SPELL_EX5_AVENGING_CRUSADER_HEAL });
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        Aura* crusader = caster->GetAura(SPELL_EX5_AVENGING_CRUSADER);
        if (!crusader)
            crusader = caster->GetAura(SPELL_EX5_AVENGING_CRUSADER_TALENT);
        if (!crusader)
            return;

        int32 const damage = GetHitDamage();
        if (damage <= 0)
            return;

        float pct = 55.f;
        if (AuraEffect const* eff = caster->GetAuraEffect(SPELL_EX5_AVENGING_CRUSADER, EFFECT_4))
            if (eff->GetAmount() > 0.f)
                pct = float(eff->GetAmount());

        int32 maxAllies = 5;
        if (AuraEffect const* eff = caster->GetAuraEffect(SPELL_EX5_AVENGING_CRUSADER, EFFECT_5))
            if (eff->GetAmount() > 0.f)
                maxAllies = int32(eff->GetAmount());

        std::vector<Unit*> allies = ExCollectInjuredAllies(caster, 40.f, uint32(std::max(1, maxAllies)));
        if (allies.empty())
            return;

        int32 const total = int32(CalculatePct(damage, pct));
        int32 const perAlly = std::max(1, total / int32(allies.size()));

        for (Unit* ally : allies)
        {
            CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR);
            args.SetTriggeringSpell(GetSpell());
            args.AddSpellMod(SPELLVALUE_BASE_POINT0, perAlly);
            caster->CastSpell(ally, SPELL_EX5_AVENGING_CRUSADER_HEAL, args);
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_avenging_crusader_heal_ex::HandleAfterHit);
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

// 157047 - Спасённый Светом: перенесён в основную часть (UnitScript
// spell_pal_saved_by_the_light_tracker) — прок-аура паладина не видит урон по союзнику.

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
    RegisterSpellScript(spell_pal_beacon_of_virtue_ex);
    RegisterSpellScript(spell_pal_tyrs_deliverance_aw_ex);
    RegisterSpellScript(spell_pal_tyrs_deliverance_aura_ex);
    RegisterSpellScript(spell_pal_avenging_crusader_heal_ex);
    RegisterSpellScript(spell_pal_hand_of_divinity_ex);
    RegisterSpellScript(spell_pal_refining_fire_ex);
}
