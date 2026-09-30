/* PLAYERBOTS под TrinityCore master — v6: DummyLog (лог боя с манекеном)
 * Формат строки: T=<сек с начала> <ТИП> key=value ...
 * ТИПЫ: AURA (+/-/~ снапшот аур), CAST (каст бота), DMG (урон src=spell|melee|dot),
 *       HEAL, SUMMARY (итог при остановке).
 * Все вызовы — world-thread. Ауры поллятся, т.к. ScriptMgr-хуков аур/проков нет:
 * проки видны как DMG/CAST с чужим spellId и как APPLY его ауры.
 */
#include "DummyLog.h"

#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"     // sTraitNodeEntryStore/sTraitDefinitionStore — атрибуция находок по талантам
#include "DatabaseEnv.h"   // WorldDatabase — таблица playerbots_mechanics (слой 2, только QA)
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotMgr.h"  // sPlayerbotMgr.IsQAAccount — гейт слоя 2 (механики только для QA)
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

namespace
{
    constexpr uint32 POLL_INTERVAL_MS = 300;

    std::string TimeStamp(char const* fmt)
    {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::ostringstream os;
        os << std::put_time(&tmv, fmt);
        return os.str();
    }

    // ---- ScriptMgr-хук: урон/хил/касты --------------------------------------
    class playerbots_combat_log_script : public UnitScript
    {
    public:
        playerbots_combat_log_script() : UnitScript("playerbots_combat_log_script") { }

        void OnDamage(Unit* attacker, Unit* /*victim*/, uint32& damage) override
        {
            if (attacker)
                sPlayerbotDummyLog.HandleFinalDamage(attacker, damage);
        }

        void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
        {
            if (attacker && target && damage)
                sPlayerbotDummyLog.HandleMeleeDamage(attacker, target, damage);
        }

        void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
        {
            if (attacker && target && damage)
                sPlayerbotDummyLog.HandlePeriodicDamage(attacker, target, damage);
        }

        void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
        {
            if (attacker && target && damage > 0)
                sPlayerbotDummyLog.HandleSpellDamage(attacker, target, spellInfo ? spellInfo->Id : 0, damage);
        }

        void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
        {
            if (healer && reciever && gain)
                sPlayerbotDummyLog.HandleHeal(healer, reciever, gain);
        }
    };

    class playerbots_cast_log_script : public PlayerScript
    {
    public:
        playerbots_cast_log_script() : PlayerScript("playerbots_cast_log_script") { }

        void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
        {
            if (player && spell)
                sPlayerbotDummyLog.HandleCast(player, spell);
        }
    };
}

void AddSC_playerbots_dummylog()
{
    new playerbots_combat_log_script();
    new playerbots_cast_log_script();
}

// ---------------------------------------------------------------- core

/* static */ PlayerbotDummyLog& PlayerbotDummyLog::Instance()
{
    static PlayerbotDummyLog inst;
    return inst;
}

PlayerbotDummyLog::Record* PlayerbotDummyLog::Find(uint32 botCounter)
{
    auto it = m_records.find(botCounter);
    return it == m_records.end() ? nullptr : &it->second;
}

void PlayerbotDummyLog::PushEv(Record& rec, char ev, uint32 spell, int32 v)
{
    if (rec.timeline.size() >= 20000)   // защита от раздувания на длинных сессиях
        return;
    uint32 t = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - rec.start).count());
    rec.timeline.push_back({ t, ev, spell, v });
}

bool PlayerbotDummyLog::IsRecording(Player const* bot) const
{
    return bot && m_records.count(bot->GetGUID().GetCounter()) != 0;
}

/* static */ std::string PlayerbotDummyLog::SpellName(uint32 spellId)
{
    static std::unordered_map<uint32, std::string> cache;
    auto it = cache.find(spellId);
    if (it != cache.end())
        return it->second;

    std::string name;
    if (SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
        if (si->SpellName)
            name = (*si->SpellName)[sWorld->GetDefaultDbcLocale()];
    if (name.empty())
        name = "#" + std::to_string(spellId);

    cache.emplace(spellId, name);
    return name;
}

void PlayerbotDummyLog::Write(Record& rec, std::string const& line)
{
    if (!rec.file.is_open())
        return;
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - rec.start).count();
    rec.file << "T=" << std::fixed << std::setprecision(2) << t << "  " << line << '\n';
    rec.file.flush();   // читаем лог «вживую» во время боя
}

