// ============================================================================
// Paladin 12.1.0 class fixes — часть 3: Защита (остаток) + Молот гнева в АН.
// Вставка: конец spell_paladin.cpp (после частей 1 и 2).
// Регистрация: AddSC_paladin_spell_scripts_ex3(). Спутник: paladin_class_fixes_3.sql
// ============================================================================

// === CUT HERE ===============================================================

#include "AreaTriggerAI.h"
#include "AreaTriggerDataStore.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

enum PaladinEx3Spells
{
    SPELL_EX3_AVENGERS_SHIELD           = 31935,
    SPELL_EX3_SOTR_ARMOR                = 132403,
    SPELL_EX3_SOTR                      = 53600,
    SPELL_EX3_WORD_OF_GLORY             = 85673,
    SPELL_EX3_GOAK                      = 86659,
    SPELL_EX3_CONSECRATION_TICK         = 81297,

    // таланты/баффы
    SPELL_EX3_FOCUSED_ENMITY            = 378845,
    SPELL_EX3_FERREN_FERVOR             = 378762,
    SPELL_EX3_BORF_TALENT               = 386653,
    SPELL_EX3_BORF_BUFF                 = 386652, // стаки +10% урона СоП, кап 5
    SPELL_EX3_STRENGTH_ADVERSITY        = 393071,
    SPELL_EX3_STRENGTH_ADVERSITY_BUFF   = 393038, // +3% парирования, 15с, кап 5
    SPELL_EX3_VALKYR_GIFT               = 378279,
    SPELL_EX3_REDOUT                    = 280373,
    SPELL_EX3_INNER_LIGHT               = 386568,
    SPELL_EX3_INNER_LIGHT_BUFF          = 386556,
    SPELL_EX3_ZEALOTS_PARAGON           = 391142,
    SPELL_EX3_VALIANT_CRUSADE           = 1245979,
    SPELL_EX3_SEARING_SUNLIGHT          = 1244070,

    // Свет титанов/прочее из ч.2 (переиспользуем id)
    SPELL_EX3_SENTINEL                  = 389539,
    SPELL_EX3_WOG_SELF                  = 315921, // Прот: Слово света на себя (+до 300%)
    SPELL_EX3_WOG_ALLY                  = 315924, // Прот: Слово света союзнику (+до 100%)

    // Молот гнева в АН
    SPELL_EX3_HOW_AW_TALENT             = 1241288,
    SPELL_EX3_HOW_OVERRIDE_RET          = 1241410, // 20271/275773 -> 24275
    SPELL_EX3_HOW_OVERRIDE_PROT         = 1277026, // 275779 -> 1241413
    SPELL_EX3_JUDGMENT_RET              = 20271,
    SPELL_EX3_JUDGMENT_PROT             = 275779,
    SPELL_EX3_JUDGMENT_HOLY             = 275773,
    SPELL_EX3_HOW                       = 24275,
    SPELL_EX3_HOW_AW                    = 1241413,
    SPELL_EX3_AW                        = 31884,
    SPELL_EX3_CRUSADE_AURA              = 231895,
    SPELL_EX3_AW_8S                     = 454351,
    SPELL_EX3_BLESSED_HAMMER            = 204019,
    SPELL_EX3_BLESSED_HAMMER_DMG        = 204301,
    SPELL_EX3_BLESSED_HAMMER_AT         = 6006    // AreaTriggerCreatePropertiesId (Effect#1 каста 204019)
};

// --- Щит мстителя: концентратор всех талант-обработок -------------------------

