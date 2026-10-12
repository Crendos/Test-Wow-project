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
#include "DBCEnums.h"          // TraitConfigType / TraitCombatConfigFlags — copytalents
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

        // .playerbots copytalents <бот> [источник] — скопировать trait-конфиг талантов
        // с игрока (по умолчанию — вызывающий; в консоли источник обязателен) на бота.
        // Старый конфиг бота удаляется из БД, копия создаётся через CreateTraitConfig
        // (память + granted), ApplyTraitConfig учит спеллы, SaveToDB сохраняет.
        static bool HandleBotCopyTalentsCommand(ChatHandler* handler, Tail args)
        {
            std::istringstream is{std::string(args)};
            std::string botName, srcName;
            if (!(is >> botName))
            {
                handler->SendSysMessage("Использование: .playerbots copytalents <бот> [источник] — источник: игрок с талантами (из чата по умолчанию ты сам); в консоли укажи имя явно.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            is >> srcName;

            // --- источник талантов (должен быть в мире) ---
            Player* src = handler->GetPlayer();
            if (!src || !srcName.empty())
            {
                if (srcName.empty())
                {
                    handler->SendSysMessage("Из консоли укажи источник: .playerbots copytalents <бот> <имя_игрока>");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                ObjectGuid srcGuid = sCharacterCache->GetCharacterGuidByName(srcName);
                src = srcGuid.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(srcGuid);
                if (!src)
                {
                    handler->SendSysMessage("Источник '" + srcName + "' не найден в мире — персонаж должен быть онлайн.");
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }

            // --- бот должен быть онлайн ---
            PlayerbotAI* ai = sPlayerbotMgr.GetBotAI(botName);
            if (!ai || !ai->GetBot())
            {
                handler->SendSysMessage("Бот с таким именем не онлайн.");
                handler->SendSysMessage(sPlayerbotMgr.DiagnoseBot(botName));
                handler->SetSentErrorMessage(true);
                return false;
            }
            Player* bot = ai->GetBot();

            if (bot == src)
            {
                handler->SendSysMessage("Источник и бот — один и тот же персонаж.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (src->GetClass() != bot->GetClass())
            {
                handler->SendSysMessage(fmt::format(
                    "Класс источника ({}) не совпадает с классом бота ({}) — таланты скопировать нельзя.",
                    uint32(src->GetClass()), uint32(bot->GetClass())));
                handler->SetSentErrorMessage(true);
                return false;
            }

            // --- активный combat-конфиг источника из БД (бит ActiveForSpec=0x1 предпочтительнее) ---
            std::string const srcG = std::to_string(src->GetGUID().GetCounter());
            QueryResult cfgQ = CharacterDatabase.Query(
                ("SELECT traitConfigId, chrSpecializationId, combatConfigFlags, localIdentifier"
                 " FROM character_trait_config WHERE guid = " + srcG + " AND type = 1"
                 " ORDER BY (combatConfigFlags & 1) DESC, traitConfigId DESC LIMIT 1").c_str());
            if (!cfgQ)
            {
                handler->SendSysMessage("У источника нет combat-конфига талантов в character_trait_config.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            int32 srcCfgId, specId, flags, localId;
            {
                Field* cf = cfgQ->Fetch();
                srcCfgId = cf[0].GetInt32();
                specId   = cf[1].GetInt32();
                flags    = cf[2].GetInt32();
                localId  = cf[3].GetInt32();
            }

            QueryResult entQ = CharacterDatabase.Query(
                ("SELECT traitNodeId, traitNodeEntryId, rank FROM character_trait_entry"
                 " WHERE guid = " + srcG + " AND traitConfigId = " + std::to_string(srcCfgId)).c_str());
            if (!entQ)
            {
                handler->SendSysMessage(fmt::format("Конфиг {} источника пуст (нет записей character_trait_entry).", srcCfgId));
                handler->SetSentErrorMessage(true);
                return false;
            }

            WorldPackets::Traits::TraitConfig copy;
            copy.Type = TraitConfigType::Combat;
            copy.ChrSpecializationID = specId;
            copy.CombatConfigFlags = TraitCombatConfigFlags(flags | int32(TraitCombatConfigFlags::ActiveForSpec));
            copy.LocalIdentifier = localId;
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

            // --- старый конфиг бота: удаляем из БД (в памяти остаётся до перелогина — безвредно) ---
            std::string const botG = std::to_string(bot->GetGUID().GetCounter());
            CharacterDatabase.DirectExecute(("DELETE FROM character_trait_entry WHERE guid = " + botG).c_str());
            CharacterDatabase.DirectExecute(("DELETE FROM character_trait_config WHERE guid = " + botG).c_str());

            // --- копия → память бота, применение, сохранение ---
            bot->CreateTraitConfig(copy);            // новый ID + granted-ноды + PLAYERSPELL_CHANGED
            bot->ApplyTraitConfig(copy.ID, true);    // LearnSpell всех нод копии (память свежая)
            bot->SaveToDB(false);                    // _SaveTraits: config+entries+character_spell
            ai->RebuildKnowledge();                  // знания AI/ротации/sweep

            std::string warn;
            if (int32(bot->GetPrimarySpecialization()) != specId)
                warn += fmt::format(" [внимание: спек конфига {} != активный спек бота {} — UI может не показать конфиг до переключения спека]",
                    specId, int32(bot->GetPrimarySpecialization()));
            if (src->GetLevel() != bot->GetLevel())
                warn += fmt::format(" [уровни разные: источник {}, бот {} — при следующем входе валидатор может снёс лишние ноды по бюджету; лечится .playerbots talents]",
                    src->GetLevel(), bot->GetLevel());

            handler->SendSysMessage(fmt::format(
                "Скопировано {} нод из конфига {} ({}, спек {}) → новый конфиг {} бота {}; ApplyTraitConfig учёл спеллы, SaveToDB сохранил, знание AI пересобрано.{}",
                copied, srcCfgId, src->GetName(), specId, copy.ID, botName, warn));
            handler->SendSysMessage("Проверка: .playerbots book " + botName + " ; UI талантов бота после перелогина.");
            TC_LOG_INFO("playerbots", "CopyTalents: {} (cfg {}, spec {}, nodes {}) -> bot {} (cfg {})",
                src->GetName(), srcCfgId, specId, copied, botName, copy.ID);
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
                    ha