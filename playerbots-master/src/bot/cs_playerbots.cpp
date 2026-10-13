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
#include "DBCEnums.h"          // TraitConfigType/TraitCombatConfigFlags — copytalents
#include "DummyLog.h"
#include "Language.h"
#include "MidnightBotMgr.h"    // их движок: команды-замены .mbot/.mb (слияние, docs/09)
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "TraitPacketsCommon.h" // WorldPackets::Traits::TraitConfig/Entry — copytalents
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
                { "add",         HandleRosterAddCommand,         static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "remove",      HandleRosterRemoveCommand,      static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "sync",        HandleRosterSyncCommand,        static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
                { "show",        HandleRosterShowCommand,        static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
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
                { "copytalents", HandleBotCopyTalentsCommand,   static_cast<TrinityStrings>(0), rbac::RBAC_PERM_COMMAND_RESET_TALENTS, Console::Yes },
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
            PlayerbotMgr::Status const st = sPlayerbotMgr.AddBot(botName, handler->GetPlayer(), SEC_PLAYER);
            if (st != PlayerbotMgr::Status::OK)
            {
                // раньше на любой отказ печаталось «Не удалось добавить бота.» без причины
                switch (st)
                {
                    case PlayerbotMgr::Status::UnknownName:
                        handler->SendSysMessage("Персонаж '" + botName + "' не найден в characters (или кэш имён ещё не поднят — подожди/перезайди).");
                        break;
                    case PlayerbotMgr::Status::AlreadyOnline:
                        handler->SendSysMessage("Бот '" + botName + "' уже онлайн — ничего не делаю.");
                        break;
                    case PlayerbotMgr::Status::NotABot:
                        handler->SendSysMessage("'" + botName + "' уже в мире, но это ОБЫЧНЫЙ игрок, не бот — добавление отклонено (имя занято).");
                        break;
                    case PlayerbotMgr::Status::NoFreeAccount:
                        handler->SendSysMessage("Нет свободного аккаунта в резервном диапазоне Playerbots.FreeAccountsStart..FreeAccountsEnd.");
                        break;
                    case PlayerbotMgr::Status::ServerError:
                        handler->SendSysMessage("Отказ сервера: Playerbots.Enabled=0, достигнут Playerbots.MaxCount или не создалась фейковая сессия — смотри Server.log.");
                        break;
                    default:
                        handler->SendSysMessage("Не удалось добавить бота (неизвестная причина).");
                        break;
                }
                handler->SendSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
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

            bot->ApplyTraitConfig(cfgId, true);     // LearnSpell по entries ПАМЯТИ (HasSpell-идемпотентно)

            // ЛЕЧЕНИЕ «применил, но талантов нет»: при входе бота _LoadTraits мог снести
            // записи конфига из памяти (ValidateConfig != Ok → Entries.clear) — тогда
            // ApplyTraitConfig обучил только остатки. Проходим ВСЕ записи
            // character_trait_entry напрямую: ApplyTraitEntry (public) → LearnSpell
            // каждой ноды МИМО валидатора памяти (hero-ветки и каскад-ноды тоже).
            uint32 direct = 0;
            if (QueryResult all = CharacterDatabase.Query(
                ("SELECT traitNodeEntryId, rank FROM character_trait_entry WHERE guid = " + guidStr
                 + " AND traitConfigId = " + std::to_string(cfgId)).c_str()))
            {
                do
                {
                    Field* f = all->Fetch();
                    bot->ApplyTraitEntry(f[0].GetInt32(), f[1].GetInt32(), 0, true);
                    ++direct;
                } while (all->NextRow());
            }

            bot->SaveToDB(false);                   // сохранить флаги талант-спеллов (паттерн RemoveBot)
            ai->RebuildKnowledge();                 // новые спеллы → знания AI/sweep

            uint32 const memEntries = bot->GetTraitConfig(cfgId)->Entries.size();
            handler->SendSysMessage(fmt::format(
                "Таланты применены: ApplyTraitConfig {} (нод в БД = {}, entries в ПАМЯТИ = {}), прямой проход по БД — {} нод → LearnSpell; знание AI пересобрано.{}",
                cfgId, nodes, memEntries, direct,
                memEntries + 3 < nodes
                    ? " [примечание: валидатор _LoadTraits снёс записи из памяти при входе — UI талантов может показывать усечённый конфиг, но спеллы выучены прямым проходом]"
                    : ""));
            handler->SendSysMessage("Проверка: .playerbots book " + botName
                + " ; спеллбук должен пополниться; SQL character_spell по guid вырастет.");
            return true;
        }

        // .playerbots copytalents <бот> [источник] — «скопируй МОИ таланты на бота».
        // Источник: из чата по умолчанию вызывающий игрок, иначе имя (должен быть онлайн).
        // Берётся combat-конфиг источника из БД (бит ActiveForSpec=0x1 предпочтительнее)
        // со всеми его записями character_trait_entry; старые конфиги бота того же спека
        // удаляются, копия создаётся CreateTraitConfig (память + granted-ноды), ставится
        // активной, спеллы учатся ApplyTraitConfig И прямым проходом ApplyTraitEntry
        // (мимо CanApplyTraitNode — иначе валидатор отбракует часть нод и получится
        // «скопировал, а талантов нет»), затем SaveToDB (_SaveTraits + character_spell).
        // Выбор hero-ветки переносится сам: это SubTreeSelection-нода внутри Entries.
        static bool HandleBotCopyTalentsCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, srcName;
            if (!(is >> botName))
            {
                handler->SendSysMessage("Использование: .playerbots copytalents <бот> [источник] — источник: игрок с талантами (из чата по умолчанию ты сам; из консоли имя обязательно).");
                handler->SetSentErrorMessage(true);
                return false;
            }
            is >> srcName;

            // --- источник талантов (живой игрок) ---
            Player* src = srcName.empty() ? handler->GetPlayer()
                                          : ObjectAccessor::FindConnectedPlayerByName(srcName);
            if (!src)
            {
                if (srcName.empty())
                    handler->SendSysMessage("Источник не определён: из консоли укажи имя — .playerbots copytalents <бот> <игрок>.");
                else
                    handler->SendSysMessage("Источник '" + srcName + "' не найден онлайн — персонаж должен быть в мире.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            // --- бот (обязательно онлайн) ---
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
            if (bot == src)
            {
                handler->SendSysMessage("Источник и бот — один и тот же персонаж.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (src->GetClass() != bot->GetClass())
            {
                handler->SendSysMessage(fmt::format("Класс источника ({}) != класс бота ({}) — таланты несовместимы.",
                    uint32(src->GetClass()), uint32(bot->GetClass())));
                handler->SetSentErrorMessage(true);
                return false;
            }

            // --- combat-конфиг источника из БД (type=1 Combat) ---
            std::string const srcG = std::to_string(src->GetGUID().GetCounter());
            QueryResult cfgQ = CharacterDatabase.Query(
                ("SELECT traitConfigId, chrSpecializationId, combatConfigFlags, localIdentifier"
                 " FROM character_trait_config WHERE guid = " + srcG + " AND type = 1"
                 " ORDER BY (combatConfigFlags & 1) DESC, traitConfigId DESC LIMIT 1").c_str());
            if (!cfgQ)
            {
                handler->SendSysMessage("У источника нет combat-конфига в character_trait_config — нечего копировать.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            Field* cf = cfgQ->Fetch();
            int32 const srcCfgId = cf[0].GetInt32();
            int32 const specId   = cf[1].GetInt32();
            int32 const flags    = cf[2].GetInt32();
            int32 const localId  = cf[3].GetInt32();

            QueryResult entQ = CharacterDatabase.Query(
                ("SELECT traitNodeId, traitNodeEntryId, rank FROM character_trait_entry"
                 " WHERE guid = " + srcG + " AND traitConfigId = " + std::to_string(srcCfgId)).c_str());
            if (!entQ)
            {
                handler->SendSysMessage(fmt::format("Конфиг {} источника пуст — в character_trait_entry нет записей.", srcCfgId));
                handler->SetSentErrorMessage(true);
                return false;
            }

            WorldPackets::Traits::TraitConfig copy;
            copy.Type = TraitConfigType::Combat;
            copy.ChrSpecializationID = specId;
            copy.CombatConfigFlags = TraitCombatConfigFlags(flags) | TraitCombatConfigFlags::ActiveForSpec;
            copy.LocalIdentifier = localId;
            copy.Name = ("Copy " + src->GetName()).c_str();
            uint32 copied = 0;
            do
            {
                Field* f = entQ->Fetch();
                WorldPackets::Traits::TraitEntry& e = copy.Entries.emplace_back();
                e.TraitNodeID = f[0].GetInt32();
                e.TraitNodeEntryID = f[1].GetInt32();
                e.Rank = f[2].GetInt32();
                ++copied;
            } while (entQ->NextRow());

            // --- старые конфиги бота ЭТОГО спека: чистим БД (иначе дубль/конфликт при входе) ---
            std::string const botG = std::to_string(bot->GetGUID().GetCounter());
            uint32 removed = 0;
            if (QueryResult oldQ = CharacterDatabase.Query(
                ("SELECT traitConfigId FROM character_trait_config WHERE guid = " + botG
                 + " AND type = 1 AND chrSpecializationId = " + std::to_string(specId)).c_str()))
            {
                std::vector<int32> oldIds;
                do { oldIds.push_back(oldQ->Fetch()[0].GetInt32()); } while (oldQ->NextRow());
                for (int32 oldId : oldIds)
                {
                    CharacterDatabase.DirectExecute(("DELETE FROM character_trait_entry WHERE guid = " + botG
                        + " AND traitConfigId = " + std::to_string(oldId)).c_str());
                    CharacterDatabase.DirectExecute(("DELETE FROM character_trait_config WHERE guid = " + botG
                        + " AND traitConfigId = " + std::to_string(oldId)).c_str());
                    ++removed;
                }
            }

            // --- копия → память бота ---
            bot->CreateTraitConfig(copy);                    // новый ID + granted-ноды + PLAYERSPELL_CHANGED
            bot->SetActiveCombatTraitConfigID(copy.ID);      // активный конфиг в памяти (UI сразу)
            bot->ApplyTraitConfig(copy.ID, true);            // LearnSpell по CanApplyTraitNode

            // прямой проход МИМО валидатора: добирает ноды, которые CanApplyTraitNode
            // отбраковал (бюджет/пререквизиты/hero-ветка)
            uint32 direct = 0;
            if (UF::TraitConfig const* mem = bot->GetTraitConfig(copy.ID))
            {
                for (UF::TraitEntry const& me : mem->Entries)
                {
                    bot->ApplyTraitEntry(me.TraitNodeEntryID, me.Rank, me.GrantedRanks, true);
                    ++direct;
                }
            }

            bot->SaveToDB(false);                            // _SaveTraits: config+entries; character_spell
            ai->RebuildKnowledge();                          // знания AI/ротации/sweep

            std::string note;
            if (int32(bot->GetPrimarySpecialization()) != specId)
                note += fmt::format(" [спек конфига {} != активный спек бота {} — UI не покажет конфиг до переключения спека]",
                    specId, int32(bot->GetPrimarySpecialization()));
            if (src->GetLevel() != bot->GetLevel())
                note += fmt::format(" [уровни разные: источник {}, бот {} — часть нод может быть вне бюджета]",
                    uint32(src->GetLevel()), uint32(bot->GetLevel()));

            handler->SendSysMessage(fmt::format(
                "Таланты скопированы: {} (конфиг {}, спек {}) -> бот {}: нод {}, старых конфигов бота удалено {}, новый конфиг {}, LearnSpell по {} записям памяти.{}",
                src->GetName(), srcCfgId, specId, botName, copied, removed, copy.ID, direct, note));
            handler->SendSysMessage("Проверка: .playerbots book " + botName + " ; UI талантов бота — после его перелогина.");
            TC_LOG_INFO("playerbots", "CopyTalents: {} cfg {} spec {} nodes {} -> bot {} cfg {} (removed {}, direct {})",
                src->GetName(), srcCfgId, specId, copied, botName, copy.ID, removed, direct);
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

            // playerbots_rotation живёт в WORLD-БД, поэтому под wipeTables
            // (чистит только текущую characters-БД) не попадает — убираем явно,
            // иначе LoadRoster/RotateRoster вечно пытались бы поднять удалённого бота.
            {
                std::string rosterErr;
                sPlayerbotMgr.RosterRemove(guidLow, rosterErr);
            }

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
            // состав пуст → StartRoster «пройдёт», но никого не поднимет: говорим сразу,
            // чем наполнить (раньше это выяснялось только по ERROR в Server.log)
            std::vector<std::string> const rows = sPlayerbotMgr.RosterList();
            if (rows.empty())
            {
                handler->SendSysMessage("playerbots_rotation пуста: сначала .playerbots roster sync (наполнит из characters по бот-аккаунтам) или создай ботов — create пишет в состав сам.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            sPlayerbotMgr.StartRoster();
            if (sPlayerbotMgr.IsRosterRunning())
            {
                handler->SendSysMessage(fmt::format(
                    "Состав запущен: в playerbots_rotation {} строк — поднимаются первые по rotation_id, дальше ротация каждые {} мин.",
                    rows.size(), sPlayerbotMgr.GetRosterRotationMinutes()));
                return true;
            }
            handler->SendSysMessage("Состав не запустился: Playerbots.Enabled=0 или смотри Server.log.");
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
            // раньше печаталось «Ротация состава: каждые минут.» — без числа
            handler->SendSysMessage(fmt::format("Ротация состава: каждые {} мин (действует до рестарта — в конфиг не пишется).",
                sPlayerbotMgr.GetRosterRotationMinutes()));
            return true;
        }

        // .playerbots roster add <имя> — добавить персонажа в состав вручную
        static bool HandleRosterAddCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots roster add <имя> — добавить персонажа в состав ротации.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
            if (!guid)
            {
                handler->SendSysMessage("Персонаж '" + botName + "' не найден в characters (или кэш имён ещё не поднят).");
                handler->SetSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
                handler->SetSentErrorMessage(true);
                return false;
            }
            uint8 classId = 0;
            uint32 accountId = 0;
            if (QueryResult qr = CharacterDatabase.Query(
                ("SELECT class, account FROM characters WHERE guid = " + std::to_string(guid.GetCounter())).c_str()))
            {
                Field* f = qr->Fetch();
                classId = f[0].GetUInt8();
                accountId = f[1].GetUInt32();
            }
            std::string err;
            if (!sPlayerbotMgr.RosterAdd(uint32(guid.GetCounter()), botName, classId, err))
            {
                handler->SendSysMessage("Не добавил в состав: " + err);
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage(fmt::format("В составе: {} (guid {}, class {}, аккаунт {}). Очередь ротации = порядок rotation_id.",
                botName, guid.GetCounter(), uint32(classId), accountId));
            return true;
        }

        // .playerbots roster remove <имя> — убрать из состава (бот в мире остаётся)
        static bool HandleRosterRemoveCommand(ChatHandler* handler, Tail name)
        {
            std::string const botName{name};
            if (botName.empty())
            {
                handler->SendSysMessage("Использование: .playerbots roster remove <имя>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(botName);
            if (!guid)
            {
                handler->SendSysMessage("Персонаж '" + botName + "' не найден в characters.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            std::string err;
            if (!sPlayerbotMgr.RosterRemove(uint32(guid.GetCounter()), err))
            {
                handler->SendSysMessage("Не убрал из состава: " + err);
                handler->SetSentErrorMessage(true);
                return false;
            }
            handler->SendSysMessage(botName + " убран из состава ротации (если бот в мире — останется; выход: .playerbots remove " + botName + ").");
            return true;
        }

        // .playerbots roster sync — наполнить состав из characters (бот-аккаунты)
        static bool HandleRosterSyncCommand(ChatHandler* handler)
        {
            std::string err;
            uint32 const added = sPlayerbotMgr.RosterSyncFromCharacters(err);
            std::vector<std::string> const now = sPlayerbotMgr.RosterList();
            handler->SendSysMessage(fmt::format(
                "Состав синхронизирован из characters: добавлено/обновлено {} строк, всего в playerbots_rotation {}.{}",
                added, now.size(), err.empty() ? "" : " " + err));
            if (now.empty())
                handler->SendSysMessage("Состав пуст: создай ботов (.playerbots create ... — пишет в состав сам) или проверь диапазоны Playerbots.FreeAccountsStart/End и QAAccountsStart/End.");
            return true;
        }

        // .playerbots roster show — что сейчас в составе
        static bool HandleRosterShowCommand(ChatHandler* handler)
        {
            std::vector<std::string> const rows = sPlayerbotMgr.RosterList();
            if (rows.empty())
            {
                handler->SendSysMessage("playerbots_rotation пуста — заполни: .playerbots roster sync (или создай бота: create пишет в состав сам).");
                return true;
            }
            handler->SendSysMessage(fmt::format("Состав ротации ({} строк):", rows.size()));
            for (std::string const& r : rows)
                handler->SendSysMessage("  " + r);
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