// 31935 - Щит мстителя + талант-обвязка:
//  * 378845 Сосредоточенная враждебность: одна цель -> +100% урона (E0);
//  * 378762 Рвение Феррена Маркуса: главной цели +E0% (10/20);
//  * 386653 Оплот праведной ярости: за каждую цель стак 386652 (кап 5);
//  * 393071 Сила в невзгодах: за каждую цель стак 393038;
//  * 378279 Дар золотой валькирии: за каждую цель -1с КД Хранителя древних королей
//    (порог 30% HP -> 393108 триггерится ядром через ауру 468);
//  * 1244070 Палящий солнечный свет: каст Освящения мгновенно тикает урон.
class spell_pal_avengers_shield_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_BORF_BUFF, SPELL_EX3_STRENGTH_ADVERSITY_BUFF,
            SPELL_EX3_GOAK, SPELL_EX3_CONSECRATION_TICK });
    }

    void CalculateDamage(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*damage*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim)
            return;

        if (caster->HasAura(SPELL_EX3_FOCUSED_ENMITY) && GetUnitTargetCountForEffect(EFFECT_0) == 1)
            if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX3_FOCUSED_ENMITY, EFFECT_0))
                AddPct(pctMod, bonus->GetAmount());

        if (caster->HasAura(SPELL_EX3_FERREN_FERVOR) && victim == GetExplTargetUnit())
            if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX3_FERREN_FERVOR, EFFECT_0))
                AddPct(pctMod, bonus->GetAmount());
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (caster && caster->HasAura(SPELL_EX3_SEARING_SUNLIGHT))
            caster->CastSpell(caster, SPELL_EX3_CONSECRATION_TICK, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
    }

    void HandleHitTarget(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        // Оплот праведной ярости: стак за цель
        if (caster->HasAura(SPELL_EX3_BORF_TALENT))
            caster->CastSpell(caster, SPELL_EX3_BORF_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });

        // Сила в невзгодах: стак за цель
        if (caster->HasAura(SPELL_EX3_STRENGTH_ADVERSITY))
            caster->CastSpell(caster, SPELL_EX3_STRENGTH_ADVERSITY_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });

        // Дар золотой валькирии: -1с КД Хранителя за цель
        if (caster->HasAura(SPELL_EX3_VALKYR_GIFT))
            caster->GetSpellHistory()->ModifyCooldown(SPELL_EX3_GOAK, Seconds(-1));
    }

    void Register() override
    {
        CalcDamage += SpellCalcDamageFn(spell_pal_avengers_shield_ex::CalculateDamage);
        AfterCast += SpellCastFn(spell_pal_avengers_shield_ex::HandleAfterCast);
        OnEffectHitTarget += SpellEffectFn(spell_pal_avengers_shield_ex::HandleHitTarget, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// 53600 - Щит праведника: потребление стаков Оплота праведной ярости.
class spell_pal_shield_of_the_righteous_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_BORF_BUFF });
    }

    void HandleAfterCast()
    {
        if (Unit* caster = GetCaster())
            if (caster->HasAura(SPELL_EX3_BORF_TALENT))
                caster->RemoveAura(SPELL_EX3_BORF_BUFF);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_shield_of_the_righteous_ex::HandleAfterCast);
    }
};

// 85673 - Слово света (ветки Прота):
//  * 315921: на себя — +до 300% лечения по недостающему здоровью;
//  * 315924: союзнику — +до 100% по недостающему здоровью цели;
//  * 389539 Страж: каждая трата СС откладывает распад стаков (v1: +1с длит.).
class spell_pal_word_of_glory_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_WOG_SELF, SPELL_EX3_WOG_ALLY, SPELL_EX3_SENTINEL });
    }

    void CalculateHealing(SpellEffectInfo const& /*effectInfo*/, Unit const* victim, int32& /*healing*/, int32& /*flatMod*/, float& pctMod) const
    {
        Unit* caster = GetCaster();
        if (!caster || !victim)
            return;

        if (victim == caster)
        {
            if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX3_WOG_SELF, EFFECT_0))
            {
                float missingFrac = 1.f - caster->GetHealthPct() / 100.f;
                AddPct(pctMod, bonus->GetAmount() * missingFrac);
            }
        }
        else if (AuraEffect const* bonus = caster->GetAuraEffect(SPELL_EX3_WOG_ALLY, EFFECT_0))
        {
            float missingFrac = 1.f - victim->GetHealthPct() / 100.f;
            AddPct(pctMod, bonus->GetAmount() * missingFrac);
        }
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        // Страж: откладываем распад (упрощение: +1с длительности ауры)
        if (Aura* sentinel = caster->GetAura(SPELL_EX3_SENTINEL))
        {
            sentinel->SetDuration(sentinel->GetDuration() + 1000);
            sentinel->SetMaxDuration(sentinel->GetMaxDuration() + 1000);
        }
    }

    void Register() override
    {
        CalcHealing += SpellCalcHealingFn(spell_pal_word_of_glory_ex::CalculateHealing);
        AfterCast += SpellCastFn(spell_pal_word_of_glory_ex::HandleAfterCast);
    }
};

