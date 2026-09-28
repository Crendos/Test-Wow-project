// ============================================================================
// Paladin 12.1.0 class fixes — часть 10: Кузнец света (Lightsmith), по логу WCL.
// Ядро дерева — Благословенные доспехи (1289728): чередование Святое оружие
// (бафф 432502) / Святой оплот (бафф 432496 + абсорб 432607); Солидарность
// (432802) зеркалит доспехи на союзника. Благословение горна (бафф 434132):
// стаки от трат СС; на капе — бесплатный доспех. Пока висит Бг:
//   ЩП -> Возмездие горна (447258) по цели; Слово света -> Святое слово (447246).
// В логе: Св. оружие 17 кастов, бафф 54 приложений @59.8% (17 ручных + ~37 авто),
// Святой оплот 17 кастов (баф 27 @30.6% + стаки блоков 217 @19.3%).
// Вставка: конец spell_paladin.cpp (после части 9). Регистрация: AddSC_paladin_spell_scripts_ex10().
// Спутник: paladin_class_fixes_10.sql
// ============================================================================

// === CUT HERE ===============================================================

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <algorithm>

enum PaladinEx10Spells
{
    SPELL_EX10_HOLY_ARMAMENTS      = 1289728, // кнопка (чередует доспехи)
    SPELL_EX10_SACRED_WEAPON_TAL   = 432472,
    SPELL_EX10_SACRED_WEAPON_BUFF  = 432502,
    SPELL_EX10_HOLY_BULWARK_TAL    = 432459,
    SPELL_EX10_HOLY_BULWARK_BUFF   = 432496,
    SPELL_EX10_HOLY_BULWARK_ABSORB = 432607,
    SPELL_EX10_BOTF_TAL            = 433011,
    SPELL_EX10_BOTF_BUFF           = 434132,
    SPELL_EX10_FORGES_RECKONING    = 447258,
    SPELL_EX10_SACRED_WORD         = 447246,
    SPELL_EX10_SOLIDARITY_TAL      = 432802,
    SPELL_EX10_DG_TALENT           = 433106, // талант, dummy. E0 = SP*points/10, E2 = союзники, E3/E4 = % хила
    SPELL_EX10_DG_BUFF             = 460822, // стаки до 5, 30с. Не сам талант
    SPELL_EX10_DG_DAMAGE           = 433808, // урон, base 0 — сумму ставит скрипт
    SPELL_EX10_DG_HEAL             = 433807, // хил, base 0
    SPELL_EX10_HOLY_SPEC           = 137029,
    SPELL_EX10_CONSECRATION        = 26573
};

// 1289728 - Благословенные доспехи: чередование Оружие/Оплот (+абсорб), Солидарность.
class spell_pal_holy_armaments_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_SACRED_WEAPON_BUFF, SPELL_EX10_HOLY_BULWARK_BUFF, SPELL_EX10_HOLY_BULWARK_ABSORB });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        // Чередование: если Оружие висит — ставим Оплот, иначе Оружие
        bool useWeapon = !caster->HasAura(SPELL_EX10_SACRED_WEAPON_BUFF);
        uint32 armBuff = useWeapon ? SPELL_EX10_SACRED_WEAPON_BUFF : SPELL_EX10_HOLY_BULWARK_BUFF;

        auto apply = [&](Unit* who)
        {
            caster->CastSpell(who, armBuff, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
            if (armBuff == SPELL_EX10_HOLY_BULWARK_BUFF)
                caster->CastSpell(who, SPELL_EX10_HOLY_BULWARK_ABSORB, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell()
                });
        };

        Unit* target = GetExplTargetUnit() && GetExplTargetUnit()->IsFriendlyTo(caster) ? GetExplTargetUnit() : caster;
        apply(target);

        // Солидарность: зеркалим на второго (если качали на себя — на ближайшего союзника)
        if (caster->HasSpell(SPELL_EX10_SOLIDARITY_TAL))
        {
            if (target != caster)
                apply(caster);
            else
            {
                float const radius = 20.f;
                std::vector<Unit*> allies;
                Trinity::AnyFriendlyUnitInObjectRangeCheck check(caster, caster, radius);
                Trinity::UnitListSearcher searcher(caster, allies, check);
                Cell::VisitAllObjects(caster, searcher, radius);

                Unit* ally = nullptr;
                float bestDist = radius;
                for (Unit* u : allies)
                    if (u != caster && u->IsAlive() && !u->HasAura(armBuff))
                    {
                        float d = caster->GetDistance(u);
                        if (d < bestDist) { bestDist = d; ally = u; }
                    }
                if (ally)
                    apply(ally);
            }
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_holy_armaments_ex::HandleAfterCast);
    }
};

// 53600 - ЩП: с Благословением горна -> Возмездие горна 447258.
// Стаки Божественного наставления — отдельный скрипт на всех тратах СС.
class spell_pal_sotr_forge_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_BOTF_BUFF, SPELL_EX10_FORGES_RECKONING });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster)
            return;

        if (!target || !caster->HasAura(SPELL_EX10_BOTF_BUFF) || !caster->HasSpell(SPELL_EX10_BOTF_TAL))
            return;

        caster->CastSpell(target, SPELL_EX10_FORGES_RECKONING, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_sotr_forge_ex::HandleAfterCast);
    }
};

// 85673 - Слово света: с Бг -> Святое слово 447246.
class spell_pal_wog_sacred_word_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_BOTF_BUFF, SPELL_EX10_SACRED_WORD });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster)
            return;

        if (!target || !caster->HasAura(SPELL_EX10_BOTF_BUFF) || !caster->HasSpell(SPELL_EX10_BOTF_TAL))
            return;

        caster->CastSpell(target, SPELL_EX10_SACRED_WORD, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_wog_sacred_word_ex::HandleAfterCast);
    }
};

