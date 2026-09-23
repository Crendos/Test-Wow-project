// ============================================================================
// Paladin 12.1.0 class fixes — часть 7: по ID от пользователя
//   * Слава авангарда (Прот): 1267203 / 1267211 / 1267215
//   * Свет внутри (Рет, стеки): 1261113 / 1261111 / 1261159 (доработка партии 1)
//   * Маяк Спасителя (Свет): 1244878 / 1245367 / 1245368
//   * Серафимский барьер (Свет): 1241714
//   * Переполняющий свет (Свет): 461244
// (458359 «Сияющая прелесть»/Radiant Glory уже сделан в партии 1)
// Механика Авангарда сверена с simc: 20% Правосудие -> 1268810; Щит мстителя
// потребляет -> болт 1269175 (+1 HP с 1267211, Доблесть 1269179 с 1267215);
// во время АН Щит мстителя всегда с Авангардом.
// Вставка после части 7. Регистрация: AddSC_paladin_spell_scripts_ex8().
// Спутник: paladin_class_fixes_7.sql
// ============================================================================

// === CUT HERE ===============================================================

#include "EventProcessor.h"
#include "ObjectAccessor.h"

enum PaladinEx8Spells
{
    SPELL_EX8_JUDGMENT_RET              = 20271,
    SPELL_EX8_JUDGMENT_PROT             = 275779,
    SPELL_EX8_JUDGMENT_HOLY             = 275773,
    SPELL_EX8_AVENGERS_SHIELD           = 31935,
    SPELL_EX8_SOTR                      = 53600,
    SPELL_EX8_WORD_OF_GLORY             = 85673,
    SPELL_EX8_LIGHT_OF_DAWN             = 85222,
    SPELL_EX8_FLASH_OF_LIGHT            = 19750,
    SPELL_EX8_HOLY_LIGHT                = 82326,
    SPELL_EX8_ETERNAL_FLAME             = 156322,
    SPELL_EX8_HOLY_SHOCK_HEAL           = 25914,
    SPELL_EX8_HOLY_POWER_ENERGIZE       = 220637,
    SPELL_EX8_JUDGMENT_GAIN_HP          = 220637, // (совпадает)

    // Слава авангарда
    SPELL_EX8_GLORY_1                   = 1267203, // тал: 20% Правосудие -> Авангард
    SPELL_EX8_GLORY_2                   = 1267211, // +10% Правосудие (DBC); +1 HP за потребление
    SPELL_EX8_GLORY_3                   = 1267215, // Щит мстителя с Доблестью -> 1269224 x4
    SPELL_EX8_VANGUARD_BUFF             = 1268810, // маркер Авангарда
    SPELL_EX8_VANGUARD_BOLT             = 1269175, // болт Щита мстителя
    SPELL_EX8_VALOR_BUFF                = 1269179, // Доблесть (после болта с r3)
    SPELL_EX8_BLAZE_OF_GLORY            = 1269224, // преломление Щита мстителя

    // Свет внутри (доработка стека)
    SPELL_EX8_LIGHT_WITHIN_STACKS       = 1261113,
    SPELL_EX8_AW_8S                     = 454351,

    // Маяк Спасителя
    SPELL_EX8_SAVIOR_TALENT             = 1244878,
    SPELL_EX8_SAVIOR_TALENT_R2          = 1245367,
    SPELL_EX8_SAVIOR_MARKER             = 1270083, // аура на цели Спасителя
    SPELL_EX8_BEACON_HEAL_CARRIER       = 53652,

    // абсорб-носитель (общий): Оплот порядка, все школы, значение из bp
    SPELL_EX8_ABSORB_CARRIER            = 209388,

    // таланты Света
    SPELL_EX8_SERAPHIC_BARRIER          = 1241714,
    SPELL_EX8_OVERFLOWING_LIGHT         = 461244
};

// --- СЛАВА АВАНГАРДА ---------------------------------------------------------

// 1267203 - Правосудие с шансом 20% даёт Авангард (1268810).
class spell_pal_glory_of_the_vanguard_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX8_GLORY_1, SPELL_EX8_VANGUARD_BUFF });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX8_GLORY_1))
            return;
        if (roll_chance(20))
            caster->CastSpell(caster, SPELL_EX8_VANGUARD_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_glory_of_the_vanguard_ex::HandleHitTarget);
    }
};