// 132403 - аура Щита праведника:
//  * 280373 Крепость: броня растёт до +60% по недостающему здоровью;
//  * 386568 Внутренний свет: по естественному истечению — 386556.
class spell_pal_sotr_aura_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellEffect({ { SPELL_EX3_SOTR_ARMOR, EFFECT_1 } });
    }

    void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        if (!target->HasAura(SPELL_EX3_REDOUT))
            return;

        float missingFrac = 1.f - target->GetHealthPct() / 100.f;
        if (missingFrac <= 0.f)
            return;

        float newAmount = aurEff->GetAmount() * (1.f + 0.6f * missingFrac);
        const_cast<AuraEffect*>(aurEff)->ChangeAmount(newAmount);
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;
        if (!target->HasAura(SPELL_EX3_INNER_LIGHT))
            return;
        if (!sSpellMgr->GetSpellInfo(SPELL_EX3_INNER_LIGHT_BUFF, DIFFICULTY_NONE))
            return;

        target->CastSpell(target, SPELL_EX3_INNER_LIGHT_BUFF, TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_pal_sotr_aura_ex::OnApply, EFFECT_1, SPELL_AURA_MOD_ARMOR_PCT_FROM_STAT, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_sotr_aura_ex::OnRemove, EFFECT_1, SPELL_AURA_MOD_ARMOR_PCT_FROM_STAT, AURA_EFFECT_HANDLE_REAL);
    }
};

// 391142 - Рвение фанатика: Молот гнева и Правосудие продлевают
// АН/Крестовый поход/Стража на E0 мс (500 мс за ранг).
class spell_pal_zealots_paragon_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_ZEALOTS_PARAGON });
    }

    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        AuraEffect const* paragon = caster ? caster->GetAuraEffect(SPELL_EX3_ZEALOTS_PARAGON, EFFECT_0) : nullptr;
        if (!paragon)
            return;

        Milliseconds extend(int32(paragon->GetAmount()));
        for (uint32 auraId : { SPELL_EX3_AW, SPELL_EX3_CRUSADE_AURA, SPELL_EX3_SENTINEL })
            if (Aura* aura = caster->GetAura(auraId))
            {
                aura->SetDuration(aura->GetDuration() + extend.count());
                aura->SetMaxDuration(aura->GetMaxDuration() + extend.count());
            }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_pal_zealots_paragon_ex::HandleHitTarget);
    }
};

// 190784 - Священный скакун + 1245979 Доблестный крестовый поход:
// езда даёт Щит праведника (упрощение: 132403 на 8с = поездка + 4с).
class spell_pal_valiant_crusade_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_VALIANT_CRUSADE, SPELL_EX3_SOTR_ARMOR });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX3_VALIANT_CRUSADE))
            return;

        caster->CastSpell(caster, SPELL_EX3_SOTR_ARMOR, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_DURATION, 8000));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_valiant_crusade_ex::HandleAfterCast);
    }
};

