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
        std::chrono::steady_clock::time_point start;
        uint32 pollAccMs = 0;
        uint64 totalDealt = 0;
        uint32 castCount = 0;
        uint32 castFails = 0;                                   // QA: отклонённые касты
        std::unordered_map<uint32 /*spellId*/, AuraSnap> botAuras;
        std::unordered_map<uint32 /*spellId*/, AuraSnap> targetAuras;
    };

    Record* Find(uint32 botCounter);
    void PollAuras(Record& rec, Player* bot);
    void Write(Record& rec, std::string const& line);

    std::unordered_map<uint32 /*bot guid counter*/, Record> m_records;
    uint32 m_globalPollMs = 0;
};

#define sPlayerbotDummyLog PlayerbotDummyLog::Instance()

// регистрация ScriptMgr-хуков (вызывается из AddSC_playerbots)
void AddSC_playerbots_dummylog();

#endif // PLAYERBOTS_DUMMY_LOG_H
