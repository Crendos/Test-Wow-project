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
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
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

    std::string dir = sConfigMgr->GetOption<std::string>("Playerbots.DummyLogDir", "PlayerbotsLogs");
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

    // QA-анализ сессии → отдельный файл <session>.qa.txt (рядом с логом)
    std::string reportPath = WriteQaReport(rec, rec.path, reason, duration);
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
    auto* snaps[2]  = { &rec.botAuras, &rec.targetAuras };

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
            auto it = snap.find(kv.first);
            if (it == snap.end())
            {
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
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(HighGuid::Player, it->first));
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

// ---------------------------------------------------------------- QA-отчёт

std::string PlayerbotDummyLog::WriteQaReport(Record const& rec, std::string const& sessionPath,
                                              std::string const& reason, float duration)
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
        add(os.str());
        flagged.insert(kv.first);
        ++findings;
    }

    // 6) сводка sweep (если QA-прогон был)
    if (!rec.sweepSummary.empty())
        add("[INFO] " + rec.sweepSummary);

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
