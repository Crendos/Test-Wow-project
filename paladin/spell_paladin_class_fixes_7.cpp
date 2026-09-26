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

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"

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
    SPELL_EX7_HAMMER_OF_LIGHT_BUFF      = 427441, // кнопка «Молот Света» (20с)

    // АУДИТ26.09: эхо-эффекты Неоспоримого постановления (432626, wowhead):
    // баф432629 в DBC = ТОЛЬКО хаст12%/6с — «Рет: Правосудие по целям;
    // Прот: Удар в праведности + Консекрат под целью» = серверная часть.
    SPELL_EX7_UNDISPUTED_TAL            = 432626,
    SPELL_EX7_SOTR                      = 53600,
    SPELL_EX7_SOTR_BUFF                 = 132403, // бафф брони Щита праведника, НЕ каст 53600
    SPELL_EX7_JUDGMENT_DEBUFF           = 197277, // дебафф Правосудия, НЕ полный каст
    SPELL_EX7_CONSECRATION              = 26573, // тот же id, что в части 4

    // Вестник солнца
    SPELL_EX7_DAWNLIGHT_TALENT          = 431377,
    SPELL_EX7_DAWNLIGHT_DOT             = 431380,
    SPELL_EX7_DAWNLIGHT_CHARGE          = 431522, // заряды «след. спендер вешает Рассвет»
    SPELL_EX7_WAKE                      = 255937,
    SPELL_EX7_DIVINE_TOLL               = 375576,
    SPELL_EX7_HOLY_PRISM                = 114165,
    SPELL_EX7_TV                        = 85256,
    SPELL_EX7_FV                        = 383328,
    SPELL_EX7_DS                        = 53385,
    SPELL_EX7_LIGHT_OF_DAWN             = 85222,
    SPELL_EX7_SUN_SEAR_TALENT           = 431413,
    SPELL_EX7_SUN_SEAR_DOT              = 431414,
    SPELL_EX7_ENDLESS_GLEAM             = 1263787, // +длительность Рассвета от спендеров
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
    bool _undisputedEchoDone = false; // Прот: СоП+Освящение один раз на каст

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_LIGHTS_GUIDANCE, SPELL_EX7_HOL_DAMAGE, SPELL_EX7_EMPIREAN_HAMMER,
            SPELL_EX7_UNDISPUTED_TAL, SPELL_EX7_JUDGMENT_DEBUFF, SPELL_EX7_SOTR_BUFF, SPELL_EX7_CONSECRATION });
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

    // SpellHitHandler без привязки к эффект-индексу: у 427453 dummy-эффект не в EFFECT_0
    // (в логе ядра: "Effect EFFECT_0 ... did not match dbc effect data"), индекс меняется между билдами DBC.
    void HandleHitTarget()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX7_LIGHTS_GUIDANCE))
            return;

        // основной урон
        caster->CastSpell(GetHitUnit(), SPELL_EX7_HOL_DAMAGE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });

        // Эхо Неоспоримого постановления (432626, wowhead):
        // Рет — наложить ДЕБАФФ Правосудия (не полный каст 20271: тот даёт СС и чужие проки);
        // Прот — бафф брони Щита праведника (132403, не каст 53600 — тот тратит СС) + Освящение под целью.
        // Хаст-бафф 432629 даёт spell_pal_hol_templar_ex (часть 9).
        if (Player* p = caster->ToPlayer())
            if (p->HasSpell(SPELL_EX7_UNDISPUTED_TAL))
            {
                CastSpellExtraArgs echoArgs = CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
                    .TriggeringSpell = GetSpell()
                };
                switch (p->GetPrimarySpecialization())
                {
                    case ChrSpecialization::PaladinRetribution:
                        caster->CastSpell(GetHitUnit(), SPELL_EX7_JUDGMENT_DEBUFF, echoArgs);
                        break;
                    case ChrSpecialization::PaladinProtection:
                        if (!_undisputedEchoDone)
                        {
                            _undisputedEchoDone = true;
                            caster->CastSpell(caster, SPELL_EX7_SOTR_BUFF, echoArgs);
                            caster->CastSpell(GetHitUnit(), SPELL_EX7_CONSECRATION, echoArgs);
                        }
                        break;
                    default:
                        break;
                }
            }

        // молотки «Света наставления»: wowhead E2 = 3; если в ауре другое число — берём его
        int32 hammers = 3;
        if (AuraEffect const* hammerCount = caster->GetAuraEffect(SPELL_EX7_LIGHTS_GUIDANCE, EFFECT_1))
            if (hammerCount->GetAmount() > 0 && hammerCount->GetAmount() <= 10)
                hammers = hammerCount->GetAmount();
        CastEmpyreanHammers(hammers);

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
                caster->CastSpell(caster, SPELL_EX7_SACROSANCT_CRUSADE_HEAL, MakeSpellArgs(TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR, GetSpell(), SPELLVALUE_BASE_POINT0, int32(heal)));
            }
        }
    }

    void Register() override
    {
        // Молотопад с Молота Света снят: wowhead 432463 — только спендеры
        // (Приговор/Буря у Рета, Щит праведника/Слово света у Прота). См. часть 9.
        OnHit += SpellHitFn(spell_pal_hammer_of_light_ex::HandleHitTarget);
    }
};