void PlayerbotDummyLog::StartRecording(Player* bot, Unit const* target)
{
    if (!bot || !target)
        return;
    if (IsRecording(bot))
        StopRecording(bot, "рестарт");

    std::string dir = sConfigMgr->GetStringDefault("Playerbots.DummyLogDir", "PlayerbotsLogs");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    Record rec;
    rec.path = dir + "/" + bot->GetName() + "_" + TimeStamp("%Y%m%d-%H%M%S") + ".log";
    rec.botName = bot->GetName();
    rec.file.open(rec.path, std::ios::out | std::ios::trunc);
    if (!rec.file.is_open())
    {
        TC_LOG_ERROR("playerbots", "DummyLog: не удалось открыть файл {}", rec.path);
        return;
    }

    rec.targetGuid  = target->GetGUID();
    rec.targetEntry = target->GetEntry();
    rec.targetName  = target->GetName();
    rec.start       = std::chrono::steady_clock::now();

    rec.file << "# playerbots dummy log v1\n";
    rec.file << "# bot=" << bot->GetName()
             << " guid=" << bot->GetGUID().ToString()
             << " account=" << bot->GetSession()->GetAccountId()
             << " level=" << uint32(bot->GetLevel())
             << " class=" << uint32(bot->GetClass())
             << " spec=" << uint32(bot->GetPrimarySpecialization()) << '\n';
    rec.file << "# target=" << target->GetName()
             << " guid=" << target->GetGUID().ToString()
             << " entry=" << target->GetEntry()
             << " map=" << target->GetMapId() << '\n';
    rec.file << "# started=" << TimeStamp("%Y-%m-%dT%H:%M:%S") << '\n';
    rec.file.flush();

    // бафы, уже висящие на боте до начала — тоже в лог
    PollAuras(rec, bot);

    m_records[bot->GetGUID().GetCounter()] = std::move(rec);
}

std::string PlayerbotDummyLog::StopRecording(Player* bot, std::string const& reason)
{
    if (!bot)
        return std::string();

    auto it = m_records.find(bot->GetGUID().GetCounter());
    if (it == m_records.end())
        return std::string();

    Record& rec = it->second;
    PollAuras(rec, bot);    // финальный снапшот (снятие аур при выходе из боя)

    float duration = std::chrono::duration<float>(std::chrono::steady_clock::now() - rec.start).count();
    std::ostringstream sum;
    sum << "SUMMARY duration_s=" << std::fixed << std::setprecision(1) << duration
        << " total_dealt=" << rec.totalDealt
        << " casts=" << rec.castCount
        << " cast_fails=" << rec.castFails
        << " reason=\"" << reason << "\"";
    Write(rec, sum.str());

    // QA-анализ сессии → отдельный файл <session>.qa.txt (рядом с логом).
    // Слой 2 (playerbots_mechanics) — только для роли QA: гейт по аккаунту бота.
    bool qaRole = bot->GetSession() && sPlayerbotMgr.IsQAAccount(bot->GetSession()->GetAccountId());
    std::string reportPath = WriteQaReport(rec, rec.path, reason, duration, qaRole, bot->GetClass());
    if (!reportPath.empty())
        Write(rec, "QA_REPORT file=\"" + reportPath + '"');

    std::string path = rec.path;
    rec.file.close();
    m_records.erase(it);

    if (!reportPath.empty())
        TC_LOG_INFO("playerbots", "DummyLog: лог {} и QA-отчёт {} закрыты ({})", path, reportPath, reason);
    else
        TC_LOG_INFO("playerbots", "DummyLog: лог {} закрыт ({})", path, reason);
    return path;
}

// ---------------------------------------------------------------- ауры (поллинг)

