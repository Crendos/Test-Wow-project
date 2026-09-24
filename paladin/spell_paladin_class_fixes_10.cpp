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
    SPELL_EX10_DIVINE_GUIDANCE     = 433106  // стаки (эффект — DBC)
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

// 53600 - ЩП: с Благословением горна -> Возмездие горна 447258; стаки Бж. наставления.
class spell_pal_sotr_forge_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_BOTF_BUFF, SPELL_EX10_FORGES_RECKONING, SPELL_EX10_DIVINE_GUIDANCE });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster)
            return;

        if (caster->HasSpell(SPELL_EX10_DIVINE_GUIDANCE))
            caster->CastSpell(caster, SPELL_EX10_DIVINE_GUIDANCE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

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

// 85673 - Слово света: с Бг -> Святое слово 447246; стаки Бж. наставления.
class spell_pal_wog_sacred_word_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX10_BOTF_BUFF, SPELL_EX10_SACRED_WORD, SPELL_EX10_DIVINE_GUIDANCE });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (!caster)
            return;

        if (caster->HasSpell(SPELL_EX10_DIVINE_GUIDANCE))
            caster->CastSpell(caster, SPELL_EX10_DIVINE_GUIDANCE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

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

void AddSC_paladin_spell_scripts_ex10()
{
    RegisterSpellScript(spell_pal_holy_armaments_ex);
    RegisterSpellScript(spell_pal_sotr_forge_ex);
    RegisterSpellScript(spell_pal_wog_sacred_word_ex);
}