//427445 «Свет наставления» (Храмовник, wowhead12.1): РЕТ — ПРОБУЖДЕНИЕ ЗОЛ
// (255937) заменяется на Молот Света на20с (прот-вариант: Звон, часть5/fix_6).
// После PROC_FIX (широкая маска427445 обнулена) выдача — только узкими путями.
class spell_pal_lights_guidance_wake_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_LIGHTS_GUIDANCE, SPELL_EX7_HAMMER_OF_LIGHT_BUFF });
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster || !caster->HasAura(SPELL_EX7_LIGHTS_GUIDANCE))
            return;
        if (Player* player = caster->ToPlayer())
            if (player->GetPrimarySpecialization() == ChrSpecialization::PaladinRetribution)
                caster->CastSpell(caster, SPELL_EX7_HAMMER_OF_LIGHT_BUFF, CastSpellExtraArgsInit{
                    .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR
                });
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_pal_lights_guidance_wake_ex::HandleAfterCast);
    }
};

// --- ВЕСТНИК СОЛНЦА ----------------------------------------------------------

// 431377 Рассветный свет (wowhead 12.x + simc midnight):
//   Рет: Пробуждение зол даёт N зарядов (431522, эффект 1 таланта, обычно 2).
//   Свет: Святая призма или Божественный звон (simc холи-выдачу не моделирует — по тултипу).
//   Спендер СС тратит 1 заряд и вешает DoT 431380 на первую цель без DoT
//   (если цель одна — обновляет уже висящий). Старые бинды 20271/20473 игнорируются.
// 1263787 Бесконечный отблеск: удар Приговора/Бури по уже висящему DoT +300мс;
//   Буря по 2+ таким целям — ещё +500мс. Холи: хил по союзнику на полном здоровье +500мс.
class spell_pal_dawnlight_ex : public SpellScript
{
    bool _chargeUsed = false;
    std::vector<ObjectGuid> _gleamDots;

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_DAWNLIGHT_TALENT, SPELL_EX7_DAWNLIGHT_DOT, SPELL_EX7_DAWNLIGHT_CHARGE });
    }

    static bool IsGrant(uint32 id)
    {
        return id == SPELL_EX7_WAKE || id == SPELL_EX7_DIVINE_TOLL || id == SPELL_EX7_HOLY_PRISM;
    }

    static bool IsSpender(uint32 id)
    {
        switch (id)
        {
            case SPELL_EX7_TV:
            case SPELL_EX7_FV:
            case SPELL_EX7_DS:
            case SPELL_EX7_WORD_OF_GLORY:
            case SPELL_EX7_SOTR:
            case SPELL_EX7_HOL_DRIVER:
            case SPELL_EX7_LIGHT_OF_DAWN:
                return true;
            default:
                return false;
        }
    }

    static int32 GleamMs(Unit* caster, SpellEffIndex idx, int32 fallback)
    {
        if (AuraEffect const* e = caster->GetAuraEffect(SPELL_EX7_ENDLESS_GLEAM, idx))
        {
            int32 amt = e->GetAmount();
            if (amt < 0)
                amt = -amt;
            if (amt >= 100 && amt <= 5000)
                return amt;
        }
        return fallback;
    }

    static void ExtendDot(Unit* target, ObjectGuid casterGuid, int32 ms)
    {
        if (!target || ms <= 0)
            return;
        if (Aura* dot = target->GetAura(SPELL_EX7_DAWNLIGHT_DOT, casterGuid))
        {
            int32 dur = dot->GetDuration() + ms;
            if (dur > dot->GetMaxDuration())
                dot->SetMaxDuration(dur);
            dot->SetDuration(dur);
        }
    }

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        SpellInfo const* info = GetSpellInfo();
        if (!caster || !info || !caster->HasSpell(SPELL_EX7_DAWNLIGHT_TALENT))
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        uint32 id = info->Id;
        auto spec = player->GetPrimarySpecialization();

        if (IsGrant(id))
        {
            bool grant = (spec == ChrSpecialization::PaladinRetribution && id == SPELL_EX7_WAKE)
                || (spec == ChrSpecialization::PaladinHoly && (id == SPELL_EX7_HOLY_PRISM || id == SPELL_EX7_DIVINE_TOLL));
            if (!grant)
                return;

            int32 charges = 2;
            if (AuraEffect const* e = caster->GetAuraEffect(SPELL_EX7_DAWNLIGHT_TALENT, EFFECT_0))
                if (e->GetAmount() > 0 && e->GetAmount() <= 10)
                    charges = e->GetAmount();

            caster->CastSpell(caster, SPELL_EX7_DAWNLIGHT_CHARGE, CastSpellExtraArgsInit{
                .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
                .TriggeringSpell = GetSpell()
            });
            if (Aura* aura = caster->GetAura(SPELL_EX7_DAWNLIGHT_CHARGE))
            {
                uint8 cap = aura->GetSpellInfo()->StackAmount;
                if (cap > 0 && charges > cap)
                    charges = cap;
                aura->SetStackAmount(uint8(charges));
            }
            return;
        }

        // AfterCast идёт после OnHit: бонус Бури по 2+ уже висевшим DoT
        if (id == SPELL_EX7_DS && _gleamDots.size() > 1 && caster->HasSpell(SPELL_EX7_ENDLESS_GLEAM))
        {
            int32 extra = GleamMs(caster, EFFECT_2, 500);
            for (ObjectGuid const& guid : _gleamDots)
                if (Unit* unit = ObjectAccessor::GetUnit(*caster, guid))
                    ExtendDot(unit, caster->GetGUID(), extra);
        }
    }

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        SpellInfo const* info = GetSpellInfo();
        if (!caster || !target || !info || !IsSpender(info->Id) || GetSpell()->IsTriggered())
            return;
        if (!caster->HasSpell(SPELL_EX7_DAWNLIGHT_TALENT))
            return;

        bool const hadDot = target->HasAura(SPELL_EX7_DAWNLIGHT_DOT, caster->GetGUID());
        if (hadDot && caster->HasSpell(SPELL_EX7_ENDLESS_GLEAM))
        {
            Player* player = caster->ToPlayer();
            if (player && player->GetPrimarySpecialization() == ChrSpecialization::PaladinRetribution
                && (info->Id == SPELL_EX7_TV || info->Id == SPELL_EX7_FV || info->Id == SPELL_EX7_DS))
            {
                ExtendDot(target, caster->GetGUID(), GleamMs(caster, EFFECT_1, 300));
                if (info->Id == SPELL_EX7_DS)
                    _gleamDots.push_back(target->GetGUID());
            }
            else if (player && player->GetPrimarySpecialization() == ChrSpecialization::PaladinHoly
                && target->IsFriendlyTo(caster) && target->GetHealth() >= target->GetMaxHealth()
                && (info->Id == SPELL_EX7_WORD_OF_GLORY || info->Id == SPELL_EX7_LIGHT_OF_DAWN))
            {
                ExtendDot(target, caster->GetGUID(), GleamMs(caster, EFFECT_0, 500));
            }
        }

        if (_chargeUsed)
            return;

        Aura* charges = caster->GetAura(SPELL_EX7_DAWNLIGHT_CHARGE);
        if (!charges || charges->GetStackAmount() <= 0)
            return;

        Unit* dest = target;
        if (hadDot)
        {
            bool single = GetSpell()->m_UniqueTargetInfo.size() <= 1;
            Unit* other = nullptr;
            if (!single)
            {
                float const radius = 12.f;
                std::vector<Unit*> nearby;
                if (target->IsFriendlyTo(caster))
                {
                    Trinity::AnyFriendlyUnitInObjectRangeCheck check(target, caster, radius);
                    Trinity::UnitListSearcher searcher(target, nearby, check);
                    Cell::VisitAllObjects(target, searcher, radius);
                }
                else
                {
                    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, radius);
                    Trinity::UnitListSearcher searcher(target, nearby, check);
                    Cell::VisitAllObjects(target, searcher, radius);
                }
                bool const friendly = target->IsFriendlyTo(caster);
                for (Unit* unit : nearby)
                    if (unit != target && unit->IsAlive()
                        && (friendly ? unit->IsFriendlyTo(caster) : caster->IsValidAttackTarget(unit))
                        && !unit->HasAura(SPELL_EX7_DAWNLIGHT_DOT, caster->GetGUID()))
                    {
                        other = unit;
                        break;
                    }
            }
            if (other)
                dest = other;
            else if (!single)
                return; // все цели уже с DoT — заряд не тратим (simc)
        }

        _chargeUsed = true;
        charges->ModStackAmount(-1);
        caster->CastSpell(dest, SPELL_EX7_DAWNLIGHT_DOT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_pal_dawnlight_ex::HandleHit);
        AfterCast += SpellCastFn(spell_pal_dawnlight_ex::HandleAfterCast);
    }
};

