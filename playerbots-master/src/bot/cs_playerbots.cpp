/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Консольные/in-game команды, в чате: .playerbots <подкоманда>
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DummyLog.h"
#include "Language.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <sstream>
#include <vector>

using namespace Trinity::ChatCommands;

namespace
{
    uint32 ToU32(std::string const& s)
    {
        if (s.empty())
            return 0;
        for (char ch : s)
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return 0;
        try { return uint32(std::stoul(s)); } catch (...) { return 0; }
    }

    std::string LowerStr(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    class playerbots_commandscript : public CommandScript
    {
    public:
        playerbots_commandscript() : CommandScript("playerbots_commandscript") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable playerbotsRosterCommandTable =
            {
                { "start",       HandleRosterStartCommand,       static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stop",        HandleRosterStopCommand,        static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "rotation",    HandleRosterRotationCommand,    static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
            };

            static ChatCommandTable playerbotsDummyCommandTable =
            {
                { "start",       HandleBotDummyStartCommand,     static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stop",        HandleBotDummyStopCommand,      static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
            };

            static ChatCommandTable playerbotsCommandTable =
            {
                { "add",         HandleBotAddCommand,            static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "create",      HandleBotCreateCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "list",        HandleBotListCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "ping",        HandleBotPingCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "remove",      HandleBotRemoveCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "removeall",   HandleBotRemoveAllCommand,      static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "followme",    HandleBotFollowMeCommand,       static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stay",        HandleBotStayCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "book",        HandleBotBookCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "equip",       HandleBotEquipCommand,          static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "dummy",       playerbotsDummyCommandTable },
                { "roster",      playerbotsRosterCommandTable },
            };

            static ChatCommandTable commandTable =
            {
                { "playerbots",  playerbotsCommandTable },
            };

            return commandTable;
        }

        // .playerbots add <имя>
        static bool HandleBotAddCommand(ChatHandler* handler, Tail name)
        {
            if (name.empty())
            {
                handler->SendSysMessage("Использование: .playerbots add <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string const botName{name};
            if (sPlayerbotMgr.AddBot(botName, handler->GetPlayer(), SEC_PLAYER) != PlayerbotMgr::Status::OK)
            {
                handler->SendSysMessage("Не удалось добавить бота.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage("Бот принят.");
            return true;
        }

        // .playerbots list
        static bool HandleBotListCommand(ChatHandler* handler)
        {
            std::vector<std::string> bots = sPlayerbotMgr.GetBotsOnline();
            if (bots.empty())
            {
                handler->SendSysMessage("Боты не онлайны.");
                return true;
            }
            for (std::string const& n : bots)
                handler->SendSysMessage(fmt::format("{}{}", n, sPlayerbotMgr.IsBotQA(n) ? "  [QA]" : ""));
            return true;
        }

        // .playerbots followme <имя>  — бот привязывается к тебе и следует
        static bool HandleBotFollowMeCommand(ChatHandler* handler, Tail name)
        {
            if (name.empty())
            {
                handler->SendSysMessage("Использование: .playerbots followme <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* master = handler->GetPlayer();
            if (!master)
            {
                handler->SendSysMessage("Команда доступна только из игры (требуется мастер).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            ai->SetMasterAndFollow(master);
            handler->SendSysMessage("Бот следует за вами.");
            return true;
        }

        // .playerbots stay <имя>  — бот отвязывается и остаётся на месте
        static bool HandleBotStayCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            ai->ClearFollow();
            handler->SendSysMessage("Бот остановлен.");
            return true;
        }

        // .playerbots equip <имя>  — бот наденет лучшее из сумок по классу
        static bool HandleBotEquipCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            ai->EquipBestItems();
            handler->SendSysMessage("Экипировка пересчитана.");
            return true;
        }

        // .playerbots create <account> class=paladin [race=N] [gender=m|f] [level=N]
        //                     [spec=dps|heal|tank|имя|ID] [hero=templar] [item=ID[,ID]] [name=X]
        static bool HandleBotCreateCommand(ChatHandler* handler, Tail args)
        {
            if (args.empty())
            {
                handler->SendSysMessage("Использование: .playerbots create <account> class=паладин|2 [race=N] [gender=m|f] [level=N] [spec=dps|heal|tank|имя|ID] [hero=templar] [item=ID[,ID...]] [name=Имя]");
                handler->SendSysMessage("Пример: .playerbots create 9001 class=paladin level=80 spec=dps hero=templar item=2000");
                handler->SetSentErrorMessage(true);
                return false;
            }

            BotCreateCriteria c;
            std::string accountTok;
            bool first = true;
            std::istringstream is{ std::string(args) };
            std::string tok;
            while (is >> tok)
            {
                if (first)
                {
                    first = false;
                    if (tok.find('=') == std::string::npos)
                    {
                        accountTok = tok;
                        continue;
                    }
                }

                auto eq = tok.find('=');
                if (eq == std::string::npos)
                {
                    handler->SendSysMessage(fmt::format("Неожиданный токен '{}': ожидались key=value", tok));
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                std::string k = tok.substr(0, eq);
                std::string v = tok.substr(eq + 1);

                if (k == "class")
                    c.classId = PlayerbotMgr::ParseClassToken(v);
                else if (k == "race")
                    c.raceId = ToU32(v);
                else if (k == "level")
                    c.level = std::max<uint32>(1, ToU32(v));
                else if (k == "spec")
                    c.spec = v;
                else if (k == "hero")
                    c.hero = v;
                else if (k == "name")
                    c.name = v;
                else if (k == "item")
                {
                    std::istringstream items{ v };
                    std::string one;
                    while (std::getline(items, one, ','))
                        if (uint32 id = ToU32(one))
                            c.items.push_back(id);
                        else if (!one.empty())
                        {
                            handler->SendSysMessage(fmt::format("item: не число '{}'", one));
                            handler->SetSentErrorMessage(true);
                            return false;
                        }
                }
                else if (k == "gender")
                {
                    std::string g = LowerStr(v);
                    if (g == "m" || g == "male" || g == "0")
                        c.gender = 0;
                    else if (g == "f" || g == "female" || g == "1")
                        c.gender = 1;
                    else
                    {
                        handler->SendSysMessage(fmt::format("gender: '{}'. Нужно m|f.", v));
                        handler->SetSentErrorMessage(true);
                        return false;
                    }
                }
                else
                {
                    handler->SendSysMessage(fmt::format("Неизвестный параметр '{}'", k));
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }

            uint32 accountId = accountTok.empty() ? 0 : ToU32(accountTok);
            if (!accountId)
            {
                handler->SendSysMessage("Нужен accountId первым аргументом (напр. 9001).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!c.classId)
            {
                handler->SendSysMessage("Нужен class= (paladin/2/warrior/...).");
                handler->SetSentErrorMessage(true);
                return false;
            }

            BotCreateResult r = sPlayerbotMgr.CreateCharacter(accountId, c);
            if (!r.ok)
            {
                handler->SendSysMessage(fmt::format("Ошибка: {}", r.error));
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage(fmt::format("Создано: {} (guid {}, account {})", r.name, r.guid, accountId));
            handler->SendSysMessage(fmt::format("Роль: {}", sPlayerbotMgr.IsQAAccount(accountId)
                ? "QA (sweep, CAST_FAIL, быстрые ретраи фейлов)"
                : "игровой (обычная ротация, без QA-шума)"));
            if (!r.error.empty())
                handler->SendSysMessage(fmt::format("Замечания: {}", r.error));
            handler->SendSysMessage(fmt::format("Дальше: .playerbots add {} ; бой: .playerbots dummy start {} <цель>", r.name, r.name));
            return true;
        }

        // .playerbots dummy start <бот> [entry] [x y z]  — бот бьёт манекен, пишется лог
        static bool HandleBotDummyStartCommand(ChatHandler* handler, Tail args)
        {
            if (args.empty())
            {
                handler->SendSysMessage("Использование: .playerbots dummy start <бот> [entry манекена] [x y z] [sweep]");
                handler->SendSysMessage("Цель: твой выделенный юнит, иначе entry, иначе авто-поиск '%Dummy%' рядом с ботом.");
                handler->SendSysMessage("sweep = QA-прогон: каждый боевой спелл попытка раз с логом SWEEP/CAST_FAIL.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::vector<std::string> t;
            bool sweep = false;
            {
                std::istringstream is{ std::string(args) };
                std::string w;
                while (is >> w)
                {
                    if (w == "sweep")
                        sweep = true;
                    else
                        t.push_back(w);
                }
            }
            if (t.empty())
            {
                handler->SendSysMessage("Использование: .playerbots dummy start <бот> [entry] [x y z] [sweep]");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const botName = t[0];
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (ai->IsDummyMode())
            {
                handler->SendSysMessage("Бот уже в режиме манекена (сначала .playerbots dummy stop).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* bot = ai->GetBot();
            if (!bot)
            {
                handler->SendSysMessage("Бот оффлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            uint32 entry = 0;
            Position dest;
            bool hasDest = false;
            size_t idx = 1;
            if (t.size() == idx + 1)
            {
                entry = ToU32(t[idx]);
                ++idx;
            }
            else if (t.size() == idx + 3 || t.size() == idx + 4)
            {
                if (t.size() == idx + 4)
                {
                    entry = ToU32(t[idx]);
                    ++idx;
                }
                try
                {
                    dest.m_positionX = std::stof(t[idx]);
                    dest.m_positionY = std::stof(t[idx + 1]);
                    dest.m_positionZ = std::stof(t[idx + 2]);
                }
                catch (...)
                {
                    handler->SendSysMessage("Координаты x y z — числа с точкой.");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                hasDest = true;
                idx += 3;
            }
            if (idx != t.size())
            {
                handler->SendSysMessage("Использование: .playerbots dummy start <бот> [entry] [x y z] [sweep]");
                handler->SetSentErrorMessage(true);
                return false;
            }

            Creature* target = nullptr;
            if (entry)
            {
                target = bot->FindNearestCreature(entry, 300.0f);
                if (!target)
                {
                    handler->SendSysMessage(fmt::format("creature entry {} не найден рядом с ботом (300 ярд).", entry));
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }
            else
            {
                if (Unit* sel = handler->getSelectedUnit())
                    target = sel->ToCreature();

                if (!target)
                {
                    if (QueryResult qr = WorldDatabase.Query(
                        "SELECT entry FROM creature_template "
                        "WHERE name LIKE '%Dummy%' OR name LIKE '%Манекен%' OR name LIKE '%Training%' LIMIT 30"))
                    {
                        do
                        {
                            uint32 e = qr->Fetch()[0].GetUInt32();
                            if ((target = bot->FindNearestCreature(e, 300.0f)))
                                break;
                        } while (qr->NextRow());
                    }
                }
                if (!target)
                {
                    handler->SendSysMessage("Манекен не найден: выдели цель (или укажи entry).");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }

            std::string err;
            if (!ai->StartDummy(target->GetGUID(), hasDest ? &dest : nullptr, err))
            {
                handler->SendSysMessage(fmt::format("Ошибка: {}", err));
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (sweep)
            {
                if (ai->EnableDummySweep())
                    handler->SendSysMessage("QA-sweep включён: каждый боевой спелл — попытка раз (строки SWEEP/CAST_FAIL в логе).");
                else
                    handler->SendSysMessage("sweep недоступен: это привилегия QA-ботов (аккаунт в Playerbots.QAAccountsStart/End, см. docs/08).");
            }
            handler->SendSysMessage(fmt::format("Бот {} бьёт «{}» (entry {}). Лог пишется; стоп: .playerbots dummy stop {}",
                botName, target->GetName(), target->GetEntry(), botName));
            if (hasDest)
                handler->SendSysMessage("Сначала подбег к указанной точке, затем бой.");
            return true;
        }

        // .playerbots dummy stop <бот>  — остановить бой, закрыть лог с SUMMARY
        static bool HandleBotDummyStopCommand(ChatHandler* handler, Tail name)
        {
            if (name.empty())
            {
                handler->SendSysMessage("Использование: .playerbots dummy stop <бот>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!ai->IsDummyMode())
            {
                handler->SendSysMessage("Бот не в режиме манекена.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string path = ai->StopDummy("по команде");
            if (!path.empty())
            {
                handler->SendSysMessage(fmt::format("Лог боя: {}", path));
                // QA-отчёт пишется рядом: тот же путь с расширением .qa.txt
                std::string report = path;
                if (report.size() >= 4 && report.compare(report.size() - 4, 4, ".log") == 0)
                    report.replace(report.size() - 4, 4, ".qa.txt");
                else
                    report += ".qa.txt";
                handler->SendSysMessage(fmt::format("QA-отчёт (аномалии/баги): {}", report));
            }
            else
                handler->SendSysMessage("Режим снят (файл лога не создавался).");
            return true;
        }

        // .playerbots book <имя>  — спелы, известные боту по загруженному знанию
        static bool HandleBotBookCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            for (uint32 sid : ai->ListKnownSpelIDs())
                handler->SendSysMessage(fmt::format("spell {}", sid));
            return true;
        }

        // .playerbots ping <имя>  (заглушка-плашка к боту)
        static bool HandleBotPingCommand(ChatHandler* handler, Tail /*name*/)
        {
            handler->SendSysMessage("ping: плане интерфейс, поведение PingMaster() встроено в AI.");
            return true;
        }

        // .playerbots remove <имя>
        static bool HandleBotRemoveCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (!botName.empty() && sPlayerbotMgr.RemoveBot(botName) == PlayerbotMgr::Status::OK)
            {
                handler->SendSysMessage("Бот удалён.");
                return true;
            }
            handler->SendSysMessage("Удалить не удалось: только своего бота можно убрать.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        // .playerbots removeall
        static bool HandleBotRemoveAllCommand(ChatHandler* handler)
        {
            sPlayerbotMgr.RemoveAll();
            handler->SendSysMessage("Все боты сняты.");
            return true;
        }

        // .playerbots roster start
        static bool HandleRosterStartCommand(ChatHandler* handler)
        {
            sPlayerbotMgr.StartRoster();
            if (sPlayerbotMgr.IsRosterRunning())
            {
                handler->SendSysMessage("Состав запущен.");
                return true;
            }
            handler->SendSysMessage("Состав не запустился: проверь таблицу world.playerbots_rotation.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        // .playerbots roster stop
        static bool HandleRosterStopCommand(ChatHandler* handler)
        {
            sPlayerbotMgr.StopRoster();
            sPlayerbotMgr.RemoveAll();
            handler->SendSysMessage("Состав остановлен.");
            return true;
        }

        // .playerbots roster rotation <минуты>
        static bool HandleRosterRotationCommand(ChatHandler* handler, uint32 minutes)
        {
            if (minutes < 1)
                minutes = sPlayerbotMgr.GetRosterRotationMinutes();

            sPlayerbotMgr.SetRosterRotationMinutes(minutes);
            if (handler->GetPlayer())
            {
                // сообщение через конфиг-менедажер, т.к. доступно и из world-console
                // sConfigMgr-геттеров для записи нет — отложим до v1
            }
            handler->SendSysMessage("Ротация состава: каждые минут.");
            return true;
        }
    };
}

    // Тикер AI + автостарт состава: скриптовый хук в worldserver-цикл (нет правок в game/lib)
    class playerbots_worldscript : public WorldScript
    {
    public:
        playerbots_worldscript() : WorldScript("playerbots_worldscript") { }

        void OnStartup() override
        {
            if (sConfigMgr->GetBoolDefault("Playerbots.AutoStartRoster", false))
                sPlayerbotMgr.StartRoster();
        }

        void OnUpdate(uint32 diff) override
        {
            sPlayerbotMgr.UpdateAI(diff);
            sPlayerbotDummyLog.Update(diff);    // поллинг аур для логов боя с манекеном
        }
    };

void AddSC_playerbots()
{
    AddSC_playerbots_dummylog();                // UnitScript+PlayerScript: урон/хил/касты в лог
    static_cast<void>( new playerbots_commandscript() );
    new playerbots_worldscript();
}