void PlayerbotDummyLog::PollAuras(Record& rec, Player* bot)
{
    Creature* target = (bot && bot->GetMap()) ? bot->GetMap()->GetCreature(rec.targetGuid) : nullptr;

    Unit* units[2] = { bot, target };
    std::unordered_map<uint32, AuraSnap>* snaps[2] = { &rec.botAuras, &rec.targetAuras };

    for (int i = 0; i < 2; ++i)
    {
        Unit* unit = units[i];
        if (!unit)
            continue;
        std::string const who = (i == 0) ? "self" : "target";
        auto& snap = *snaps[i];

        std::unordered_map<uint32, AuraSnap> cur;
        for (auto const& kv : unit->GetAppliedAuras())
        {
            AuraApplication* aa = kv.second;
            if (!aa || !aa->GetBase())
                continue;
            Aura* aura = aa->GetBase();
            AuraSnap s;
            s.spellId  = aura->GetId();
            s.caster   = aura->GetCasterGUID();
            s.stacks   = aura->GetStackAmount();
            s.positive = false;
            s.name     = SpellName(s.spellId);
            if (SpellInfo const* si = sSpellMgr->GetSpellInfo(s.spellId, DIFFICULTY_NONE))
                s.positive = si->IsPositive();
            cur.emplace(s.spellId, s);
        }

        // появившиеся / изменившиеся стаки
        for (auto const& kv : cur)
        {
            rec.auraSeen.insert(kv.first);      // слой 1: union всех когда-либо виденных аур
            auto it = snap.find(kv.first);
            if (it == snap.end())
            {
                PushEv(rec, 'A', kv.second.spellId, 0);   // слой 2: появление ауры в таймлайне
                int32 dur = -1;
                // длительность нужна только в строке APPLY
                for (auto const& a : unit->GetAppliedAuras())
                    if (a.second && a.second->GetBase() && a.second->GetBase()->GetId() == kv.first)
                    { dur = a.second->GetBase()->GetDuration(); break; }

                std::ostringstream os;
                os << "AURA + spell=" << kv.second.spellId
                   << " name=\"" << kv.second.name << '"'
                   << " who=" << who
                   << " " << (kv.second.positive ? "buff" : "debuff")
                   << " stacks=" << uint32(kv.second.stacks)
                   << " dur_ms=" << dur
                   << " caster=" << ((kv.second.caster == bot->GetGUID()) ? std::string("self") : kv.second.caster.ToString());
                Write(rec, os.str());
            }
            else if (it->second.stacks != kv.second.stacks)
            {
                std::ostringstream os;
                os << "AURA ~ spell=" << kv.second.spellId
                   << " name=\"" << kv.second.name << '"'
                   << " who=" << who
                   << " stacks=" << uint32(kv.second.stacks);
                Write(rec, os.str());
            }
        }

        // снятые
        for (auto const& kv : snap)
        {
            if (cur.count(kv.first))
                continue;
            std::ostringstream os;
            os << "AURA - spell=" << kv.second.spellId
               << " name=\"" << kv.second.name << '"'
               << " who=" << who
               << " " << (kv.second.positive ? "buff" : "debuff");
            Write(rec, os.str());
        }

        snap.swap(cur);
    }
}

void PlayerbotDummyLog::Update(uint32 diff)
{
    if (m_records.empty())
        return;
    m_globalPollMs += diff;
    if (m_globalPollMs < POLL_INTERVAL_MS)
        return;
    m_globalPollMs = 0;

    for (auto it = m_records.begin(); it != m_records.end();)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(it->first));
        if (!bot)
        {
            it->second.file.close();
            it = m_records.erase(it);
            continue;
        }
        PollAuras(it->second, bot);
        ++it;
    }
}

// ---------------------------------------------------------------- обработчики хуков

void PlayerbotDummyLog::HandleFinalDamage(Unit const* attacker, uint32 amount)
{
    if (!attacker)
        return;
    if (Record* rec = Find(attacker->GetGUID().GetCounter()))
        rec->totalDealt += amount;
}

void PlayerbotDummyLog::HandleSpellDamage(Unit const* attacker, Unit const* victim, uint32 spellId, int32 amount)
{
    if (!attacker || !victim || amount <= 0)
        return;
    if (Record* rec = Find(attacker->GetGUID().GetCounter()))
    {
        rec->damaged.insert(spellId);
        PushEv(*rec, 'D', spellId, amount);
        std::ostringstream os;
        os << "DMG src=spell spell=" << spellId
           << " name=\"" << SpellName(spellId) << '"'
           << " amount=" << amount
           << " victim=\"" << victim->GetName() << '"';
        Write(*rec, os.str());
    }
}

void PlayerbotDummyLog::HandleMeleeDamage(Unit const* attacker, Unit const* victim, uint32 amount)
{
    if (!attacker || !victim || !amount)
        return;
    if (Record* rec = Find(attacker->GetGUID().GetCounter()))
    {
        std::ostringstream os;
        os << "DMG src=melee amount=" << amount
           << " victim=\"" << victim->GetName() << '"';
        Write(*rec, os.str());
    }
}

