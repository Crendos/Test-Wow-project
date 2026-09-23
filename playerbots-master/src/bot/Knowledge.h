/*
 * PLAYERBOTS — общая структура знаний (связка Mgr/AI)
 */
#ifndef PLAYERBOT_KNOWLEDGE_H
#define PLAYERBOT_KNOWLEDGE_H

#include "Define.h"

// v3/v4 «классовое знание»: «спел + когда применять» (ручная БД или авто из спелбукка)
struct BotKnowledge
{
    enum class Kind : uint8
    {
        Damage    = 0,   // обычный урон по врагу
        Heal      = 1,   // лечение (себя/мастер)
        SelfBuff  = 2,   // self-бафф, поддерживаем отсутствием ауры у бота
        Debuff    = 3,
        Defensive = 4,   // большая защита при малом HP (Ice Block/Divine Shield и пр.)
        Interrupt = 5,   // reserved (v4+ реакция на каст цели)
        DoT       = 6    // DoT/дебафф урона: обновляем на цели
    };

    uint32 spellId    = 0;
    Kind   kind       = Kind::Damage;
    uint16 priority   = 100;   // больше = раньше в цикле
    uint8  selfHpMax  = 100;   // % HP бота, ниже которого спел допустим (Defensive/Heal)
    uint8  targetHpMin= 0;     // % HP цели (для execute-подобных)
    uint8  targetHpMax= 100;
    bool   maintainAura = false; // DoT/SelfBuff: каст, пока ауры нет у цели/бота
    bool   burst      = false; // v4: большой кулдаун — жатвить в начале боя/при HP<30
};

#endif // PLAYERBOT_KNOWLEDGE_H