// 431413 Солнечный ожог: крит Молота гнева / Божественной бури (только Рет) вешает 431414.
// Холи-вариант (крит Шока/Зари) — отдельный хил, его id simc не моделирует; 431414 туда не кастуем.
class spell_pal_sun_sear_ex : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EX7_SUN_SEAR_TALENT, SPELL_EX7_SUN_SEAR_DOT });
    }

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        SpellInfo const* info = GetSpellInfo();
        if (!caster || !target || !info || !IsHitCrit() || !caster->HasSpell(SPELL_EX7_SUN_SEAR_TALENT))
            return;
        if (GetSpell()->IsTriggered())
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        if (player->GetPrimarySpecialization() != ChrSpecialization::PaladinRetribution)
            return;
        if (info->Id != SPELL_EX7_HAMMER_OF_WRATH && info->Id != SPELL_EX7_HAMMER_OF_WRATH_AW && info->Id != SPELL_EX7_DS)
            return;

        caster->CastSpell(target, SPELL_EX7_SUN_SEAR_DOT, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_pal_sun_sear_ex::HandleHit);
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
        if (!spellInfo || (spellInfo->Id != SPELL_EX7_HAMMER_OF_WRATH && spellInfo->Id != SPELL_EX7_HAMMER_OF_WRATH_AW))
            return false;

        // ре-касты от самого прока (triggered) не прокают повторно — ограничивает цепочку одним звеном
        if (Spell const* procSpell = eventInfo.GetProcSpell())
            if (procSpell->IsTriggered())
                return false;

        return true;
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
    RegisterSpellScript(spell_pal_lights_guidance_wake_ex);
    RegisterSpellScript(spell_pal_dawnlight_ex);
    RegisterSpellScript(spell_pal_sun_sear_ex);
    RegisterSpellScript(spell_pal_second_sunrise_ex);
    RegisterSpellScript(spell_pal_valiance_ex);
}
