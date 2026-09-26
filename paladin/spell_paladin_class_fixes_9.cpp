// ============================================================================
// PAL_MECH_REV_20260926 — маркер для step1_scripts (без него партии считаются устаревшими).
// Paladin 12.1.0 class fixes — часть 9: Темплар 12.x (дозакрытие после партии 6).
// Дерево героев переработано в 12.x; по логу WCL +18 (храмовник) и коду simc
// добавлены таланты, которых не было в партии 6:
//   * Сотрясение небес (431533): только Молот Света даёт/обновляет бафф 431536
//     (пандемия рекастом). Спендеры СС его не продлевают. Тик 2с — родной периодик.
//   * Неоспоримое постановление (432626): Молот Света даёт бафф скорости 432629.
//   * Божественный молот (198034, пассивка 432929): ТДА (375576) даёт вращающийся
//     молот — урон 198137 каждые 2с ВЕДЁТ РОДНОЙ периодик спелла 198034 (аура 23,
//     период 2000мс — проверено по данным); длительность 8с из данных, без +0.5с за ОС.
//   * Освящение Темплара (424616): Правосудие/Молот гнева дают стак 433671.
//   * Избавление Света (425518): летящие молотки (431398) дают стаки 433674;
//     на капе, когда генератор и кнопка Молота недоступны — бесплатный Молот (433732).
// В логе +18: Сотрясение 54 прока @52.5%, Постановление 54 @18.2%, Б.молот 24 @11.2%,
// Избавление 1827 стаков @96.7% — все счётчики сходятся с механикой.
// Вставка: конец spell_paladin.cpp (после части 8). Регистрация: AddSC_paladin_spell_scripts_ex9().
// Спутник: paladin_class_fixes_9.sql
// ============================================================================

// === CUT HERE ===============================================================
// PAL_MECH_REV_20260926 — эта строка обязана быть НИЖЕ CUT HERE, иначе step1 её не вставит.

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
    SPELL_EX9_WAKE                = 255937,
    SPELL_EX9_HOL_READY           = 427441, // кнопка Молота Света
    SPELL_EX9_JUDGMENT_RET        = 20271,
    SPELL_EX9_JUDGMENT_PROT       = 275779,
    SPELL_EX9_JUDGMENT_HOLY       = 275773,
    SPELL_EX9_HOW                 = 24275,
    SPELL_EX9_HOW_AW              = 1241413,
    SPELL_EX9_SOTR                = 53600,
    SPELL_EX9_WOG                 = 85673,
    SPELL_EX9_TV                  = 85256,
    SPELL_EX9_FV                  = 383328,
    SPELL_EX9_DS                  = 53385,
    SPELL_EX9_HAMMERFALL          = 432463,
    SPELL_EX9_HIGHER_CALLING      = 431687,
    SPELL_EX9_CS                  = 35395,
    SPELL_EX9_BOJ                 = 184575,
    SPELL_EX9_TEMPLAR_STRIKE      = 407480,
    SPELL_EX9_TEMPLAR_SLASH       = 406647,
    SPELL_EX9_CRUSADING_STRIKE    = 408385,
    SPELL_EX9_BLESSED_HAMMER      = 204019,
    SPELL_EX9_HOTR                = 53595
};