void PlayerbotDummyLog::HandlePeriodicDamage(Unit const* attacker, Unit const* victim, uint32 amount)
{
    if (!attacker || !victim || !amount)
        return;
    // ScriptMgr-хук периодики не несёт spellId — корреляция через строки AURA той же ауры
    if (Record* rec = Find(attacker->GetGUID().GetCounter()))
    {
        std::ostringstream os;
        os << "DMG src=dot amount=" << amount
           << " victim=\"" << victim->GetName() << '"';
        Write(*rec, os.str());
    }
}

void PlayerbotDummyLog::HandleHeal(Unit const* healer, Unit const* victim, uint32 amount)
{
    if (!healer || !victim || !amount)
        return;
    if (Record* rec = Find(healer->GetGUID().GetCounter()))
    {
        std::ostringstream os;
        os << "HEAL amount=" << amount
           << " victim=\"" << victim->GetName() << '"';
        Write(*rec, os.str());
    }
}

void PlayerbotDummyLog::HandleCast(Player const* caster, Spell const* spell)
{
    if (!caster || !spell)
        return;
    if (Record* rec = Find(caster->GetGUID().GetCounter()))
    {
        SpellInfo const* si = spell->GetSpellInfo();
        uint32 spellId = si ? si->Id : 0;
        Unit* target = spell->m_targets.GetUnitTarget();

        ++rec->castCount;
        ++rec->casted[spellId];
        PushEv(*rec, 'C', spellId, 0);
        std::ostringstream os;
        os << "CAST spell=" << spellId
           << " name=\"" << SpellName(spellId) << '"';
        if (target)
            os << " target=\"" << target->GetName() << '"';
        Write(*rec, os.str());
    }
}

/* static */ std::string PlayerbotDummyLog::CastResultName(int32 result)
{
    switch (result)
    {
        case SPELL_CAST_OK:                  return "OK";
        case SPELL_FAILED_BAD_TARGETS:       return "BAD_TARGETS";
        case SPELL_FAILED_CASTER_AURASTATE:  return "CASTER_AURASTATE";
        case SPELL_FAILED_INTERRUPTED:       return "INTERRUPTED";
        case SPELL_FAILED_LINE_OF_SIGHT:     return "LINE_OF_SIGHT";
        case SPELL_FAILED_MOVING:            return "MOVING";
        case SPELL_FAILED_NOT_BEHIND:        return "NOT_BEHIND";
        case SPELL_FAILED_NOT_READY:         return "NOT_READY(cooldown)";
        case SPELL_FAILED_NO_POWER:          return "NO_POWER(ресурс)";
        case SPELL_FAILED_OUT_OF_RANGE:      return "OUT_OF_RANGE";
        case SPELL_FAILED_POSSESSED:         return "POSSESSED";
        case SPELL_FAILED_SPELL_UNAVAILABLE: return "SPELL_UNAVAILABLE";
        case SPELL_FAILED_STUNNED:           return "STUNNED";
        case SPELL_FAILED_TRY_AGAIN:         return "TRY_AGAIN";
        default:                             return "E" + std::to_string(result);
    }
}

void PlayerbotDummyLog::HandleCastFail(Player const* caster, uint32 spellId, int32 result, Unit const* target)
{
    if (!caster)
        return;
    if (Record* rec = Find(caster->GetGUID().GetCounter()))
    {
        ++rec->castFails;
        ++rec->failCount[spellId];
        rec->failResult[spellId] = result;
        PushEv(*rec, 'F', spellId, result);
        std::ostringstream os;
        os << "CAST_FAIL spell=" << spellId
           << " name=\"" << SpellName(spellId) << '"'
           << " result=" << result
           << " (" << CastResultName(result) << ')';
        if (target)
            os << " target=\"" << target->GetName() << '"';
        Write(*rec, os.str());
    }
}

void PlayerbotDummyLog::Note(Player const* bot, std::string const& text)
{
    if (!bot)
        return;
    if (Record* rec = Find(bot->GetGUID().GetCounter()))
        Write(*rec, text);
}

void PlayerbotDummyLog::SetSweepSummary(Player const* bot, uint32 ok, uint32 fail, uint32 total)
{
    if (!bot)
        return;
    if (Record* rec = Find(bot->GetGUID().GetCounter()))
        rec->sweepSummary = "SWEEP ok=" + std::to_string(ok)
            + " fail=" + std::to_string(fail)
            + " total=" + std::to_string(total);
}

void PlayerbotDummyLog::NotePowerSpent(Player const* bot, uint32 spellId, int32 spent)
{
    if (!bot)
        return;
    if (Record* rec = Find(bot->GetGUID().GetCounter()))
    {
        PushEv(*rec, 'P', spellId, spent);
        std::ostringstream os;
        os << "PWR spell=" << spellId
           << " name=\"" << SpellName(spellId) << '"'
           << " spent=" << spent;
        Write(*rec, os.str());
    }
}