// 31935 - Щит мстителя: с Авангардом (или в АН) — болт 1269175 через 300 мс
// ПОСЛЕ попадания щита (розничная задержка, simc: glory_of_the_vanguard_delay=300ms).
// Болт даёт +1 HP (1267211) и Доблесть 1269179 (стакается, тратится ЩП целиком).
// В АН болт летит всегда, и Авангард при этом НЕ тратится (simc: isApex3 без decrement).
class pal_vanguard_bolt_event : public BasicEvent
{
public:
    pal_vanguard_bolt_event(Unit* caster, ObjectGuid targetGuid) : _caster(caster), _targetGuid(targetGuid) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        Unit* target = ObjectAccessor::GetUnit(*_caster, _targetGuid);
        if (target && _caster->IsValidAttackTarget(target))
            _caster->CastSpell(target, SPELL_EX8_VANGUARD_BOLT, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

        if (_caster->HasAura(SPELL_EX8_GLORY_2))
            _caster->CastSpell(_caster, SPELL_EX8_HOLY_POWER_ENERGIZE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, 1 } }
            });

        if (_caster->HasAura(SPELL_EX8_GLORY_3))
            _caster->CastSpell(_caster, SPELL_EX8_VALOR_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

        return true;
    }

private:
    Unit* _caster;
    ObjectGuid _targetGuid;
};

class spell_pal_avengers_shield_vanguard_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX8_VANGUARD_BOLT, SPELL_EX8_HOLY_POWER_ENERGIZE, SPELL_EX8_VALOR_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster || !target)
            return;

        bool hasVanguard = caster->HasAura(SPELL_EX8_VANGUARD_BUFF);
        bool avengingWrath = caster->HasAura(SPELL_EX_AVENGING_WRATH); // в АН — всегда
        if (!hasVanguard && !avengingWrath)
            return;

        // Розница/simc: в АН Авангард остаётся (не тратится)
        if (hasVanguard && !avengingWrath)
            caster->RemoveAura(SPELL_EX8_VANGUARD_BUFF);

        caster->m_Events.AddEventAtOffset(new pal_vanguard_bolt_event(caster, target->GetGUID()), 300ms);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_avengers_shield_vanguard_ex::HandleAfterCast);
    }
};

// 53600 - Щит праведника: с Доблестью (1269179) — преломление 1269224
// на до 4 целей (E1 1267215).
class spell_pal_shield_of_the_righteous_vanguard_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX8_VALOR_BUFF, SPELL_EX8_BLAZE_OF_GLORY });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster || !caster->HasAura(SPELL_EX8_VALOR_BUFF) || !target)
            return;

        caster->RemoveAura(SPELL_EX8_VALOR_BUFF);

        // Розница/simc: Blaze бьёт и основную цель ЩП (execute_on_target), плюс до 4 вторичных
        caster->CastSpell(target, SPELL_EX8_BLAZE_OF_GLORY, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        float const radius = 10.f;
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, radius);
        Trinity::UnitListSearcher searcher(target, enemies, check);
        Cell::VisitAllObjects(target, searcher, radius);

        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [caster, target](Unit* enemy)
        {
            return enemy == target || !caster->IsValidAttackTarget(enemy) || !enemy->IsWithinLOSInMap(caster);
        }), enemies.end());
        if (enemies.size() > 4)
            enemies.resize(4);

        for (Unit* enemy : enemies)
            caster->CastSpell(enemy, SPELL_EX8_BLAZE_OF_GLORY, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_shield_of_the_righteous_vanguard_ex::HandleAfterCast);
    }
};

// --- МАЯК СПАСИТЕЛЯ ----------------------------------------------------------

