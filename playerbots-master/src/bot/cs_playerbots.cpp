/*
 * PLAYERBOTS под TrinityCore master — Фаза 0 MVP
 * Консольные/in-game команды, в чате: .playerbots <подкоманда>
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "CharacterCache.h"   // sCharacterCache — .playerbots delete (кэш имён)
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DummyLog.h"
#include "Language.h"
#include "MidnightBotMgr.h"    // их движок: команды-замены .mbot/.mb (слияние, docs/09)
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
                { "delete",      HandleBotDeleteCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "followme",    HandleBotFollowMeCommand,       static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stay",        HandleBotStayCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "summon",      HandleBotSummonCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "book",        HandleBotBookCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "hero",        HandleBotHeroCommand,           static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "equip",       HandleBotEquipCommand,          static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "talents",     HandleBotTalentsCommand,       static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "stats",       HandleBotStatsCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "role",        HandleBotRoleCommand,          static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "assist",      HandleBotAssistCommand,        static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "attack",      HandleBotAttackCommand,        static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "boost",       HandleBotBoostCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "boostall",    HandleBotBoostAllCommand,      static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "party",       HandleBotPartyCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "loot",        HandleBotLootCommand,          static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "resurrect",   HandleBotResurrectCommand,     static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "learn",       HandleBotLearnCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
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
            {
                std::string const suffix = sPlayerbotMgr.IsBotQA(n) ? "  [QA]" : "";
                if (PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(n); ai && ai->GetBot())
                {
                    Player* b = ai->GetBot();
                    handler->SendSysMessage(fmt::format("{}{}  map {}  ({:.0f} {:.0f} {:.0f})",
                        n, suffix, b->GetMapId(), b->GetPositionX(), b->GetPositionY(), b->GetPositionZ()));
                }
                else
                    handler->SendSysMessage(fmt::format("{}{}", n, suffix));
            }
            return true;
        }

        // .playerbots summon <имя>  — телепортировать бота к себе (в т.ч. с другой карты)
        static bool HandleBotSummonCommand(ChatHandler* handler, Tail name)
        {
            if (name.empty())
            {
                handler->SendSysMessage("Использование: .playerbots summon <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* master = handler->GetPlayer();
            if (!master)
            {
                handler->SendSysMessage("Команда доступна только из игры (требуется игрок).");
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
            Player* bot = ai->GetBot();
            if (!bot)
            {
                handler->SendSysMessage("Бот офлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (bot == master)
            {
                handler->SendSysMessage("Это твой собственный персонаж.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            uint32 const fromMap = bot->GetMapId();
            float const fx = bot->GetPositionX(), fy = bot->GetPositionY(), fz = bot->GetPositionZ();
            if (!bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(),
                master->GetPositionZ(), master->GetOrientation()))
            {
                handler->SendSysMessage("TeleportTo не сработал — причина в консоли мира (LOG: playerbots / maps).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage(fmt::format("Бот {} телепортирован к вам (был map {} {:.0f} {:.0f} {:.0f}).",
                botName, fromMap, fx, fy, fz));
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
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots equip <имя> — бот должен быть онлайн (сначала .playerbots add <имя>); пример: .playerbots equip Idg");
                handler->SetSentErrorMessage(true);
                return false;
            }
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SendSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
                handler->SetSentErrorMessage(true);
                return false;
            }
            ai->EquipBestItems();
            handler->SendSysMessage("Экипировка пересчитана.");
            return true;
        }

        // .playerbots talents <имя>  — применить ЗАПИСАННЫЙ конфиг талантов к живому боту
        // (character_trait_entry существует, но ApplyTraitConfig при create не выполнялся —
        //  бот «не выбрал ни одного таланта» и стоит без способностей; пересоздание не нужно)
        static bool HandleBotTalentsCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots talents <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SendSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
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

            std::string const guidStr = std::to_string(bot->GetGUID().GetCounter());
            QueryResult qr = CharacterDatabase.Query(
                ("SELECT traitConfigId, COUNT(*) FROM character_trait_entry WHERE guid = " + guidStr
                 + " GROUP BY traitConfigId ORDER BY COUNT(*) DESC LIMIT 1").c_str());
            if (!qr)
            {
                handler->SendSysMessage("В character_trait_entry нет ни одного конфига для guid=" + guidStr
                    + " — конфиг талантов не записывался: пересоздай бота (новый create пишет конфиг сам).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Field* f = qr->Fetch();
            int32 const cfgId = f[0].GetInt32();
            uint32 const nodes = f[1].GetUInt32();

            if (!bot->GetTraitConfig(cfgId))
            {
                handler->SendSysMessage(fmt::format(
                    "Конфиг {} не загружен в память персонажа при входе (_LoadTraits) — перезайди ботом и повтори.", cfgId));
                handler->SetSentErrorMessage(true);
                return false;
            }

            bot->ApplyTraitConfig(cfgId, true);     // LearnSpell каждой ноды (HasSpell-идемпотентно)
            bot->SaveToDB(false);                   // сохранить флаги талант-спеллов (паттерн RemoveBot)
            ai->RebuildKnowledge();                 // новые спеллы → знания AI/sweep

            // Диагностика «применил, но талантов нет»: при входе бота _LoadTraits мог снести
            // записи конфига (ValidateConfig != Ok → Entries.clear) — тогда Apply идёт по
            // усечённому списку памяти, а не по 142 нодам из БД.
            uint32 const memEntries = bot->GetTraitConfig(cfgId)->Entries.size();
            handler->SendSysMessage(fmt::format(
                "ApplyTraitConfig {} применён: нод в character_trait_entry (БД) = {}, entries в ПАМЯТИ персонажа = {}{}",
                cfgId, nodes, memEntries,
                memEntries + 3 < nodes
                    ? " — валидатор _LoadTraits снёс битые ноды при входе (каскад prerequisites/conditions); спеллы Apply учатся только по оставшимся → пересоздай бота свежим create"
                    : ""));
            handler->SendSysMessage("Проверка: .playerbots book " + botName
                + " ; SQL character_spell по guid вырастет; UI талантов показывает записи конфига.");
            return true;
        }

        // ==== Команды, ЗАМЕНЯЮЩИЕ их .mbot/.mb (движок MidnightBotAI под нашим именем) ====

        // Общий путь: наш онлайн-бот → однократная адоптация в их ростер → их метод.
        // QA-боты и боты в режиме манекена не адоптируются: QA остаётся на нашем AI.
        static Player* ResolveMbotBot(ChatHandler* handler, std::string const& botName, std::string& err)
        {
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                err = "нет онлайн — " + sPlayerbotMgr.DiagnoseBot(botName);
                return nullptr;
            }
            Player* bot = ai->GetBot();
            if (!bot)
            {
                err = "бот оффлайн";
                return nullptr;
            }
            if (sMidnightBotMgr->IsBotActive(botName))
                return bot;
            if (bot->GetSession() && sPlayerbotMgr.IsQAAccount(bot->GetSession()->GetAccountId()))
            {
                err = "это QA-бот: их движок не подключается (используй наш .playerbots dummy/sweep)";
                return nullptr;
            }
            if (ai->IsDummyMode())
            {
                err = "бот в режиме манекена — сначала .playerbots dummy stop";
                return nullptr;
            }
            ObjectGuid owner = handler->GetPlayer() ? handler->GetPlayer()->GetGUID() : ObjectGuid();
            if (!sMidnightBotMgr->AdoptBot(bot, owner, err))
                return nullptr;
            return bot;
        }

        // .playerbots stats — метры урона/хила (аналог .mbot stats; работает и из консоли)
        static bool HandleBotStatsCommand(ChatHandler* handler)
        {
            handler->SendSysMessage("[MC] --- Bot damage/heal (60s) ---");
            std::vector<std::string> lines = sMidnightBotMgr->DamageReportLines();
            if (lines.empty())
            {
                handler->SendSysMessage("[MC] no active bots");
                return true;
            }
            for (std::string const& line : lines)
                handler->SendSysMessage(line);
            return true;
        }

        // .playerbots role <имя> <tank|healer|dps|none> — роль их движка (аналог .mbot role)
        static bool HandleBotRoleCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, roleText;
            is >> botName >> roleText;
            if (botName.empty() || roleText.empty())
            {
                handler->SendSysMessage("Использование: .playerbots role <имя> <tank|healer|dps|none>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string const rt = LowerStr(roleText);
            BotRole role = BotRole::None;
            if (rt == "tank") role = BotRole::Tank;
            else if (rt == "heal" || rt == "healer") role = BotRole::Healer;
            else if (rt == "dps" || rt == "dd" || rt == "damage") role = BotRole::Dps;
            else if (rt != "none")
            {
                handler->PSendSysMessage("Роль '%s' неизвестна (tank/healer/dps/none).", roleText.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string err;
            if (!ResolveMbotBot(handler, botName, err))
            {
                handler->PSendSysMessage("role: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->SetBotRole(botName, role, err))
            {
                handler->PSendSysMessage("role: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s: роль '%s' применена (движок MidnightBotAI).", botName.c_str(), roleText.c_str());
            return true;
        }

        // .playerbots assist <имя> [on|off] — ассист их движка (аналог .mbot assist)
        static bool HandleBotAssistCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, token;
            is >> botName >> token;
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots assist <имя> [on|off]");
                handler->SetSentErrorMessage(true);
                return false;
            }
            bool enable = true;
            std::string const t = LowerStr(token);
            if (t == "off") enable = false;
            else if (!token.empty() && t != "on")
            {
                handler->SendSysMessage("Использование: .playerbots assist <имя> [on|off]");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string err;
            if (!ResolveMbotBot(handler, botName, err))
            {
                handler->PSendSysMessage("assist: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->SetBotAssist(botName, enable, handler->GetPlayer(), err))
            {
                handler->PSendSysMessage("assist: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s: assist %s.", botName.c_str(), enable ? "on" : "off");
            return true;
        }

        // .playerbots attack <имя> — атаковать твою выделенную цель (аналог .mbot attack)
        static bool HandleBotAttackCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: выдели цель и: .playerbots attack <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* player = handler->GetPlayer();
            if (!player)
            {
                handler->SendSysMessage("attack: только из игры (нужна выделенная цель).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Unit* target = player->GetSelectedUnit();
            if (!target)
            {
                handler->SendSysMessage("attack: нет выделенной цели.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string err;
            if (!ResolveMbotBot(handler, botName, err))
            {
                handler->PSendSysMessage("attack: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->BotAttackTarget(botName, player, target, err))
            {
                handler->PSendSysMessage("attack: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s атакует '%s'.", botName.c_str(), target->GetName());
            return true;
        }

        // .playerbots boost <имя> [ур.] — уровень+гир+таланты их движком (аналог .mbot boost)
        static bool HandleBotBoostCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, levelTok;
            is >> botName >> levelTok;
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots boost <имя> [уровень]");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* player = handler->GetPlayer();
            uint8 targetLevel = (player && player->GetLevel() >= 1) ? uint8(player->GetLevel()) : 10;
            if (!levelTok.empty())
            {
                uint32 lvl = ToU32(levelTok);
                if (!lvl)
                {
                    handler->PSendSysMessage("boost: уровень — число (levelTok '%s').", levelTok.c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                targetLevel = uint8(std::min<uint32>(lvl, 255));
            }
            std::string err;
            if (!ResolveMbotBot(handler, botName, err))
            {
                handler->PSendSysMessage("boost: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->BoostBot(botName, player, targetLevel, err))
            {
                handler->PSendSysMessage("boost: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s: boosted to level %u.", botName.c_str(), uint32(targetLevel));
            return true;
        }

        // .playerbots boostall — все АКТИВНЫЕ их боты (аналог .mbot boostall)
        static bool HandleBotBoostAllCommand(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            uint8 targetLevel = (player && player->GetLevel() >= 1) ? uint8(player->GetLevel()) : 10;
            std::vector<std::string> names = sMidnightBotMgr->ActiveBotNames();
            if (names.empty())
            {
                handler->SendSysMessage("boost: no active bots (сначала роль/stats-команды активируют бота)");
                return true;
            }
            uint32 ok = 0, failed = 0;
            std::string firstErr;
            for (std::string const& botName : names)
            {
                std::string err;
                if (sMidnightBotMgr->BoostBot(botName, player, targetLevel, err))
                    ++ok;
                else
                {
                    ++failed;
                    if (firstErr.empty()) firstErr = err;
                }
            }
            if (failed)
                handler->PSendSysMessage("boost: %u ok, %u failed - %s", ok, failed, firstErr.c_str());
            else
                handler->PSendSysMessage("all: boosted to level %u (%u bots)", uint32(targetLevel), ok);
            return true;
        }

        // .playerbots party <имя>|all — в группу владельца (аналог .mbot party)
        static bool HandleBotPartyCommand(ChatHandler* handler, Tail name)
        {
            Player* player = handler->GetPlayer();
            if (!player)
            {
                handler->SendSysMessage("party: только из игры (боты вступают в твою группу).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string const token{name};
            if (token.empty())
            {
                handler->SendSysMessage("Использование: .playerbots party <имя>|all");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (LowerStr(token) == "all")
            {
                uint32 added = 0, already = 0, failed = 0;
                sMidnightBotMgr->PartyAllBots(player, added, already, failed);
                handler->PSendSysMessage("party all: +%u, уже в группе %u, ошибок %u", added, already, failed);
                return true;
            }
            std::string err;
            if (!ResolveMbotBot(handler, token, err))
            {
                handler->PSendSysMessage("party: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->AddBotToGroup(token, player, err))
            {
                handler->PSendSysMessage("party: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s вступил в твою группу.", token.c_str());
            return true;
        }

        // .playerbots loot [show|method <m>|threshold <q>] — правила добычи (аналог .mbot loot)
        static bool HandleBotLootCommand(ChatHandler* handler, Tail args)
        {
            Player* player = handler->GetPlayer();
            if (!player)
            {
                handler->SendSysMessage("loot: только из игры (правила твоей группы).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::istringstream is{std::string(args)};
            std::string first, second;
            is >> first >> second;
            std::string err;
            if (first.empty() || LowerStr(first) == "show")
            {
                std::string out;
                if (!sMidnightBotMgr->LootShow(player, out, err))
                    handler->PSendSysMessage("loot: %s", err.c_str());
                else
                    handler->PSendSysMessage("%s", out.c_str());
                return true;
            }
            if (LowerStr(first) == "threshold")
            {
                if (second.empty())
                {
                    handler->SendSysMessage("Использование: .playerbots loot threshold <poor|common|uncommon|rare|epic|legendary>");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                if (!sMidnightBotMgr->LootSetThreshold(player, second, err))
                    handler->PSendSysMessage("loot: %s", err.c_str());
                else
                    handler->PSendSysMessage("loot threshold set to %s.", second.c_str());
                return true;
            }
            if (!sMidnightBotMgr->LootSetMethod(player, LowerStr(first), err))
                handler->PSendSysMessage("loot: %s", err.c_str());
            else
                handler->PSendSysMessage("loot method set to %s.", LowerStr(first).c_str());
            return true;
        }

        // .playerbots resurrect <имя> — поднять (аналог .mbot resurrect)
        static bool HandleBotResurrectCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots resurrect <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string err;
            if (!ResolveMbotBot(handler, botName, err))
            {
                handler->PSendSysMessage("resurrect: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (!sMidnightBotMgr->ResurrectBot(botName, err))
            {
                handler->PSendSysMessage("resurrect: %s", err.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->PSendSysMessage("%s: resurrected.", botName.c_str());
            return true;
        }

        // .playerbots learn [имя] — досказать недостающие спеллы роли (аналог .mbot learn)
        static bool HandleBotLearnCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (!botName.empty())
            {
                std::string err;
                if (!ResolveMbotBot(handler, botName, err))
                {
                    handler->PSendSysMessage("learn: %s", err.c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                uint32 learned = sMidnightBotMgr->LearnBotSpells(botName, err);
                if (!err.empty())
                    handler->PSendSysMessage("%s: learn failed - %s", botName.c_str(), err.c_str());
                else
                    handler->PSendSysMessage("%s: learned %u spell(s)", botName.c_str(), learned);
                return true;
            }
            std::vector<std::string> names = sMidnightBotMgr->ActiveBotNames();
            if (names.empty())
            {
                handler->SendSysMessage("all: learned 0 spell(s) - no active bots");
                return true;
            }
            uint32 total = 0;
            for (std::string const& n : names)
            {
                std::string err;
                total += sMidnightBotMgr->LearnBotSpells(n, err);
            }
            handler->PSendSysMessage("all: learned %u spell(s)", total);
            return true;
        }

        // .playerbots create <account> class=paladin [race=N] [side=ally|horde] [gender=m|f] [level=N]
        //                     [spec=dps|heal|tank|имя|ID] [hero=templar[,herald|all]] [item=ID[,ID]] [name=X]
        static bool HandleBotCreateCommand(ChatHandler* handler, Tail args)
        {
            if (args.empty())
            {
                handler->SendSysMessage("Использование: .playerbots create <account> class=паладин|2 [race=N] [side=ally|horde] [gender=m|f] [level=N] [spec=dps|heal|tank|имя|ID] [hero=ветка[,ветка]|all] [item=ID[,ID...]] [name=Имя]");
                handler->SendSysMessage("Пример: .playerbots create 9001 class=paladin level=80 spec=dps hero=templar,herald item=2000");
                handler->SendSysMessage("hero: обе ветки спека паладина — Ret templar,herald | Holy herald,lightsmith | Prot templar,lightsmith (или all = все ветки класса)");
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
                {
                    if (!c.side.empty())
                    {
                        handler->SendSysMessage("race= и side= одновременно нельзя — выбери одно.");
                        handler->SetSentErrorMessage(true);
                        return false;
                    }
                    c.raceId = ToU32(v);
                }
                else if (k == "side")
                {
                    if (c.raceId)
                    {
                        handler->SendSysMessage("race= и side= одновременно нельзя — выбери одно.");
                        handler->SetSentErrorMessage(true);
                        return false;
                    }
                    std::string s = LowerStr(v);
                    if (s == "ally" || s == "alliance" || s == "a" || s == "альянс")
                        c.side = "ally";
                    else if (s == "horde" || s == "h" || s == "орда")
                        c.side = "horde";
                    else
                    {
                        handler->SendSysMessage(fmt::format("side: '{}'. Нужно ally|horde.", v));
                        handler->SetSentErrorMessage(true);
                        return false;
                    }
                }
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
                handler->SendSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
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
                    // Резерв для консоли/QA: ближайший спавн манекена на КАРТЕ бота —
                    // телепортируем бота к нему, выделять цель не нужно.
                    std::string const sql =
                        "SELECT c.id, c.position_x, c.position_y, c.position_z FROM creature c "
                        "JOIN creature_template t ON c.id = t.entry WHERE c.map = " + std::to_string(bot->GetMapId()) +
                        " AND (t.name LIKE '%Dummy%' OR t.name LIKE '%Манекен%' OR t.name LIKE '%Training%') LIMIT 400";
                    if (QueryResult spawnQr = WorldDatabase.Query(sql.c_str()))
                    {
                        float bestD2 = 1e30f;
                        uint32 bestEntry = 0;
                        float bx = 0.f, by = 0.f, bz = 0.f;
                        do
                        {
                            Field* f = spawnQr->Fetch();
                            uint32 e = f[0].GetUInt32();
                            float x = f[1].GetFloat(), y = f[2].GetFloat(), z = f[3].GetFloat();
                            float dx = x - bot->GetPositionX(), dy = y - bot->GetPositionY();
                            float d2 = dx * dx + dy * dy;
                            if (d2 < bestD2)
                            {
                                bestD2 = d2;
                                bestEntry = e;
                                bx = x;
                                by = y;
                                bz = z;
                            }
                        } while (spawnQr->NextRow());

                        if (bestEntry)
                        {
                            bot->TeleportTo(bot->GetMapId(), bx, by, bz + 1.0f, bot->GetOrientation());
                            target = bot->FindNearestCreature(bestEntry, 100.0f);
                            if (target)
                                handler->SendSysMessage(fmt::format(
                                    "Бот телепортирован к манекену ({:.0f} {:.0f} {:.0f} map {}).",
                                    bx, by, bz, bot->GetMapId()));
                        }
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

        // .playerbots hero <имя> <ветка[,ветка]|all>  — дозаучить ветки геро-талантов
        // на УЖЕ существующем боте + пересобрать знания AI (новые спеллы появятся в sweep)
        static bool HandleBotHeroCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{ std::string(args) };
            std::string botName, trees;
            if (!(is >> botName) || !(is >> trees))
            {
                handler->SendSysMessage("Использование: .playerbots hero <имя> <ветка[,ветка]|all>");
                handler->SendSysMessage("Пример: .playerbots hero Xkq herald   — дозаучить вторую ветку спека");
                handler->SetSentErrorMessage(true);
                return false;
            }
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai)
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage(sPlayerbotMgr.LearnHeroTrees(ai->GetBot(), trees));
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

        // .playerbots delete <имя> [force] — ПОЛНАЯ чистка слота персонажа на аккаунте:
        // если бот в мире — выкидываем (логаут), затем удаляем все строки персонажа по guid
        // во ВСЕХ таблицах characters-БД (включая сам characters) + запись из кэша имён.
        // Аккаунт НЕ трогаем. Без force — только бот-аккаунты (QA/FreeAccounts/онлайн-ботов).
        static bool HandleBotDeleteCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, token;
            bool force = false;
            while (is >> token)
            {
                if (token == "force" && botName.empty())
                    force = true;
                else if (botName.empty())
                    botName = token;
                else
                {
                    handler->SendSysMessage("Использование: .playerbots delete <имя> [force]");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots delete <имя> [force] — удалить персонажа-бота и полностью вычистить его слот (все таблицы по guid). force — если персонаж вне бот-диапазонов.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
            if (guid.IsEmpty())
            {
                handler->SendSysMessage(fmt::format("Персонаж '{}' не найден в кэше имён (нет в characters?) — нечего удалять.", botName));
                handler->SetSentErrorMessage(true);
                return false;
            }
            uint32 const accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
            uint32 const guidLow = guid.GetCounter();

            bool const inWorld = sPlayerbotMgr.GetBotAI(botName) != nullptr;
            bool const isBot = inWorld || sPlayerbotMgr.IsBotAccount(accountId);
            if (!isBot && !force)
            {
                handler->SendSysMessage(fmt::format(
                    "Аккаунт {} вне бот-диапазонов (QA/FreeAccounts) и персонаж не добавлен как бот — это может быть живой игрок. Если уверен: .playerbots delete {} force",
                    accountId, botName));
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (inWorld && sPlayerbotMgr.RemoveBot(botName) != PlayerbotMgr::Status::OK)
            {
                handler->SendSysMessage("Бот в мире, но RemoveBot отказал — прерываю (смотри Server.log).");
                handler->SetSentErrorMessage(true);
                return false;
            }

            // Полная чистка: все таблицы текущей (characters) БД с колонкой guid,
            // затем сам characters, затем повторный проход — на случай FK-ребёнка,
            // не давшего удалить characters с первого раза.
            std::string const g = std::to_string(guidLow);
            auto wipeTables = [&g]()
            {
                uint32 n = 0;
                if (QueryResult tabs = CharacterDatabase.Query(
                    "SELECT TABLE_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()"
                    " AND COLUMN_NAME = 'guid' AND TABLE_NAME <> 'characters'"))
                {
                    do
                    {
                        if (Field* tf = tabs->Fetch())
                        {
                            std::string const t = tf[0].GetString();
                            CharacterDatabase.DirectExecute(("DELETE FROM `" + t + "` WHERE guid = " + g).c_str());
                            ++n;
                        }
                    } while (tabs->NextRow());
                }
                return n;
            };
            uint32 const tables = wipeTables();
            CharacterDatabase.DirectExecute(("DELETE FROM characters WHERE guid = " + g).c_str());
            wipeTables();   // страховка от FK-хвостов

            uint64 left = 1;
            if (QueryResult chk = CharacterDatabase.Query(("SELECT COUNT(*) FROM characters WHERE guid = " + g).c_str()))
                if (Field* f = chk->Fetch())
                    left = f->GetUInt64();

            sCharacterCache->DeleteCharacterCacheEntry(guid, botName);

            if (left == 0)
            {
                handler->SendSysMessage(fmt::format(
                    "Персонаж {} (guid {}, аккаунт {}) удалён: таблиц очищено {}, в characters остаток 0 — СЛОТ СВОБОДЕН. Аккаунт {} оставлен.{}",
                    botName, guidLow, accountId, tables, accountId,
                    inWorld ? " Бот был в мире — выкинут перед чисткой." : ""));
                TC_LOG_INFO("playerbots", "DeleteCharacter: {} (guid {}, account {}) — слот очищён (tables {})",
                    botName, guidLow, accountId, tables);
                return true;
            }
            handler->SendSysMessage(fmt::format(
                "Удаление НЕ завершено: guid {} всё ещё в characters (строк: {}). Смотри Server.log (Unhandled errno → FK).",
                guidLow, left));
            handler->SetSentErrorMessage(true);
            return false;
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

    // Вход в мир (PlayerScript::OnLogin шлётся для всех сессий, в т.ч. фейковых
    // бот-сессий без сокета) — здесь подцепляем AI-обёртку бота.
    // Без этого вызова entry.ai оставался пустым: .playerbots list имя показывал,
    // а followme/dummy/equip/hero отвечали «Бот с таким именем не онлайн».
    class playerbots_login_script : public PlayerScript
    {
    public:
        playerbots_login_script() : PlayerScript("playerbots_login_script") { }

        void OnLogin(Player* player, bool /*firstLogin*/) override
        {
            if (player && player->GetSession() && player->GetSession()->IsPlayerBot())
                sPlayerbotMgr.HandlePlayerBotLoggedIn(player);
        }
    };

// MidnightBotAI (LrdPsychoChains WOW-HUB, GPLv3) — их .mbot/.mb команды + менеджер,
// встроены в наш модуль СЛИЯНИЕМ: обе системы команд работают одновременно.
void AddSC_midnight_bot_ai();

void AddSC_playerbots()
{
    AddSC_playerbots_dummylog();                // UnitScript+PlayerScript: урон/хил/касты в лог
    static_cast<void>( new playerbots_commandscript() );
    new playerbots_worldscript();
    new playerbots_login_script();
    AddSC_midnight_bot_ai();                    // .mbot/.mb: create/spawn/roles/stats/loot/boost...
}