// ---------------------------------------------------------------- QA-отчёт

std::string PlayerbotDummyLog::WriteQaReport(Record const& rec, std::string const& sessionPath,
                                              std::string const& reason, float duration,
                                              bool qaRole, uint8 classId)
{
    // путь: <Bot>_<ts>.log → <Bot>_<ts>.qa.txt
    std::string reportPath = sessionPath;
    if (reportPath.size() >= 4 && reportPath.compare(reportPath.size() - 4, 4, ".log") == 0)
        reportPath.replace(reportPath.size() - 4, 4, ".qa.txt");
    else
        reportPath += ".qa.txt";

    std::ofstream f(reportPath, std::ios::out | std::ios::trunc);
    if (!f.is_open())
    {
        TC_LOG_ERROR("playerbots", "DummyLog: не удалось записать QA-отчёт {}", reportPath);
        return std::string();
    }

    std::vector<std::string> lines;
    std::unordered_set<uint32> flagged;      // спелл уже попал в находку — не дублируем
    uint32 findings = 0;
    auto add = [&lines](std::string s) { lines.push_back(std::move(s)); };

    auto lastResult = [&rec](uint32 spellId) -> int32
    {
        auto it = rec.failResult.find(spellId);
        return it != rec.failResult.end() ? it->second : 0;
    };
    auto isDamageSpell = [](SpellInfo const* si) -> bool
    {
        if (!si)
            return false;
        for (SpellEffectInfo const& eff : si->GetEffects())
            if (eff.Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
                return true;
        return false;
    };

    // --- атрибуция по талантам (только для роли QA): «спелл → влияющий талант» ---
    // Источники: (а) DBC TraitDefinition — талант-нода даёт/перекрывает/триггерит спелл;
    // (б) наша таблица playerbots_hero_talents — принадлежность спелла геро-дереву.
    // Строится один раз на отчёт; не-QA отчёты карту не строят (talentHint → пусто).
    std::unordered_map<uint32, std::vector<uint32>> talDirect, talGrants, talOverrides;
    std::unordered_map<uint32, std::string> heroTreeOf;
    if (qaRole)
    {
        for (TraitNodeEntryEntry const* oe : sTraitNodeEntryStore)
        {
            if (!oe)
                continue;
            TraitDefinitionEntry const* def = sTraitDefinitionStore.LookupEntry(oe->TraitDefinitionID);
            if (!def)
                continue;
            uint32 talent = def->SpellID > 0 ? uint32(def->SpellID)
                           : (def->VisibleSpellID > 0 ? uint32(def->VisibleSpellID) : 0);
            if (!talent)
                continue;
            if (def->SpellID > 0)
                talDirect[uint32(def->SpellID)].push_back(talent);
            if (def->VisibleSpellID > 0)
                talDirect[uint32(def->VisibleSpellID)].push_back(talent);
            if (def->OverridesSpellID > 0)
                talOverrides[uint32(def->OverridesSpellID)].push_back(talent);
            // талант-спелл, триггерящий/выдающий другой спелл (прок-фишки деревьев)
            if (SpellInfo const* tsi = sSpellMgr->GetSpellInfo(talent, DIFFICULTY_NONE))
                for (SpellEffectInfo const& eff : tsi->GetEffects())
                    if (eff.TriggerSpell)
                        talGrants[eff.TriggerSpell].push_back(talent);
        }

        // Query(const char*, …) — собираем строку отдельно и передаём c_str()
        std::string heroSql =
            "SELECT tree, spell_id FROM playerbots_hero_talents WHERE class = " + std::to_string(classId);
        if (QueryResult hres = WorldDatabase.Query(heroSql.c_str()))
        {
            do
            {
                Field* h = hres->Fetch();
                heroTreeOf[h[1].GetUInt32()] = h[0].GetString();
            } while (hres->NextRow());
        }
    }

    auto talentHint = [&](uint32 sp) -> std::string
    {
        if (!qaRole || !sp)
            return std::string();
        std::vector<uint32> cands;
        auto collect = [&cands](std::unordered_map<uint32, std::vector<uint32>> const& m, uint32 key)
        {
            auto it = m.find(key);
            if (it == m.end())
                return;
            for (uint32 v : it->second)
                if (std::find(cands.begin(), cands.end(), v) == cands.end())
                    cands.push_back(v);
        };
        collect(talDirect, sp);
        collect(talOverrides, sp);
        collect(talGrants, sp);

        auto hero = heroTreeOf.find(sp);
        std::string heroSuffix = hero != heroTreeOf.end() ? " [hero: " + hero->second + "]" : std::string();
        if (cands.empty())
            return hero != heroTreeOf.end()
                ? " | талант: герой-дерево " + hero->second + " (спелл " + std::to_string(sp) + ")"
                : std::string();

        std::string out = " | талант: ";
        for (size_t i = 0; i < cands.size() && i < 3; ++i)
        {
            if (i)
                out += ", ";
            out += SpellName(cands[i]) + " (" + std::to_string(cands[i]) + ")";
        }
        out += heroSuffix;
        return out;
    };

    // 1) неизвестные коды отказа — главная находка QA (непокрытый случай/баг ядра)
    for (auto const& kv : rec.failCount)
    {
        std::string rn = CastResultName(lastResult(kv.first));
        bool unknown = rn.size() > 1 && rn[0] == 'E' && std::isdigit(static_cast<unsigned char>(rn[1]));
        if (!unknown)
            continue;
        std::ostringstream os;
        os << "[BUG] unknown_cast_result spell=" << kv.first
           << " name=\"" << SpellName(kv.first) << '"'
           << " result=" << rn
           << " count=" << kv.second
           << " — код отказа не распознан: разобрать вручную (см. enum SpellCastResult)";
        os << talentHint(kv.first);
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 2) уронные спеллы (спендеры), отклонённые по ресурсу
    for (auto const& kv : rec.failCount)
    {
        if (flagged.count(kv.first))
            continue;
        if (CastResultName(lastResult(kv.first)).find("NO_POWER") == std::string::npos)
            continue;
        if (!isDamageSpell(sSpellMgr->GetSpellInfo(kv.first, DIFFICULTY_NONE)))
            continue;
        std::ostringstream os;
        os << "[WARN] resource_starved spell=" << kv.first
           << " name=\"" << SpellName(kv.first) << '"'
           << " count=" << kv.second
           << " — спендер отклонён по ресурсу (Holy Power и пр.)";
        os << talentHint(kv.first);
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 3) уронный спелл скастован, но ни одного DMG с его spellId не было
    for (auto const& kv : rec.casted)
    {
        if (flagged.count(kv.first) || rec.damaged.count(kv.first))
            continue;
        if (!isDamageSpell(sSpellMgr->GetSpellInfo(kv.first, DIFFICULTY_NONE)))
            continue;
        std::ostringstream os;
        os << "[SUSPECT] cast_without_damage spell=" << kv.first
           << " name=\"" << SpellName(kv.first) << '"'
           << " casts=" << kv.second
           << " — касты прошли, урона с этим spellId нет: свери лог (возможен баг/прок-замена)";
        os << talentHint(kv.first);
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 4) общий итог: были касты — нет авторитетного урона
    if (rec.totalDealt == 0 && rec.castCount > 0)
    {
        add("[BUG] zero_total_damage — касты были, total_dealt=0");
        ++findings;
    }

    // 5) повторяющиеся отказы с уже известными кодами
    for (auto const& kv : rec.failCount)
    {
        if (kv.second < 5 || flagged.count(kv.first))
            continue;
        std::ostringstream os;
        os << "[INFO] repeated_failure spell=" << kv.first
           << " name=\"" << SpellName(kv.first) << '"'
           << " result=" << CastResultName(lastResult(kv.first))
           << " count=" << kv.second;
        os << talentHint(kv.first);
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 6) сводка sweep (если QA-прогон был)
    if (!rec.sweepSummary.empty())
        add("[INFO] " + rec.sweepSummary);

    // 7) СЛОЙ 1 (авто из DBC): эффект ауры обещан, но аура не появлялась
    for (auto const& kv : rec.casted)
    {
        if (flagged.count(kv.first))
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(kv.first, DIFFICULTY_NONE);
        if (!si)
            continue;
        bool hasAuraEffect = false;
        for (SpellEffectInfo const& eff : si->GetEffects())
            if (eff.Effect == SPELL_EFFECT_APPLY_AURA)
            {
                hasAuraEffect = true;
                break;
            }
        if (!hasAuraEffect || rec.auraSeen.count(kv.first))
            continue;
        std::ostringstream os;
        os << "[SUSPECT] aura_not_applied spell=" << kv.first
           << " name=\"" << SpellName(kv.first) << '"'
           << " casts=" << kv.second
           << " — DBC обещает эффект ауры, но она не появлялась ни у бота, ни у цели"
              " (или аура короче поллинга300 мс) — свери лог";
        os << talentHint(kv.first);
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 8) СЛОЙ 1 (авто из DBC): цена > 0, но списание ресурса не зафиксировано
    {
        std::unordered_map<uint32, int32> spentMap;
        for (auto const& ev : rec.timeline)
            if (ev.ev == 'P')
                spentMap[ev.spell] += ev.v;

        for (auto const& kv : rec.casted)
        {
            if (flagged.count(kv.first))
                continue;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(kv.first, DIFFICULTY_NONE);
            if (!si)
                continue;
            bool costed = false;
            for (SpellPowerEntry const* pe : si->PowerCosts)
                if (pe && (pe->ManaCost > 0 || pe->PowerCostPct > 0.0f))
                {
                    costed = true;
                    break;
                }
            if (!costed)
                continue;
            int32 spent = 0;
            auto it = spentMap.find(kv.first);
            if (it != spentMap.end())
                spent = it->second;
            if (spent > 0)
                continue;
            std::ostringstream os;
            os << "[SUSPECT] power_not_spent spell=" << kv.first
               << " name=\"" << SpellName(kv.first) << '"'
               << " casts=" << kv.second
               << " — цена >0 по DBC, но списание не зафиксировано (см. строки PWR в логе):"
                  " бесплатный прок или баг";
            os << talentHint(kv.first);
            add(os.str());
            flagged.insert(kv.first);
            ++findings;
        }
    }

    // 9) СЛОЙ 2: ручные знания о механиках класса (playerbots_mechanics) — ТОЛЬКО QA.
    //    Обычные игровые боты эти знания не читают и не применяют (гейт по роли аккаунта).
    if (qaRole)
    {
        std::string mechSql =
            "SELECT spell_id, check_type, arg, window_ms, note FROM playerbots_mechanics"
            " WHERE class_id = " + std::to_string(classId) + " ORDER BY spell_id, check_type";
        QueryResult mres = WorldDatabase.Query(mechSql.c_str());

        if (!mres)
        {
            std::ostringstream os;
            os << "[INFO] mechanics_coverage class=" << uint32(classId)
               << " — в playerbots_mechanics нет строк для этого класса:"
                  " дополни sql/world_playerbots_mechanics.sql (после починки класса — знания под новые)";
            add(os.str());
            ++findings;
        }
        else
        {
            uint32 const durMs = uint32(duration * 1000.0f);
            do
            {
                Field* f = mres->Fetch();
                uint32  trig   = f[0].GetUInt32();
                std::string check = f[1].GetString();
                uint32  arg    = f[2].GetUInt32();
                uint32  window = f[3].GetUInt32();
                std::string note = f[4].GetString();

                auto firstCastT = [&rec](uint32 spell) -> int64
                {
                    for (auto const& ev : rec.timeline)
                        if (ev.ev == 'C' && ev.spell == spell)
                            return int64(ev.tMs);
                    return -1;
                };

                if (check == "power_cost")
                {
                    bool any = false, ok = false;
                    int32 got = 0;
                    for (auto const& ev : rec.timeline)
                        if (ev.ev == 'P' && ev.spell == trig)
                        {
                            any = true;
                            got = ev.v;
                            if (uint32(ev.v) == arg)
                            {
                                ok = true;
                                break;
                            }
                        }
                    if (ok)
                        continue;
                    if (!any)
                    {
                        if (rec.casted.count(trig))
                        {
                            std::ostringstream os;
                            os << "[INFO] mechanic_skipped check=power_cost spell=" << trig
                               << " — нет данных о списании (каст был, PWR не записан)";
                            os << talentHint(trig);
                            add(os.str());
                            ++findings;
                        }
                        continue;
                    }
                    std::ostringstream os;
                    os << "[SUSPECT] mechanic_power_cost spell=" << trig
                       << " name=\"" << SpellName(trig) << '"'
                       << " expected=" << arg << " got=" << got
                       << " — «" << note << "»: цена не сходится (бесплатный прок или баг)";
                    os << talentHint(trig);
                    add(os.str());
                    ++findings;
                    continue;
                }

                if (check != "proc_after" && check != "aura_after")
                {
                    std::ostringstream os;
                    os << "[INFO] mechanic_unknown_type check=\"" << check << "\" spell=" << trig
                       << " — тип не поддерживается этим ядром (обнови playerbots/cpp)";
                    os << talentHint(trig);
                    add(os.str());
                    ++findings;
                    continue;
                }

                int64 t0 = firstCastT(trig);
                if (t0 < 0)
                    continue;   // триггер не кастовался в сессии — нечего проверять
                if (durMs < uint32(t0) + window)
                {
                    std::ostringstream os;
                    os << "[INFO] mechanic_window_active check=" << check
                       << " trigger=" << trig << " expected_spell=" << arg
                       << " — окно " << window << " мс ещё не истекло внутри сессии, не проверено";
                    os << talentHint(arg);
                    add(os.str());
                    ++findings;
                    continue;
                }

                char want = (check == "aura_after") ? 'A' : 0;   // 0 = C или D
                bool ok = false;
                for (auto const& ev : rec.timeline)
                {
                    if (ev.tMs <= uint32(t0) || ev.tMs > uint32(t0) + window || ev.spell != arg)
                        continue;
                    if (want ? ev.ev == want : (ev.ev == 'C' || ev.ev == 'D'))
                    {
                        ok = true;
                        break;
                    }
                }
                if (ok)
                    continue;

                bool argCast = false;
                for (auto const& ev : rec.timeline)
                    if (ev.ev == 'C' && ev.spell == arg)
                    {
                        argCast = true;
                        break;
                    }
                if (check == "aura_after")
                {
                    // триггер кастован, окно истекло, ауры арга не было: арг мог висеть и от
                    // проков триггера — каст арга не требуется, доказательство исчерпано
                    std::ostringstream os;
                    os << "[BUG] mechanic_missing check=aura_after"
                       << " trigger=" << trig
                       << " expected_spell=" << arg
                       << " window_ms=" << window
                       << " — «" << note << "»: в окне после триггера аура не появилась:"
                          " механика не реализована/не сработала";
                    os << talentHint(arg);
                    add(os.str());
                    ++findings;
                    continue;
                }

                // proc_after: sweep проходит один раз — нужна соразмерная улика
                if (!argCast)
                {
                    std::ostringstream os;
                    os << "[INFO] mechanic_skipped check=proc_after"
                       << " trigger=" << trig << " expected_spell=" << arg
                       << " — «" << note << "»: арг ни разу не кастовался, не проверено";
                    os << talentHint(arg);
                    add(os.str());
                    ++findings;
                    continue;
                }

                {   // арг кастовался, но не после триггера в окне: порядок sweep мог не совпасть.
                    // BUG — только если ПОСЛЕ триггера пробовали и отказали.
                    bool triedAndFailed = false;
                    for (auto const& ev : rec.timeline)
                        if (ev.ev == 'F' && ev.spell == arg &&
                            ev.tMs > uint32(t0) && ev.tMs <= uint32(t0) + window)
                        {
                            triedAndFailed = true;
                            break;
                        }
                    std::ostringstream os;
                    if (!triedAndFailed)
                    {
                        os << "[INFO] mechanic_recheck check=proc_after trigger=" << trig
                           << " expected_spell=" << arg
                           << " — «" << note << "»: арг был только вне окна после триггера,"
                              " повтори sweep (порядок кастов мог не совпасть)";
                    }
                    else
                    {
                        os << "[BUG] mechanic_missing check=proc_after"
                           << " trigger=" << trig
                           << " expected_spell=" << arg
                           << " window_ms=" << window
                           << " — «" << note << "»: после триггера арг пробовали и отказали"
                              " — механика не реализована/не сработала";
                    }
                    os << talentHint(arg);
                    add(os.str());
                    ++findings;
                }
            } while (mres->NextRow());
        }
    }

    // статистика сессии
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(1)
           << "[STAT] duration_s=" << duration
           << " total_dealt=" << rec.totalDealt
           << " casts=" << rec.castCount
           << " cast_fails=" << rec.castFails;
        add(os.str());
    }

    f << "# playerbots QA report v1\n";
    f << "# bot=" << rec.botName
      << " target=" << rec.targetName
      << " entry=" << rec.targetEntry << '\n';
    f << "# generated=" << TimeStamp("%Y-%m-%dT%H:%M:%S")
      << " duration_s=" << std::fixed << std::setprecision(1) << duration
      << " reason=\"" << reason << "\"\n";
    f << "# session_log=" << sessionPath << "\n";
    f << "FINDINGS " << findings << "\n";
    for (std::string const& l : lines)
        f << l << '\n';
    f.close();
    return reportPath;
}
