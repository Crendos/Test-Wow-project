/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Консольные/in-game команды, в чате: .playerbots <подкоманда>
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Config.h"
#include "Language.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

namespace
{
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

            static ChatCommandTable playerbotsCommandTable =
            {
                { "add",         HandleBotAddCommand,            static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "list",        HandleBotListCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "ping",        HandleBotPingCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "remove",      HandleBotRemoveCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "removeall",   HandleBotRemoveAllCommand,      static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "followme",    HandleBotFollowMeCommand,       static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stay",        HandleBotStayCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
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
                return false;
            }
            std::string const botName{name};
            if (sPlayerbotMgr.AddBot(botName, handler->GetPlayer(), SEC_PLAYER) != PlayerbotMgr::Status::OK)
            {
                handler->SendSysMessage("Не удалось добавить бота.");
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
                handler->SendSysMessage(n.c_str());
            return true;
        }

        // .playerbots followme <имя>  — бот привязывается к тебе и следует
        static bool HandleBotFollowMeCommand(ChatHandler* handler, Tail name)
        {
            if (name.empty())
            {
                handler->SendSysMessage("Использование: .playerbots followme <имя>");
                return false;
            }
            Player* master = handler->GetPlayer();
            if (!master)
            {
                handler->SendSysMessage("Команда доступна только из игры (требуется мастер).");
                return false;
            }
            std::string const botName{name};
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
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
                return false;
            }
            ai->ClearFollow();
            handler->SendSysMessage("Бот остановлен.");
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
                // sConfigMgr->SetIntDefault запишет NonVolatile в配置文件 — отложим до v1
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
        }
    };

void AddSC_playerbots()
{
    static_cast<void>( new playerbots_commandscript() );
    new playerbots_worldscript();
}
