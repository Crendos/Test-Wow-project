/* PLAYERBOTS под TrinityCore master — v6
 * DummyLog: запись лога боя с манекеном (все ауры/бафы, урон, способности, проки).
 * Всё в world-thread: старт/стоп из команд, хуки из ScriptMgr, поллинг аур по тику.
 * Файл лога: Playerbots.DummyLogDir (по умолчанию PlayerbotsLogs/) <bot>_<дата>.log
 */
#ifndef PLAYERBOTS_DUMMY_LOG_H
#define PLAYERBOTS_DUMMY_LOG_H

#include "Define.h"
#include "ObjectGuid.h"

#include <chrono>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class Unit;
class Spell;
class SpellInfo;

class PlayerbotDummyLog
{
public:
    static PlayerbotDummyLog& Instance();

    // старт/стоп записи для конкретного бота (по одному активному логу на бота)
    void StartRecording(Player* bot, Unit const* target);
    std::string StopRecording(Player* bot, std::string const& reason);   // возвращает путь к файлу ("" = не записывалось)
    bool IsRecording(Player const* bot) const;

    // периодический снапшот аур — вызывается из playerbots_worldscript::OnUpdate
    void Update(uint32 diff);

    // входы из ScriptMgr-хуков (см. AddSC_playerbots_dummylog в DummyLog.cpp)
    void HandleFinalDamage(Unit const* attacker, uint32 amount);                       // OnDamage — авторитетный итог
    void HandleSpellDamage(Unit const* attacker, Unit const* victim, uint32 spellId, int32 amount);
    void HandleMeleeDamage(Unit const* attacker, Unit const* victim, uint32 amount);
    void HandlePeriodicDamage(Unit const* attacker, Unit const* victim, uint32 amount);
    void HandleHeal(Unit const* healer, Unit const* victim, uint32 amount);
    void HandleCast(Player const* caster, Spell const* spell);

    // QA-слой: неудачный каст (CastSpell вернул != SPELL_CAST_OK) и произвольные заметки движка
    void HandleCastFail(Player const* caster, uint32 spellId, int32 result, Unit const* target);
    void Note(Player const* bot, std::string const& text);   // строка SWEEP/прочее в текущий лог

    // сводка sweep для QA-отчёта (вызывается из PlayerbotAI::StopDummy до StopRecording)
    void SetSweepSummary(Player const* bot, uint32 ok, uint32 fail, uint32 total);

    // слой 1 (авто): фактическое списание ресурса после успешного каста
    // (вызывается из PlayerbotAI::CastSpellAt; spent=0 при цене>0 = прок/баг — см. PWR в логе)
    void NotePowerSpent(Player const* bot, uint32 spellId, int32 spent);

    static std::string SpellName(uint32 spellId);
    static std::string CastResultName(int32 result);         // SpellCastResult → человекочитаемое имя

private:
    struct AuraSnap
    {
        uint32 spellId = 0;
        ObjectGuid caster;
        uint8 stacks = 0;
        bool positive = false;
        std::string name;
    };

    struct Record
    {
        std::ofstream file;
        std::string path;
        ObjectGuid targetGuid;
        uint32 targetEntry = 0;
        std::string targetName;
        std::string botName;                                    // для заголовка QA-отчёта
        std::chrono::steady_clock::time_point start;
        uint32 pollAccMs = 0;
        uint64 totalDealt = 0;
        uint32 castCount = 0;
        uint32 castFails = 0;                                   // QA: отклонённые касты
        // --- агрегаты для QA-отчёта (.qa.txt) ---
        std::unordered_map<uint32 /*spellId*/, uint32> casted;   // сколько раз каждый спелл скастован
        std::unordered_set<uint32 /*spellId*/> damaged;          // спеллы, дававшие урон
        std::unordered_map<uint32 /*spellId*/, uint32> failCount;
        std::unordered_map<uint32 /*spellId*/, int32> failResult;// последний код отказа
        std::string sweepSummary;                               // "SWEEP ok=... fail=..." если был sweep
        std::unordered_map<uint32 /*spellId*/, AuraSnap> botAuras;
        std::unordered_map<uint32 /*spellId*/, AuraSnap> targetAuras;
        // --- слои 1-2 QA (авто-ожидания из DBC + playerbots_mechanics) ---
        std::unordered_set<uint32 /*spellId*/> auraSeen;   // все ауры, когда-либо виденные (self+target)
        struct Ev
        {
            uint32 tMs;    // от старта записи
            char   ev;     // C=cast, D=damage, F=cast_fail, A=aura_apply, P=power_spent
            uint32 spell;
            int32  v;      // amount / result / spent
        };
        std::vector<Ev> timeline;                            // для проверки окон механик (cap 20000)
    };

    Record* Find(uint32 botCounter);
    void PollAuras(Record& rec, Player* bot);
    void Write(Record& rec, std::string const& line);
    void PushEv(Record& rec, char ev, uint32 spell, int32 v);   // событие в таймлайн (для механик)
    // QA-отчёт: анализ агрегатов сессии → файл <session>.qa.txt; возвращает путь.
    // qaRole — знания playerbots_mechanics читаются ТОЛЬКО для роли QA; classId — ключ слоя 2.
    std::string WriteQaReport(Record const& rec, std::string const& sessionPath,
                              std::string const& reason, float duration, bool qaRole, uint8 classId);

    std::unordered_map<uint32 /*bot guid counter*/, Record> m_records;
    uint32 m_globalPollMs = 0;
};

#define sPlayerbotDummyLog PlayerbotDummyLog::Instance()

// регистрация ScriptMgr-хуков (вызывается из AddSC_playerbots)
void AddSC_playerbots_dummylog();

#endif // PLAYERBOTS_DUMMY_LOG_H
