/*
 * PLAYERBOTS — общая структура знаний (связка Mgr/AI)
 */
#ifndef PLAYERBOT_KNOWLEDGE_H
#define PLAYERBOT_KNOWLEDGE_H

#include "Define.h"

// v3 «классовое знание»: запись «спел + когда применять» (ручная БД или авто из спелбукка)
struct BotKnowledge
{
    enum class Kind : uint8 { Damage = 0, Heal = 1, SelfBuff = 2, Defensive = 4, Interrupt = 5 };

    uint32 spellId    = 0;
    Kind   kind       = Kind::Damage;
    uint16 priority   = 100;   // больше = раньше в цикле
    uint8  selfHpMax  = 100;   // % здоровья бота, ниже которого спел доступен (для Defensive/Heal)
    uint8  targetHpMin= 0;     // % здоровья цели: допустимый диапазон (для execute-подобных)
    uint8  targetHpMax= 100;
    bool   maintainAura = false; // self-spell: поддерживаем пока ауры нет у бота
};

#endif // PLAYERBOT_KNOWLEDGE_H