// 1244878 - Маяк Спасителя: каждые 2с (E2) — навешиваем/переносим маркер
// 1270083 на самого раненого союзника в 30 ярдах (E0), только в бою.
class spell_pal_beacon_of_the_savior_apply_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX8_SAVIOR_MARKER });
    }

    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player || !player->IsInCombat())
            return;

        float const radius = 30.f;
        Unit* current = nullptr;
        Unit* best = nullptr;
        float bestPct = 101.f;

        std::vector<Unit*> allies;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(player, player, radius, true);
        Trinity::UnitListSearcher searcher(player, allies, check);
        Cell::VisitAllObjects(player, searcher, radius);

        for (Unit* ally : allies)
        {
            if (!ally->IsAlive() || !player->IsInRaidWith(ally))
                continue;

            if (ally->HasAura(SPELL_EX8_SAVIOR_MARKER, player->GetGUID()))
                current = ally;

            float pct = ally->GetHealthPct();
            if (pct < bestPct)
            {
                bestPct = pct;
                best = ally;
            }
        }

        if (!best)
            return;
        if (current == best)
            return;
        if (current && current->GetHealthPct() <= bestPct)
            return;

        player->CastSpell(best, SPELL_EX8_SAVIOR_MARKER, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringAura = GetEffect(EFFECT_2)
        });
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pal_beacon_of_the_savior_apply_ex::OnPeriodic, EFFECT_2, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 1244878 (+1245367) - перенос: прямое лечение другим также лечит цель
// Спасителя на 10% (+10% со вторым рангом) количества лечения.
class spell_pal_beacon_of_the_savior_transfer_ex : public SpellScript
{
    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX8_SAVIOR_TALENT))
            return;
        if (target->HasAura(SPELL_EX8_SAVIOR_MARKER, caster->GetGUID()))
            return;

        int32 heal = GetHitHeal();
        if (heal <= 0)
            return;

        int32 pct = 10;
        if (caster->HasAura(SPELL_EX8_SAVIOR_TALENT_R2))
            if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX8_SAVIOR_TALENT_R2, EFFECT_0))
                pct += bonus->GetAmount();

        Unit* savior = nullptr;
        for (Unit* candidate : { target })
            (void)candidate;

        float const radius = 40.f;
        std::vector<Unit*> allies;
        Trinity::AnyFriendlyUnitInObjectRangeCheck check(caster, caster, radius, true);
        Trinity::UnitListSearcher searcher(caster, allies, check);
        Cell::VisitAllObjects(caster, searcher, radius);

        for (Unit* ally : allies)
            if (ally->HasAura(SPELL_EX8_SAVIOR_MARKER, caster->GetGUID()))
            {
                savior = ally;
                break;
            }

        if (!savior)
            return;

        caster->CastSpell(savior, SPELL_EX8_BEACON_HEAL_CARRIER, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, CalculatePct(heal, pct) } }
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_beacon_of_the_savior_transfer_ex::HandleHitTarget);
    }
};

// --- АБСОРБ-МЕХАНИКИ СВЕТА ----------------------------------------------------

// 1241714 - Серафимский барьер: 18% хила Слова света/Заря -> щит (носитель 209388).
class spell_pal_seraphic_barrier_ex : public SpellScript
{
    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX8_SERAPHIC_BARRIER))
            return;

        int32 heal = GetHitHeal();
        if (heal <= 0)
            return;

        caster->CastSpell(GetHitUnit(), SPELL_EX8_ABSORB_CARRIER, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, CalculatePct(heal, 18) } }
        });
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_seraphic_barrier_ex::HandleHitTarget);
    }
};

// 461244 - Переполняющий свет: 50% оверхила Св. сияния -> щит, кап 10% макс. HP.
class spell_pal_overflowing_light_ex : public SpellScript
{
    void HandleOnHit()
    {
        if (Unit* target = GetHitUnit())
        {
            _healthBefore = target->GetHealth();
            _maxHealth = target->GetMaxHealth();
        }
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || !caster->HasAura(SPELL_EX8_OVERFLOWING_LIGHT))
            return;

        int32 heal = GetHitHeal();
        if (heal <= 0)
            return;

        int64 effective = std::min<int64>(heal, int64(_maxHealth - _healthBefore));
        int64 overheal = int64(heal) - effective;
        if (overheal <= 0)
            return;

        int64 cap = caster->CountPctFromMaxHealth(10);
        int64 absorb = std::min<int64>(CalculatePct(overheal, 50), cap);
        if (absorb <= 0)
            return;

        caster->CastSpell(target, SPELL_EX8_ABSORB_CARRIER, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell(),
            .SpellValueOverrides = { { SPELLVALUE_BASE_POINT0, int32(absorb) } }
        });
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_pal_overflowing_light_ex::HandleOnHit);
        AfterHit += SpellHitFn(spell_pal_overflowing_light_ex::HandleAfterHit);
    }

    uint64 _healthBefore = 0;
    uint64 _maxHealth = 1;
};

void AddSC_paladin_spell_scripts_ex8()
{
    RegisterSpellScript(spell_pal_glory_of_the_vanguard_ex);
    RegisterSpellScript(spell_pal_avengers_shield_vanguard_ex);
    RegisterSpellScript(spell_pal_shield_of_the_righteous_vanguard_ex);
    RegisterSpellScript(spell_pal_beacon_of_the_savior_apply_ex);
    RegisterSpellScript(spell_pal_beacon_of_the_savior_transfer_ex);
    RegisterSpellScript(spell_pal_seraphic_barrier_ex);
    RegisterSpellScript(spell_pal_overflowing_light_ex);
}
