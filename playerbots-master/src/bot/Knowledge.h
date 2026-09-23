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
        DoT       = 6,   // DoT/дебафф урона: обновляем на цели
        Dispel    = 7    // v5: диспел себя (бот_аура правило → dispel_self)
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

// ---------------------------------------------------------------------------
// v5: правила босс-механик (world.playerbots_boss_rules)
// Движок AI на каждом тике боя с боссом проходит правила по seq и выполняет
// первое сработавшее (с per-rule кулдауном, по умолчанию 1.5с).
// ---------------------------------------------------------------------------
struct BossRule
{
    enum class Trigger : uint8
    {
        BossCast    = 0,  // босс сейчас кастует spellId (TriggerArg)
        BossAura    = 1,  // на боссе висит аура (TriggerArg = spellId)
        BotAura     = 2,  // на боте висит аура (TriggerArg = spellId)
        BossHpBelow = 3,  // HP босса ниже % (TriggerArg)
        Always      = 4   // всегда в бою (полезно с per-rule кулдауном)
    };

    enum class Action : uint8
    {
        Interrupt    = 0, // сбить боссу каст известным interrupt-спелом
        RunFromBoss  = 1, // отбежать от босса на ActionArg ярдов
        Spread       = 2, // отойти от ближайшего союзника на ActionArg ярдов
        Sidestep     = 3, // шаг в сторону на ActionArg ярдов (выход из лужи)
        SwitchTarget = 4, // переключить цель на creature entry ActionArg
        UseDefensive = 5, // принудительно сжать защитный кулдаун
        DispelSelf   = 6, // диспел с себя известным dispel-спелом
        UseBurst     = 7  // принудительно открыть burst-окно на 5с
    };

    uint32  BossEntry   = 0;
    Trigger TriggerType = Trigger::BossCast;
    uint32  TriggerArg  = 0;
    Action  ActionType  = Action::Interrupt;
    uint32  ActionArg   = 0;
    uint32  Seq         = 100;   // меньше = важнее
    uint32  CooldownMs  = 1500;  // per-rule, не спамить действием каждый тик
};

#endif // PLAYERBOT_KNOWLEDGE_H