// 433106 - Божественное наставление: каждая способность Силы Света даёт стак 460822 (до 5).
// Следующее Освящение (26573) делит урон 433808 по врагам в 8 ярдах и лечит до N союзников в 20 ярдах.
// Формула описания: ($SP * $433106s1 / 10) * стаки. Универсальность и спец-модификатор
// (1+$137028s1/100 или 1+$137029s1/100) навешивает ядро на 433808/433807 — в базу их не кладём.
class spell_pal_divine_guidance_spend_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_DG_TALENT, SPELL_EX10_DG_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX10_DG_TALENT))
            return;

        caster->CastSpell(caster, SPELL_EX10_DG_BUFF, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_guidance_spend_ex::HandleAfterCast);
    }
};

class spell_pal_divine_guidance_cons_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_DG_TALENT, SPELL_EX10_DG_BUFF, SPELL_EX10_DG_DAMAGE, SPELL_EX10_DG_HEAL });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Spell const* spell = GetSpell();
        // Эхо Правосудия/Молота Света тоже кастует 26573. Это не кнопка Освящения.
        if (!caster || !spell || spell->IsTriggered())
            return;

        Aura* stacks = caster->GetAura(SPELL_EX10_DG_BUFF);
        SpellInfo const* talentInfo = sSpellMgr->GetSpellInfo(SPELL_EX10_DG_TALENT, DIFFICULTY_NONE);
        if (!stacks || !talentInfo || stacks->GetStackAmount() <= 0)
            return;

        int32 const stackCount = stacks->GetStackAmount();
        if (talentInfo->GetEffects().size() <= EFFECT_0)
            return;

        SpellEffectValue const coeff = talentInfo->GetEffect(EFFECT_0).CalcValue(caster);
        if (coeff <= 0.0)
            return;

        float const total = float(caster->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_HOLY))
            * float(coeff / 10.0) * float(stackCount);
        if (total <= 0.f)
            return;

        stacks->Remove();

        auto effectAmount = [&](SpellEffIndex index, int32 fallback)
        {
            if (talentInfo->GetEffects().size() <= index)
                return fallback;
            SpellEffectValue value = talentInfo->GetEffect(index).CalcValue(caster);
            return value > 0.0 ? int32(value) : fallback;
        };

        bool const holy = caster->HasAura(SPELL_EX10_HOLY_SPEC);
        int32 const healPct = holy ? effectAmount(EFFECT_4, 100) : effectAmount(EFFECT_3, 30);
        int32 allyCap = effectAmount(EFFECT_2, 3);
        if (allyCap <= 0)
            allyCap = 3;

        float const enemyRadius = 8.f; // 1228455, SpellRadius 14
        std::vector<Unit*> enemies;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck enemyCheck(caster, caster, enemyRadius);
        Trinity::UnitListSearcher enemySearcher(caster, enemies, enemyCheck);
        Cell::VisitAllObjects(caster, enemySearcher, enemyRadius);
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](Unit* enemy)
        {
            return !enemy || !enemy->IsAlive() || !caster->IsValidAttackTarget(enemy);
        }), enemies.end());

        if (enemies.empty())
            return;

        int32 const share = std::max(1, int32(total / float(enemies.size()) + 0.5f));
        for (Unit* enemy : enemies)
            caster->CastSpell(enemy, SPELL_EX10_DG_DAMAGE, MakeSpellArgs(
                TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                GetSpell(), SPELLVALUE_BASE_POINT0, share));

        int32 const heal = int32(total * float(healPct) / 100.f + 0.5f);
        if (heal <= 0)
            return;

        float const allyRadius = 20.f; // 1310389, SpellRadius 9
        std::vector<Unit*> allies;
        Trinity::AnyFriendlyUnitInObjectRangeCheck allyCheck(caster, caster, allyRadius);
        Trinity::UnitListSearcher allySearcher(caster, allies, allyCheck);
        Cell::VisitAllObjects(caster, allySearcher, allyRadius);
        allies.push_back(caster);
        allies.erase(std::remove_if(allies.begin(), allies.end(), [&](Unit* ally)
        {
            return !ally || !ally->IsAlive() || !caster->IsFriendlyTo(ally);
        }), allies.end());
        std::sort(allies.begin(), allies.end());
        allies.erase(std::unique(allies.begin(), allies.end()), allies.end());
        std::sort(allies.begin(), allies.end(), [&](Unit* a, Unit* b)
        {
            return caster->GetDistance(a) < caster->GetDistance(b);
        });
        if (int32(allies.size()) > allyCap)
            allies.resize(allyCap);

        for (Unit* ally : allies)
            caster->CastSpell(ally, SPELL_EX10_DG_HEAL, MakeSpellArgs(
                TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                GetSpell(), SPELLVALUE_BASE_POINT0, heal));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_guidance_cons_ex::HandleAfterCast);
    }
};

void AddSC_paladin_spell_scripts_ex11(); // часть 11 (регистрируется отсюда, лоадер не меняется)

void AddSC_paladin_spell_scripts_ex10()
{
    AddSC_paladin_spell_scripts_ex11();
    RegisterSpellScript(spell_pal_holy_armaments_ex);
    RegisterSpellScript(spell_pal_sotr_forge_ex);
    RegisterSpellScript(spell_pal_wog_sacred_word_ex);
    RegisterSpellScript(spell_pal_divine_guidance_spend_ex);
    RegisterSpellScript(spell_pal_divine_guidance_cons_ex);
}