namespace
{
    [[nodiscard]] bool ShakeHasNativePeriodic(uint32 spellId)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info)
            return false;
        for (SpellEffectInfo const& effect : info->GetEffects())
            if (effect.IsAura(SPELL_AURA_PERIODIC_TRIGGER_SPELL) && effect.TriggerSpell)
                return true;
        return false;
    }

    // wowhead 425518 (12.x): копить стаки с Эмпирейских молотов; съесть кап, только когда
    // генератор (ТДА у Прота / Пробуждение зол у Рета) И кнопка Молота недоступны.
    // Тогда выдаётся бесплатный Молот (433732) и сама кнопка (427441).
    void TryConsumeLightsDeliverance(Unit* caster, bool buttonJustExpired = false)
    {
        if (!caster || !caster->HasSpell(SPELL_EX9_LIGHTS_DELIVERANCE))
            return;
        // во время OnRemove аура ещё числится висящей — для спадания кнопки проверку пропускаем
        if (!buttonJustExpired && caster->HasAura(SPELL_EX9_HOL_READY))
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        uint32 generator = player->GetPrimarySpecialization() == ChrSpecialization::PaladinProtection
            ? SPELL_EX9_DIVINE_TOLL : SPELL_EX9_WAKE;
        SpellInfo const* genInfo = sSpellMgr->GetSpellInfo(generator, DIFFICULTY_NONE);
        if (!genInfo || player->GetSpellHistory()->IsReady(genInfo))
            return;

        Aura* ld = caster->GetAura(SPELL_EX9_DELIVERANCE_STACK);
        if (!ld)
            return;
        int32 cap = ld->GetSpellInfo()->StackAmount;
        if (cap <= 0)
            cap = 60; // текущий тултип: 60 у обеих спек
        if (ld->GetStackAmount() < cap)
            return;

        // снимаем стаки ДО каста: повторный вызов из того же тика (несколько эффектов 427441) уже не пройдёт кап
        ld->Remove();
        CastSpellExtraArgs args(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
        caster->CastSpell(caster, SPELL_EX9_DELIVERANCE_FREE, args);
        caster->CastSpell(caster, SPELL_EX9_HOL_READY, args);
    }

    void CastEmpyreanAt(Unit* caster, Unit* prefer, Spell const* triggering, int32 count)
    {
        if (!caster || count <= 0)
            return;
        Unit* dest = prefer && caster->IsValidAttackTarget(prefer) ? prefer : nullptr;
        if (!dest)
        {
            float const radius = 20.f;
            std::vector<Unit*> enemies;
            Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, radius);
            Trinity::UnitListSearcher searcher(caster, enemies, check);
            Cell::VisitAllObjects(caster, searcher, radius);
            float best = radius;
            for (Unit* enemy : enemies)
                if (caster->IsValidAttackTarget(enemy) && enemy->IsWithinLOSInMap(caster))
                {
                    float d = caster->GetDistance(enemy);
                    if (d < best) { best = d; dest = enemy; }
                }
        }
        if (!dest)
            return;
        for (int32 i = 0; i < count; ++i)
            caster->CastSpell(dest, SPELL_EX9_EMPIREAN_HAMMER, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = triggering
            });
    }
}

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

        // Сотрясение небес: бафф 431536. В данных у него уже есть периодик 2с
        // (wowhead: Periodically trigger spell). Свой тикер включаем ТОЛЬКО если
        // родного триггера нет — иначе молотки летят дважды.
        // Пандемия — повторным кастом баффа (флаг Periodic Refresh Extends Duration),
        // НЕ тратами Силы Света. simc refresh() живёт в hammer_of_light, не в общем спендере.
        if (caster->HasSpell(SPELL_EX9_SHAKE_HEAVENS_TAL))
        {
            bool wasUp = caster->HasAura(SPELL_EX9_SHAKE_HEAVENS_BUFF);
            caster->CastSpell(caster, SPELL_EX9_SHAKE_HEAVENS_BUFF, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR
            });
            if (!wasUp && !ShakeHasNativePeriodic(SPELL_EX9_SHAKE_HEAVENS_BUFF))
                caster->m_Events.AddEventAtOffset(new pal_templar_tick_event(caster, SPELL_EX9_SHAKE_HEAVENS_BUFF, SPELL_EX9_EMPIREAN_HAMMER, 5), 2s);
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
        TryConsumeLightsDeliverance(caster);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_empyrean_deliverance_ex::HandleAfterCast);
    }
};

// Спендеры Силы Света = Молотопад (432463), НЕ продление Сотрясения.
// wowhead: Рет — Окончательный приговор/Буря; Прот — Щит праведника/Слово света.
// Пока висит Сотрясение — ещё один молот. Сотрясение само обновляется только Молотом Света
// (лог Т-Рета: 96 гейнов ШТ = 96 кастов МС, не сотни спендеров).
// Божественный молот (432929, тултип 12.x): ровно 8с от ТДА, без +0.5с за ОС
// (этой строки в текущем тултипе нет; simc midnight её тоже не делает).
class spell_pal_sotr_shake_heavens_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_EMPIREAN_HAMMER, SPELL_EX9_HAMMERFALL });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        SpellInfo const* info = GetSpellInfo();
        if (!caster || !info || !caster->HasSpell(SPELL_EX9_HAMMERFALL))
            return;
        if (GetSpell()->IsTriggered())
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        bool ok = false;
        switch (player->GetPrimarySpecialization())
        {
            case ChrSpecialization::PaladinRetribution:
                ok = info->Id == SPELL_EX9_TV || info->Id == SPELL_EX9_FV || info->Id == SPELL_EX9_DS;
                break;
            case ChrSpecialization::PaladinProtection:
                ok = info->Id == SPELL_EX9_SOTR || info->Id == SPELL_EX9_WOG;
                break;
            default:
                break;
        }
        if (!ok)
            return;

        int32 count = caster->HasAura(SPELL_EX9_SHAKE_HEAVENS_BUFF) ? 2 : 1;
        CastEmpyreanAt(caster, GetExplTargetUnit(), GetSpell(), count);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_sotr_shake_heavens_ex::HandleAfterCast);
    }
};

