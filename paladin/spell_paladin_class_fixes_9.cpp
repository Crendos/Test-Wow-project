// ============================================================================
// Paladin 12.1.0 class fixes — часть 9: Темплар 12.x (дозакрытие после партии 6).
// Дерево героев переработано в 12.x; по логу WCL +18 (храмовник) и коду simc
// добавлены таланты, которых не было в партии 6:
//   * Сотрясение небес (431533): Молот Света даёт бафф 431536 — каждые 2с летит
//     летящий молоток (431398); ЩП обновляет бафф по пандемии (8с + 30%).
//   * Неоспоримое постановление (432626): Молот Света даёт бафф скорости 432629.
//   * Божественный молот (198034, пассивка 432929): ТДА (375576) даёт вращающийся
//     молот — урон 198137 каждые 2с ВЕДЁТ РОДНОЙ периодик спелла 198034 (аура 23,
//     период 2000мс — проверено по данным); retail 12.0: длительность 8с,
//     каждая потраченная ОС продлевает молот на +0.5с (см. spell_pal_sotr_shake_heavens_ex).
//   * Освящение Темплара (424616): Правосудие/Молот гнева дают стак 433671.
//   * Избавление Света (425518): летящие молотки (431398) дают стаки 433674;
//     на капе каст МС даёт бесплатный Молот Света (433732) и снимает стаки.
// В логе +18: Сотрясение 54 прока @52.5%, Постановление 54 @18.2%, Б.молот 24 @11.2%,
// Избавление 1827 стаков @96.7% — все счётчики сходятся с механикой.
// Вставка: конец spell_paladin.cpp (после части 8). Регистрация: AddSC_paladin_spell_scripts_ex9().
// Спутник: paladin_class_fixes_9.sql
// ============================================================================

// === CUT HERE ===============================================================

#include "CellImpl.h"
#include "EventProcessor.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"

enum PaladinEx9Spells
{
    SPELL_EX9_HOL_DRIVER          = 427453, // каст Молота Света (кнопка)
    SPELL_EX9_EMPIREAN_HAMMER     = 431398, // летящий молоток
    SPELL_EX9_SHAKE_HEAVENS_TAL   = 431533, // Сотрясение небес (талант)
    SPELL_EX9_SHAKE_HEAVENS_BUFF  = 431536, // бафф: тик 2с
    SPELL_EX9_UNDISPUTED_TAL      = 432626, // Неоспоримое постановление (талант)
    SPELL_EX9_UNDISPUTED_BUFF     = 432629, // бафф (скорость, значение из DBC E1)
    SPELL_EX9_DIVINE_HAMMER       = 198034, // Божественный молот (вращение)
    SPELL_EX9_DIVINE_HAMMER_TICK  = 198137, // урон вращения
    SPELL_EX9_LIGHTS_DELIVERANCE  = 425518, // Избавление Света (талант)
    SPELL_EX9_DELIVERANCE_STACK   = 433674, // стакающийся бафф
    SPELL_EX9_DELIVERANCE_FREE    = 433732, // след. Молот Света бесплатен
    SPELL_EX9_SANCTIFICATION_TAL  = 424616, // Освящение Темплара (талант)
    SPELL_EX9_SANCTIFICATION_BUFF = 433671, // стаки Освящения
    SPELL_EX9_DIVINE_TOLL         = 375576,
    SPELL_EX9_JUDGMENT_RET        = 20271,
    SPELL_EX9_JUDGMENT_PROT       = 275779,
    SPELL_EX9_JUDGMENT_HOLY       = 275773,
    SPELL_EX9_HOW                 = 24275,
    SPELL_EX9_HOW_AW              = 1241413,
    SPELL_EX9_SOTR                = 53600
};

// Тикер баффов Темплара: каждые 2с, пока висит бафф.
class pal_templar_tick_event : public BasicEvent
{
public:
    pal_templar_tick_event(Unit* caster, uint32 buffId, uint32 castId, uint32 maxTicks)
        : _caster(caster), _buffId(buffId), _castId(castId), _ticks(maxTicks) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        if (_ticks == 0 || !_caster->IsAlive() || !_caster->HasAura(_buffId))
            return true;

        if (_castId == SPELL_EX9_EMPIREAN_HAMMER)
        {
            // летящий молоток — в ближайшего врага (до 10 ярдов), иначе в землю под собой
            float const radius = 10.f;
            std::vector<Unit*> targets;
            Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(_caster, _caster, radius);
            Trinity::UnitListSearcher searcher(_caster, targets, check);
            Cell::VisitAllObjects(_caster, searcher, radius);

            Unit* target = nullptr;
            float bestDist = radius;
            for (Unit* enemy : targets)
                if (_caster->IsValidAttackTarget(enemy) && enemy->IsWithinLOSInMap(_caster))
                {
                    float dist = _caster->GetDistance(enemy);
                    if (dist < bestDist) { bestDist = dist; target = enemy; }
                }

            if (target)
                _caster->CastSpell(target, SPELL_EX9_EMPIREAN_HAMMER, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR
                });
        }
        else // 198137 — урон вращающегося молота вокруг паладина
            _caster->CastSpell(_caster, _castId, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

        --_ticks;
        _caster->m_Events.AddEventAtOffset(this, 2s);
        return false;
    }

private:
    Unit* _caster;
    uint32 _buffId;
    uint32 _castId;
    uint32 _ticks;
};

