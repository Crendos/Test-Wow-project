-- PalDump: /paldump — выгрузка ID всех способностей (книга заклинаний) и талантов.
-- После /reload файл лежит: WTF\Account\<аккаунт>\SavedVariables\PalDump.lua
-- Совместим и со старым, и с новым API (C_SpellBook / C_Traits).

local function PlayerBank()
    if Enum and Enum.SpellBookSpellBank and Enum.SpellBookSpellBank.Player then
        return Enum.SpellBookSpellBank.Player
    end
    return "player"
end

local function ItemInfo(idx, b)
    if C_SpellBook and C_SpellBook.GetSpellBookItemInfo then
        local info = C_SpellBook.GetSpellBookItemInfo(idx, b)
        if info then return info.name, (info.spellID or info.id) end
        return
    end
    local info = GetSpellBookItemInfo(idx, b == "player" and BOOKTYPE_SPELL or b)
    if info then return info.name, info.id end
end

local function Tabs()
    local res = {}
    local b = PlayerBank()
    if C_SpellBook and C_SpellBook.GetNumSpellBookSkillLines then
        local n = C_SpellBook.GetNumSpellBookSkillLines(b) or 0
        for i = 1, n do
            -- name, icon, itemIndexOffset, numSpells, ...
            local t = { C_SpellBook.GetSpellBookSkillLineInfo(b, i) }
            local offset, count = t[3], t[4]
            if offset and count and count > 0 then
                res[#res + 1] = { offset = offset, count = count, name = tostring(t[1]) }
            end
        end
    elseif GetNumSpellTabs then
        local n = GetNumSpellTabs() or 0
        for i = 1, n do
            local name, _, offset, numSpells = GetSpellTabInfo(i)
            if offset and numSpells and numSpells > 0 then
                res[#res + 1] = { offset = offset, count = numSpells, name = tostring(name) }
            end
        end
    end
    return res
end

local function DumpBook()
    local b = PlayerBank()
    local out, skipped = {}, 0
    for _, tab in ipairs(Tabs()) do
        for i = tab.offset + 1, tab.offset + tab.count do
            local name, id = ItemInfo(i, b)
            if name and id then
                out[#out + 1] = string.format("%s = %d", name, id)
            else
                skipped = skipped + 1
            end
        end
    end
    return out, skipped
end

local function DumpTalents()
    local out = {}
    if not (C_ClassTalents and C_ClassTalents.GetActiveConfigID and C_Traits) then return out end
    local okC, configID = pcall(C_ClassTalents.GetActiveConfigID)
    if not okC or not configID then return out end
    local okT, treeInfo = pcall(C_Traits.GetTreeInfo, configID)
    local nodes = okT and treeInfo and treeInfo.nodes
    if type(nodes) ~= "table" then return out end
    local nameOf = function(spellID)
        if C_Spell and C_Spell.GetSpellInfo then
            local s = C_Spell.GetSpellInfo(spellID)
            return s and s.name
        end
        return select(1, GetSpellInfo(spellID))
    end
    for _, nodeID in ipairs(nodes) do
        if type(nodeID) == "number" then
            local okN, nodeInfo = pcall(C_Traits.GetNodeInfo, configID, nodeID)
            local entryIDs = okN and nodeInfo and nodeInfo.entryIDs
            if type(entryIDs) == "table" then
                for _, entryID in ipairs(entryIDs) do
                    local okE, entryInfo = pcall(C_Traits.GetEntryInfo, configID, entryID)
                    local defID = okE and entryInfo and entryInfo.definitionID
                    if defID then
                        local okD, defInfo = pcall(C_Traits.GetDefinitionInfo, defID)
                        if okD and defInfo and defInfo.spellID then
                            out[#out + 1] = string.format("%s = %d [талант]",
                                defInfo.name or nameOf(defInfo.spellID) or "?", defInfo.spellID)
                        end
                    end
                end
            end
        end
    end
    return out
end

SLASH_PALDUMP1 = "/paldump"
SlashCmdList["PALDUMP"] = function()
    PalDumpDB = PalDumpDB or {}
    local book, skipped = DumpBook()
    local tal = DumpTalents()
    PalDumpDB.book = book
    PalDumpDB.talents = tal
    PalDumpDB.time = date("%Y-%m-%d %H:%M")
    print(("[PalDump] способности: %d (пропущено %d), таланты: %d"):format(#book, skipped, #tal))
    if #tal == 0 then
        print("[PalDump] таланты не выгрузились — снимите их IdTip'ом или пришлите вручную")
    end
    print("[PalDump] теперь введите /reload и пришлите файл:")
    print('  WTF\\Account\\<имя аккаунта>\\SavedVariables\\PalDump.lua')
end