// 375576 - ТДА (Темплар): вращающийся Божественный молот (198034).
// Тик урона ВЕДЁТ родной периодик спелла 198034 (E0, аура 23, 2000мс -> 198137),
// поэтому наш тикер для молота НЕ ставится. Длительность — 8с из данных, без продления.
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

// 431687 Высшее призвание: пока висит Сотрясение, билдеры продлевают его.
// wowhead: Рет — Удар крестоносца / Молот гнева / Клинок; Прот — УК / Мг / Правосудие.
// Удар крестоносца у Прота заменён Благословенным молотом / Молотом праведника — их тоже считаем.
// Крещендо ударов (408385) = замена автоатаки, simc даёт только 500мс.
class spell_pal_higher_calling_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_SHAKE_HEAVENS_BUFF, SPELL_EX9_HIGHER_CALLING });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        SpellInfo const* info = GetSpellInfo();
        if (!caster || !info || !caster->HasSpell(SPELL_EX9_HIGHER_CALLING))
            return;
        Aura* sth = caster->GetAura(SPELL_EX9_SHAKE_HEAVENS_BUFF);
        if (!sth)
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        uint32 id = info->Id;
        bool ok = false;
        switch (player->GetPrimarySpecialization())
        {
            case ChrSpecialization::PaladinRetribution:
                ok = id == SPELL_EX9_CS || id == SPELL_EX9_HOW || id == SPELL_EX9_HOW_AW
                    || id == SPELL_EX9_BOJ || id == SPELL_EX9_TEMPLAR_STRIKE || id == SPELL_EX9_TEMPLAR_SLASH
                    || id == SPELL_EX9_CRUSADING_STRIKE;
                break;
            case ChrSpecialization::PaladinProtection:
                ok = id == SPELL_EX9_CS || id == SPELL_EX9_HOW || id == SPELL_EX9_HOW_AW
                    || id == SPELL_EX9_JUDGMENT_RET || id == SPELL_EX9_JUDGMENT_PROT || id == SPELL_EX9_JUDGMENT_HOLY
                    || id == SPELL_EX9_BLESSED_HAMMER || id == SPELL_EX9_HOTR || id == SPELL_EX9_CRUSADING_STRIKE;
                break;
            default:
                break;
        }
        if (!ok)
            return;

        int32 ext = id == SPELL_EX9_CRUSADING_STRIKE ? 500 : 1000;
        if (id != SPELL_EX9_CRUSADING_STRIKE)
            if (AuraEffect const* eff = caster->GetAuraEffect(SPELL_EX9_HIGHER_CALLING, EFFECT_0))
            {
                int32 amt = eff->GetAmount();
                if (amt > 0 && amt <= 10)
                    ext = amt * 1000;
                else if (amt > 10 && amt <= 5000)
                    ext = amt;
            }

        int32 dur = sth->GetDuration() + ext;
        if (dur > sth->GetMaxDuration())
            sth->SetMaxDuration(dur);
        sth->SetDuration(dur);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_higher_calling_ex::HandleAfterCast);
    }
};

class pal_deliverance_check_event : public BasicEvent
{
public:
    explicit pal_deliverance_check_event(Unit* caster) : _caster(caster) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        TryConsumeLightsDeliverance(_caster, true);
        return true;
    }

private:
    Unit* _caster;
};

// Кнопка Молота (427441) спала — если стаки Избавления уже на капе и генератор на КД,
// выдать бесплатный Молот (simc expire-callback hammer_of_light_ready).
class spell_pal_hol_ready_expire_ex : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX9_DELIVERANCE_FREE, SPELL_EX9_HOL_READY });
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;
        Unit* caster = GetTarget();
        if (!caster)
            return;
        // откладываем на 1мс: каст 427441 изнутри его же OnRemove съедается снятием
        caster->m_Events.AddEventAtOffset(new pal_deliverance_check_event(caster), 1ms);
    }

    void Register() override
    {
        // тип ауры кнопки между билдами плавает (оверрайд панели, не dummy) — ловим любой эффект
        AfterEffectRemove += AuraEffectRemoveFn(spell_pal_hol_ready_expire_ex::OnRemove, EFFECT_ALL, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_paladin_spell_scripts_ex9()
{
    RegisterSpellScript(spell_pal_hol_templar_ex);
    RegisterSpellScript(spell_pal_empyrean_deliverance_ex);
    RegisterSpellScript(spell_pal_sotr_shake_heavens_ex);
    RegisterSpellScript(spell_pal_divine_toll_templar_ex);
    RegisterSpellScript(spell_pal_sanctification_templar_ex);
    RegisterSpellScript(spell_pal_higher_calling_ex);
    RegisterSpellScript(spell_pal_hol_ready_expire_ex);
}