// 427453 - Молот Света (Темплар 12.x): выдача баффов и Избавление Света.
class spell_pal_hol_templar_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_UNDISPUTED_BUFF, SPELL_EX9_SHAKE_HEAVENS_BUFF, SPELL_EX9_DELIVERANCE_FREE });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        // Неоспоримое постановление: бафф скорости
        if (caster->HasSpell(SPELL_EX9_UNDISPUTED_TAL))
            caster->CastSpell(caster, SPELL_EX9_UNDISPUTED_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });

        // Сотрясение небес: бафф + молотки каждые 2с (бафф 8с => до 4 тиков, guard 5)
        if (caster->HasSpell(SPELL_EX9_SHAKE_HEAVENS_TAL))
        {
            caster->CastSpell(caster, SPELL_EX9_SHAKE_HEAVENS_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });
            caster->m_Events.AddEventAtOffset(new pal_templar_tick_event(caster, SPELL_EX9_SHAKE_HEAVENS_BUFF, SPELL_EX9_EMPIREAN_HAMMER, 5), 2s);
        }

        // Избавление Света: кап стаков => бесплатный Молот Света
        if (caster->HasSpell(SPELL_EX9_LIGHTS_DELIVERANCE))
            if (Aura* ld = caster->GetAura(SPELL_EX9_DELIVERANCE_STACK))
                if (uint32 cap = ld->GetSpellInfo()->StackAmount)
                    if (ld->GetStackAmount() >= int32(cap))
                    {
                        caster->CastSpell(caster, SPELL_EX9_DELIVERANCE_FREE, CastSpellExtraArgsInit{
                            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
                        });
                        ld->Remove();
                    }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_hol_templar_ex::HandleAfterCast);
    }
};

// 431398 - летящий молоток: +1 стак Избавления Света.
class spell_pal_empyrean_deliverance_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_DELIVERANCE_STACK });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasSpell(SPELL_EX9_LIGHTS_DELIVERANCE))
            return;

        caster->CastSpell(caster, SPELL_EX9_DELIVERANCE_STACK, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_empyrean_deliverance_ex::HandleAfterCast);
    }
};

// Спендеры Силы Света: продление Божественного молота (+0.5с за ОС, retail 12.0)
// и пандемия Сотрясения небес.
// simc: refresh в holy_power_consumer_t::execute (ОБЩИЙ для ЩП/ОП/Буря/СС):
// +8с (база), кап 1.3×базы = 10.4с. Лог Т-Рета: ШТ 96 аптайм-гейнов = 96 кастов МС
// при 296+348 спендерах — без капа бафф был бы вечным, поэтому кап обязателен.
class spell_pal_sotr_shake_heavens_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_SHAKE_HEAVENS_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        // Retail 12.0: "While active, each Holy Power spent increases the duration
        // of Divine Hammer by 0.5 seconds" — без капа, кулдаун молота = ТДА (1 мин)
        if (Aura* dh = caster->GetAura(SPELL_EX9_DIVINE_HAMMER))
            dh->SetDuration(dh->GetDuration() + 500);

        // Сотрясение небес: пандемия (только при таланте)
        if (!caster->HasSpell(SPELL_EX9_SHAKE_HEAVENS_TAL))
            return;

        if (Aura* sth = caster->GetAura(SPELL_EX9_SHAKE_HEAVENS_BUFF))
        {
            int32 base = sth->GetSpellInfo()->GetMaxDuration();
            if (base <= 0)
                base = 8000;
            // пандемия: +база, но не выше 1.3×базы (simc: 8с до 10.4с)
            int64 updated = int64(sth->GetDuration()) + base;
            int64 cap     = int64(base) * 13 / 10;
            sth->SetDuration(int32(updated < cap ? updated : cap));
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_sotr_shake_heavens_ex::HandleAfterCast);
    }
};

// 375576 - ТДА (Темплар): вращающийся Божественный молот (198034).
// Тик урона ВЕДЁТ родной периодик спелла 198034 (E0, аура 23, 2000мс -> 198137),
// поэтому наш тикер для молота НЕ ставится. Продление +0.5с/ОС — в
// spell_pal_sotr_shake_heavens_ex (привязан ко всем спендерам ОС).
class spell_pal_divine_toll_templar_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_DIVINE_HAMMER, SPELL_EX9_DIVINE_HAMMER_TICK });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasSpell(SPELL_EX9_DIVINE_HAMMER))
            return;

        // длительность 8с и тик 2с берутся из данных спелла 198034
        caster->CastSpell(caster, SPELL_EX9_DIVINE_HAMMER, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_divine_toll_templar_ex::HandleAfterCast);
    }
};

// Правосудие/Молот гнева: стаки Освящения Темплара (433671).
class spell_pal_sanctification_templar_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_SANCTIFICATION_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasSpell(SPELL_EX9_SANCTIFICATION_TAL))
            return;

        caster->CastSpell(caster, SPELL_EX9_SANCTIFICATION_BUFF, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
        });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_sanctification_templar_ex::HandleAfterCast);
    }
};

void AddSC_paladin_spell_scripts_ex9()
{
    RegisterSpellScript(spell_pal_hol_templar_ex);
    RegisterSpellScript(spell_pal_empyrean_deliverance_ex);
    RegisterSpellScript(spell_pal_sotr_shake_heavens_ex);
    RegisterSpellScript(spell_pal_divine_toll_templar_ex);
    RegisterSpellScript(spell_pal_sanctification_templar_ex);
}