// 204019 - Благословенный молот.
// Розничная схема: Effect#1 каста = Create Area Trigger (6006) — ядро само спавнит AT,
// AI-скрипт at_pal_blessed_hammer вешает 204301 (урон + дебафф) на врагов, через которых
// проходит спираль. Визуал молота клиент рисует из SpellVisual самого каста.
// Fallback: если в world DB нет строки create_properties 6006 (не импортирован _8.sql) —
// старое поведение: мгновенный AoE-урон вокруг.
class spell_pal_blessed_hammer_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_BLESSED_HAMMER_DMG });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (sAreaTriggerDataStore->GetAreaTriggerCreateProperties(AreaTriggerCreatePropertiesId{ SPELL_EX3_BLESSED_HAMMER_AT, false }))
            return; // AT 6006 заспавнится эффектом заклинания — спираль работает

        float const radius = 8.f;
        std::vector<Unit*> targets;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, radius);
        Trinity::UnitListSearcher searcher(caster, targets, check);
        Cell::VisitAllObjects(caster, searcher, radius);

        for (Unit* target : targets)
            if (target->IsWithinLOSInMap(caster))
                caster->CastSpell(target, SPELL_EX3_BLESSED_HAMMER_DMG, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell()
                });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_blessed_hammer_ex::HandleAfterCast);
    }
};

// Спираль Благословенного молота: урон при прохождении над врагом (каждый — 1 раз).
struct at_pal_blessed_hammer : public AreaTriggerAI
{
    using AreaTriggerAI::AreaTriggerAI;

    void OnCreate(Spell const* /*creatingSpell*/) override
    {
        // Сплайн рассчитан на ~2.5 с; живём столько же, а не все 5 с заклинания
        at->SetDuration(2500);
    }

    void OnUnitEnter(Unit* unit) override
    {
        if (!unit || !unit->IsAlive())
            return;

        if (_hit.find(unit->GetGUID()) != _hit.end())
            return;

        Unit* caster = at->GetCaster();
        if (!caster || !caster->IsValidAttackTarget(unit))
            return;

        _hit.insert(unit->GetGUID());
        caster->CastSpell(unit, SPELL_EX3_BLESSED_HAMMER_DMG, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR
        });
    }

private:
    std::unordered_set<ObjectGuid> _hit;
};

// 1241288 - Молот гнева: в АН Правосудие превращается в Молот гнева
// (надеваем DBC-оверрайды 1241410 (Рет) / 1277026 (Прот), снимаем на выходе).
class spell_pal_hammer_of_wrath_aw_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX3_HOW_AW_TALENT, SPELL_EX3_HOW_OVERRIDE_RET, SPELL_EX3_HOW_OVERRIDE_PROT });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player || !player->HasAura(SPELL_EX3_HOW_AW_TALENT))
            return;

        if (player->HasSpell(SPELL_EX3_JUDGMENT_RET) || player->HasSpell(SPELL_EX3_JUDGMENT_HOLY))
            player->CastSpell(player, SPELL_EX3_HOW_OVERRIDE_RET, TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
        if (player->HasSpell(SPELL_EX3_JUDGMENT_PROT))
            player->CastSpell(player, SPELL_EX3_HOW_OVERRIDE_PROT, TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        player->RemoveAura(SPELL_EX3_HOW_OVERRIDE_RET);
        player->RemoveAura(SPELL_EX3_HOW_OVERRIDE_PROT);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_pal_hammer_of_wrath_aw_ex::OnApply, EFFECT_0, SPELL_AURA_ADD_PCT_MODIFIER, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_hammer_of_wrath_aw_ex::OnRemove, EFFECT_0, SPELL_AURA_ADD_PCT_MODIFIER, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_paladin_spell_scripts_ex3()
{
    RegisterSpellScript(spell_pal_avengers_shield_ex);
    RegisterSpellScript(spell_pal_shield_of_the_righteous_ex);
    RegisterSpellScript(spell_pal_word_of_glory_ex);
    RegisterSpellScript(spell_pal_sotr_aura_ex);
    RegisterSpellScript(spell_pal_zealots_paragon_ex);
    RegisterSpellScript(spell_pal_valiant_crusade_ex);
    RegisterSpellScript(spell_pal_blessed_hammer_ex);
    RegisterSpellScript(spell_pal_hammer_of_wrath_aw_ex);
}
