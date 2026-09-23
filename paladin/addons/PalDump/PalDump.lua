-- PalDump: /paldump — выгрузка ID способностей (книга заклинаний) и талантов.
-- КАЖДАЯ специализация сохраняется ОТДЕЛЬНО и НЕ затирает предыдущие:
-- переключите спеку → /paldump → /reload (и так для Света/Воздаяния/Защиты).
-- После /reload файл лежит: WTF\Account\<аккаунт>\SavedVariables\PalDump.lua
-- Совместим со старым и новым API (C_SpellBook / C_Traits / C_SpecializationInfo).

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
                                defInfo.name or "?", defInfo.spellID)
                        end
                    end
                end
            end
        end
    end
    return out
end

local function SpecInfo()
    local idx
    if C_SpecializationInfo and C_SpecializationInfo.GetSpecialization then
        idx = C_SpecializationInfo.GetSpecialization()
    end
    if not idx and GetSpecialization then idx = GetSpecialization() end
    if not idx then return "unknown", 0 end
    local vals = {}
    if C_SpecializationInfo and C_SpecializationInfo.GetSpecializationInfo then
        local ok, r1, r2, r3, r4, r5, r6 = pcall(C_SpecializationInfo.GetSpecializationInfo, idx)
        if ok then
            if type(r1) == "table" then return tostring(r1.name or "?"), (r1.specID or idx) end
            vals = { r1, r2, r3, r4, r5, r6 }
        end
    end
    if #vals == 0 and GetSpecializationInfo then
        local ok, r1, r2, r3, r4, r5, r6 = pcall(GetSpecializationInfo, idx)
        if ok then
            if type(r1) == "table" then return tostring(r1.name or "?"), (r1.specID or idx) end
            vals = { r1, r2, r3, r4, r5, r6 }
        end
    end
    local name, id = tostring(vals[1] or ("spec" .. idx)), nil
    for i = 1, #vals do
        local v = vals[i]
        if type(v) == "number" and (v == 65 or v == 66 or v == 70) then id = v; break end
    end
    if not id then
        for i = #vals, 1, -1 do
            if type(vals[i]) == "number" and vals[i] > 10 then id = vals[i]; break end
        end
    end
    return name, (id or idx)
end

SLASH_PALDUMP1 = "/paldump"
SlashCmdList["PALDUMP"] = function()
    PalDumpDB = PalDumpDB or {}
    PalDumpDB.specs = PalDumpDB.specs or {}
    local specName, specID = SpecInfo()
    local key = tostring(specID)
    local book, skipped = DumpBook()
    local tal = DumpTalents()
    PalDumpDB.specs[key] = { name = specName, book = book, talents = tal, time = date("%Y-%m-%d %H:%M") }
    print(("[PalDump] %s (%s): способности %d (пропущено %d), талантов %d — СОХРАНЕНО"):format(
        specName, key, #book, skipped, #tal))
    local list = {}
    for k, v in pairs(PalDumpDB.specs) do list[#list + 1] = tostring(v.name) .. "(" .. k .. ")" end
    print("[PalDump] уже в файле: " .. table.concat(list, ", "))
    print("[PalDump] введите /reload, затем при желании переключите другую спеку и повторите /paldump")
    print('  финальный файл: WTF\\Account\\<имя аккаунта>\\SavedVariables\\PalDump.lua')
end
