-- PalDump v4.9 (25.09.2026): одноразовые метки «✓ источник ... работает» при первом
--    срабатывании UNIT_AURA / UNIT_SPELLCAST / COMBAT_TEXT — видно, приходят ли
--    события вообще (даже когда лог выключен). Метка печатается один раз за сессию.
-- PalDump v4.8 (25.09.2026): баннер версии/источников при входе в мир (одна строка
--    сразу показывает, что установлено и что включено) + снап не показывает nil-ауры
--    (снап до подгрузки мира давал «nil [0]»).
-- PalDump v4.7 (25.09.2026): ГЛАВНОЕ. В клиенте подписка на COMBAT_LOG_EVENT_UNFILTERED
--    — запрещённое действие (тест T15 дал попап; T8/T16/T17/T18 — чисто). Поэтому:
--    1) CLEU ПО УМОЛЧАНИЮ ВЫКЛЮЧЕН (попап при входе исчез!) — вкл: /paldumplog cleu on.
--    2) Лог пересажен на РАЗРЕШЁННЫЕ события: UNIT_AURA (ауры/бафы/циклы),
--       UNIT_SPELLCAST_SUCCEEDED (касты), COMBAT_TEXT_UPDATE (урон/хилы).
--    3) Детектор цикла «!!! ЦИКЛ?» работает и без CLEU (общий TrackAuraBurst).
-- PalDump v4.6 (25.09.2026): pcall-броня — ВСЕ обработчики (события, AutoDump,
--    /paldump, snap) обёрнуты: если клиент блокирует вызов с Lua-ошибкой,
--    вместо попапа будет строка «[PalDump] ... ОШИБКА: ...» в чате.
--    T9: C_ClassTalents/C_Traits/GetSpellBookItemInfo СУЩЕСТВУЮТ (table/table/function) —
--    блокируются именно ВЫЗОВЫ (T1/T2/T4).
-- PalDump v4.5 (25.09.2026): ловушка ADDON_ACTION_* ВЫКЛЮЧЕНА по умолчанию —
--    регистрация этих событий в клиенте 12.x сама по себе даёт попап «действие
--    только для интерфейсу Blizzard» (проверь макросом T8). Включить: /paldumplog diag on.
--    T9 выявил: C_ClassTalents/C_Traits недоступны (T1/T2/T4 падают) — авто-дамп
--    их и не должен дёргать, пока выключен.
-- PalDump v4.4 (25.09.2026): отлова попапа «действие только для Blizzard».
--    1) АВТО-ДАМП НА ВХОДЕ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕН (главный подозреваемый попапа):
--       включить — /paldumplog auto on ; запустить вручную — /paldumplog dump.
--    2) Снимок аур: путь C_UnitAuras.GetAuraDataByIndex (старый AuraUtil отдавал nil).
--    3) Ловушка попапа: 3 повтора строки + стек debugstack в чат и в лог.
-- PalDump v4.3 (25.09.2026): /paldumplog export [N] открывает ОКНО с текстом лога
--    (рамка по центру экрана): выделение уже стоит → Ctrl+C = копия, куда угодно.
--    Работает без копирования из чата (его в игре нет) и без записи на диск.
-- PalDump v4.2 (25.09.2026): лог больше НЕ зависит от записи на диск.
--    1) /paldumplog export [N] — последние N строк (по умолч. 100); в v4.2 печатались
--       в чат, в v4.3 заменено на окно с EditBox (см. выше).
--    2) Маркеры «!!! ЦИКЛ?» и «!!! АУРА-ШТОРМ» печатаются в чат СРАЗУ (скриншот хватит).
--    3) События ADDON_ACTION_* пишутся и в чат, и в лог (попадут в файл при /reload).
--    Файл WTF\...\SavedVariables\PalDump.lua обновляется клиентом ТОЛЬКО при
--    /reload или чистом выходе из игры (диспетчер задач = ничего не сохранится).
-- PalDump v4.1 (24.09.2026): убран автосейв pcall(SaveVariables) — в клиенте 12.x
--    запись SavedVariables принудительно запрещена, и вызов давал в игре попап
--    «Модификация Paldump заблокирована при попытке выполнять действие, доступное
--    только интерфейсу Blizzard». Лог копится в памяти, на диск попадает при
--    /reload и при выходе из игры. Попап остался? Смотри [PalDump] в чате ниже.
-- PalDump v3: два инструмента в одном.
-- 1) /paldump — выгрузка ID способностей (книга заклинаний) и талантов.
--    КАЖДАЯ специализация сохраняется ОТДЕЛЬНО и НЕ затирает предыдущие:
--    переключите спеку → /paldump → /reload (и так для Света/Воздаяния/Защиты).
-- 2) /paldumplog — БОЕВОЙ ЛОГ: что я кастанул, какие бафы/дебафы получили/потеряли
--    я, пит и цели, хилы/энергайзы по мне, суммоны. Пишется в тот же файл:
--    WTF\Account\<аккаунт>\SavedVariables\PalDump.lua (таблица PalDumpDB.log)
--    На диск при /reload и при выходе из игры (см. v4.1 выше).
--    Команды:
--      /paldumplog          — вкл/выкл лог
--      /paldumplog snap     — мгновенный снимок всех аур (я + цель)
--      /paldumplog show 30  — показать последние 30 строк в чат
--      /paldumplog dmg on   — дополнительно писать урон (много строк!)
--      /paldumplog clear    — очистить лог
-- Совместим со старым и новым API (C_SpellBook / C_Traits / C_SpecializationInfo).

---------------------------------------------------------------------
-- ХРАНИЛИЩЕ
---------------------------------------------------------------------
PalDumpDB = PalDumpDB or {}
PalDumpDB.specs = PalDumpDB.specs or {}
PalDumpDB.cfg   = PalDumpDB.cfg   or { log = true, dmg = false, auto = false, cleu = false }
PalDumpDB.log   = PalDumpDB.log   or {}

local MAX_LOG = 8000          -- кольцевой буфер строк

local playerGUID = nil
local summoned = {}           -- GUID питомцев/стражей из SPELL_SUMMON
local startTime = GetTime()
local lastKey, lastN = nil, 0

-- детектор циклов аур (см. OnCLEU): окно/пороги
local AURA_WINDOW, AURA_BURST = 15, 6    -- >=6 наложений одного спелла за 15с
local STORM_WINDOW, STORM_BURST = 10, 15 -- >=15 аура-событий за 10с
local auraTrack = {}          -- [spellId] = {времена наложений}
local stormRing = {}          -- недавние аура-события на игроке
local stormNames = {}         -- [spellId] = имя
local lastStormMark = -999

local function nowStr()
    return string.format("%7.1f", GetTime() - startTime)
end

local function fmt(ev, id, name, src, dst, extra)
    return string.format("[%s] %-24s %s [%d] %s -> %s%s",
        nowStr(), ev, tostring(name or "?"), id or 0, tostring(src or "?"), tostring(dst or "?"), extra or "")
end

local function addLine(line, key)
    local L = PalDumpDB.log
    if key and key == lastKey and L[#L] then
        lastN = lastN + 1
        L[#L] = line .. string.format("  <<x%d>>", lastN + 1)
    else
        lastKey, lastN = key, 0
        L[#L + 1] = line
        if #L > MAX_LOG then table.remove(L, 1) end
    end
end

---------------------------------------------------------------------
-- БОЕВОЙ ЛОГ (COMBAT_LOG_EVENT_UNFILTERED)
---------------------------------------------------------------------
local AURA_EV = {
    SPELL_AURA_APPLIED       = true,
    SPELL_AURA_APPLIED_DOSE  = true,
    SPELL_AURA_REMOVED       = true,
    SPELL_AURA_REMOVED_DOSE  = true,
    SPELL_AURA_REFRESH       = true,
    SPELL_AURA_BROKEN        = true,
    SPELL_AURA_BROKEN_SPELL  = true,
}

-- Детектор цикла аур (баг храмовника: бафы обновляются каждые 1-2 сек вне боя).
-- Общий для CLEU и UNIT_AURA (v4.7).
local function TrackAuraBurst(spellId, spellName)
    local t = GetTime()
    local lst = auraTrack[spellId]
    if not lst then lst = {}; auraTrack[spellId] = lst end
    lst[#lst + 1] = t
    while lst[1] and lst[1] < t - AURA_WINDOW do table.remove(lst, 1) end
    local n = #lst
    if n >= AURA_BURST and (n == AURA_BURST or (n - AURA_BURST) % 20 == 0) then
        local ln = string.format("!!! ЦИКЛ? [%d] %s — %d наложений за %dс", spellId or 0, tostring(spellName or "?"), n, AURA_WINDOW)
        addLine(ln, nil)
        print("[PalLog] " .. ln)
    end
    stormRing[#stormRing + 1] = { t = t, id = spellId, name = spellName }
    stormNames[spellId] = spellName
    while stormRing[1] and stormRing[1].t < t - STORM_WINDOW do table.remove(stormRing, 1) end
    if #stormRing >= STORM_BURST and t - lastStormMark > 30 then
        local counts, order = {}, {}
        for _, e in ipairs(stormRing) do
            if not counts[e.id] then order[#order + 1] = e.id; counts[e.id] = 0 end
            counts[e.id] = counts[e.id] + 1
        end
        local parts = {}
        for _, id in ipairs(order) do
            parts[#parts + 1] = string.format("[%d]%s x%d", id or 0, tostring(stormNames[id] or "?"), counts[id])
        end
        local ln = string.format("!!! АУРА-ШТОРМ: %d аура-событий за %dс: %s", #stormRing, STORM_WINDOW, table.concat(parts, ", "))
        addLine(ln, nil)
        print("[PalLog] " .. ln)
        lastStormMark = t
    end
end

local function OnCLEU()
    if not PalDumpDB.cfg.log then return end
    local petG = UnitGUID("pet")
    local _, ev, _, sGUID, sName, _, _, dGUID, dName, _, _, spellId, spellName, _, a1, a2, a3, a4, a5, a6, a7 = CombatLogGetCurrentEventInfo()
    if not ev then return end

    local isPlayerAuraLoopEvent = dGUID == playerGUID and
        (ev == "SPELL_AURA_APPLIED" or ev == "SPELL_AURA_APPLIED_DOSE" or ev == "SPELL_AURA_REFRESH")
    if isPlayerAuraLoopEvent then
        TrackAuraBurst(spellId, spellName)
    end

    local petS = petG and sGUID == petG
    local petD = petG and dGUID == petG
    local srcMine = sGUID == playerGUID or summoned[sGUID] or petS
    local dstMine = dGUID == playerGUID or summoned[dGUID] or petD
    if not (srcMine or dstMine) then return end

    local src = srcMine and (petS and "ПИТ" or "Я") or tostring(sName or sGUID or "?")
    local dst = dstMine and (petD and "ПИТ" or "Я") or tostring(dName or dGUID or "?")

    local line, key
    if ev == "SPELL_CAST_SUCCESS" then
        line = fmt(ev, spellId, spellName, src, dst)
        key = "C" .. spellId .. dst
    elseif AURA_EV[ev] then
        local ex
        if ev == "SPELL_AURA_APPLIED" or ev == "SPELL_AURA_APPLIED_DOSE" or ev == "SPELL_AURA_REMOVED_DOSE" then
            ex = (a1 == "BUFF") and " [баф]" or " [дебаф]"
            if ev ~= "SPELL_AURA_APPLIED" and a2 and a2 > 1 then ex = ex .. " стаков:" .. a2 end
        elseif ev == "SPELL_AURA_REFRESH" then
            ex = (a1 == "BUFF") and " [баф] обновлён" or " [дебаф] обновлён"
        elseif ev == "SPELL_AURA_BROKEN" then
            ex = " сорван"
        elseif ev == "SPELL_AURA_BROKEN_SPELL" then
            ex = " сорван спеллом " .. tostring(a2 or "?")
        else
            ex = ""
        end
        line = fmt(ev, spellId, spellName, src, dst, ex)
        key = "A" .. ev .. spellId .. dst
    elseif ev == "SPELL_SUMMON" then
        if srcMine and dGUID then summoned[dGUID] = true end
        line = fmt(ev, spellId, spellName, src, dst)
        key = nil
    elseif ev == "SPELL_ENERGIZE" and dstMine then
        line = fmt(ev, spellId, spellName, src, dst, " +" .. tostring(a2 or 0) .. " ресурс")
        key = "E" .. spellId .. tostring(a1 or "")
    elseif (ev == "SPELL_HEAL" or ev == "SPELL_PERIODIC_HEAL") and dstMine then
        line = fmt(ev, spellId, spellName, src, dst, " хил:" .. tostring(a1 or 0) .. (a4 and " КРИТ" or ""))
        key = "H" .. spellId .. src
    elseif PalDumpDB.cfg.dmg and (ev == "SPELL_DAMAGE" or ev == "SPELL_PERIODIC_DAMAGE") then
        line = fmt(ev, spellId, spellName, src, dst, " урон:" .. tostring(a1 or 0) .. (a7 and " КРИТ" or ""))
        key = "D" .. ev .. spellId .. dst
    elseif ev == "SPELL_DISPEL" then
        line = fmt(ev, spellId, spellName, src, dst, " снял: " .. tostring(a2 or "?"))
        key = nil
    elseif ev == "SPELL_INTERRUPT" then
        line = fmt(ev, spellId, spellName, src, dst, " прервал: " .. tostring(a2 or "?"))
        key = nil
    else
        return
    end
    addLine(line, key)
end

---------------------------------------------------------------------
-- СНИМОК АУР (/paldumplog snap)
---------------------------------------------------------------------
local function EachAura(unit, filter, cb)
    -- 12.x: прямой путь C_UnitAuras (AuraUtil в их клиенте отдавал name=nil)
    if C_UnitAuras and C_UnitAuras.GetAuraDataByIndex then
        for i = 1, 60 do
            local ok, a = pcall(C_UnitAuras.GetAuraDataByIndex, unit, i, filter)
            if not ok or not a then break end
            if a.name then -- защита от «nil [0]» при снапе до подгрузки мира
                cb(a.name, a.spellId or a.spellID, a.applications, a.expirationTime, a.sourceUnit)
            end
        end
        return true
    end
    local done = false
    if AuraUtil and AuraUtil.ForEachAura then
        done = pcall(AuraUtil.ForEachAura, unit, filter, nil,
            function(a)
                if not a then return true end
                cb(a.name, a.spellId or a.spellID, a.applications, a.expirationTime, a.sourceUnit)
                return true
            end)
    end
    if not done then
        for i = 1, 40 do
            local name, _, count, _, _, expirationTime, _, _, _, spellId = UnitAura(unit, i, filter)
            if not name then break end
            cb(name, spellId, count, expirationTime, nil)
        end
    end
end

---------------------------------------------------------------------
-- v4.7: ЛОГ ЧЕРЕЗ РАЗРЕШЁННЫЕ СОБЫТИЯ (CLEU в этом клиенте запрещён)
---------------------------------------------------------------------
local auraCache = {} -- [unit] = { ["H".."378412"] = {id=, name=, n=, exp=} }

local function SpellName(id)
    if C_Spell and C_Spell.GetSpellInfo then
        local ok, r = pcall(C_Spell.GetSpellInfo, id)
        if ok and r then
            if type(r) == "table" then return r.name end
            return r
        end
    end
    if GetSpellInfo then
        local ok, n = pcall(GetSpellInfo, id)
        if ok then return n end
    end
    return nil
end

local function SnapAuras(unit)
    local now = {}
    for _, filter in ipairs({ "HELPFUL", "HARMFUL" }) do
        EachAura(unit, filter, function(name, id, stacks, exp)
            if id then
                now[filter:sub(1, 1) .. id] = { id = id, name = name, n = stacks or 1, exp = exp or 0 }
            end
        end)
    end
    return now
end

local srcSeen = { aura = false, cast = false, ct = false }

local function OnUnitAura(unit)
    if not unit then return end
    if not (unit == "player" or unit == "pet" or unit == "target" or unit == "focus"
        or unit:match("^party%d$") or unit:match("^raid%d$")) then return end
    if unit == "player" and not srcSeen.aura then
        srcSeen.aura = true
        print("[PalLog] ✓ источник UNIT_AURA работает (бафы/дебафы)")
    end
    if not PalDumpDB.cfg.log then return end
    local now = SnapAuras(unit)
    local prev = auraCache[unit]
    auraCache[unit] = now
    if not prev then return end -- первый снап юнита — база без дельты
    local tag = unit == "player" and "Я" or (unit == "pet" and "ПИТ" or unit)
    for k, v in pairs(now) do
        local p = prev[k]
        if not p then
            addLine(fmt("AURA_APPLIED", v.id, v.name, tag, "", (v.n or 1) > 1 and (" x" .. v.n) or ""), nil)
            if unit == "player" then TrackAuraBurst(v.id, v.name) end
        elseif p.n ~= v.n then
            addLine(fmt("AURA_DOSE", v.id, v.name, tag, "", " стаков:" .. tostring(v.n)), nil)
            if unit == "player" then TrackAuraBurst(v.id, v.name) end
        elseif p.exp ~= v.exp then
            addLine(fmt("AURA_REFRESH", v.id, v.name, tag, "", " обновлён"), nil)
            if unit == "player" then TrackAuraBurst(v.id, v.name) end
        end
    end
    for k, p in pairs(prev) do
        if not now[k] then
            addLine(fmt("AURA_REMOVED", p.id, p.name, tag, "", ""), nil)
        end
    end
end

local function OnCastOk(unit, spellID)
    if unit ~= "player" and unit ~= "target" and unit ~= "focus" then return end
    if not PalDumpDB.cfg.log then return end
    local who = unit == "player" and "Я" or unit
    addLine(fmt("SPELL_CAST_SUCCEEDED", spellID, SpellName(spellID) or "?", who, ""), "C" .. tostring(spellID) .. unit)
end

local function OnCombatText(t, a2, a3)
    if not srcSeen.ct then
        srcSeen.ct = true
        print("[PalLog] ✓ источник COMBAT_TEXT работает (урон/хилы)")
    end
    if not PalDumpDB.cfg.log then return end
    local what = tostring(t or "?")
    if what ~= "HEAL" and what ~= "DAMAGE" and what ~= "DAMAGE_SHIELD" and what ~= "ENERGIZE" then return end
    addLine(fmt("CT_" .. what, 0, "", "Я", "", " " .. tostring(a2 or "") .. " " .. tostring(a3 or "")), nil)
end

local function Snapshot()
    local t = GetTime()
    local function dump(unit, filter, label)
        local n = 0
        EachAura(unit, filter, function(name, id, stacks, exp, srcUnit)
            n = n + 1
            local rem = (exp and exp > 0 and exp - t > 0) and string.format(" ост.%ds", math.ceil(exp - t)) or ""
            local stk = (stacks and stacks > 1) and (" x" .. stacks) or ""
            local who = ""
            if srcUnit == "player" then who = " от-меня"
            elseif srcUnit == "pet" then who = " от-пита"
            elseif srcUnit then who = " от:" .. tostring(srcUnit) end
            addLine(string.format("[СНИМОК] %s: %s [%d]%s%s%s", label, tostring(name), id or 0, stk, rem, who), nil)
            return true
        end)
        return n
    end
    local b = dump("player", "HELPFUL", "мой-баф")
    local d = dump("player", "HARMFUL", "мой-дебаф")
    local tn = 0
    if UnitExists("target") then
        tn = dump("target", "HELPFUL", "цель-баф")
        tn = tn + dump("target", "HARMFUL", "цель-дебаф")
    end
    addLine(string.format("=== СНИМОК: у меня бафов %d, дебафов %d, на цели аур %d ===", b, d, tn), nil)
    auraCache["player"] = SnapAuras("player")
    if UnitExists("target") then auraCache["target"] = SnapAuras("target") end
    print(("[PalLog] Снимок записан: у меня %d бафов / %d дебафов, на цели %d аур"):format(b, d, tn))
end

---------------------------------------------------------------------
-- КОМАНДЫ
---------------------------------------------------------------------
---------------------------------------------------------------------
-- ОКНО ЭКСПОРТА (/paldumplog export): текст в рамке → Ctrl+A, Ctrl+C
---------------------------------------------------------------------
local function ShowExport(text)
    local f = PalDumpCopyFrame
    if not f then
        f = CreateFrame("Frame", "PalDumpCopyFrame", UIParent, "BasicFrameTemplateWithInset")
        f:SetSize(640, 460)
        f:SetPoint("CENTER")
        if f.TitleContainer and f.TitleContainer.TitleText then
            f.TitleContainer.TitleText:SetText("PalDump — экспорт (Ctrl+A, Ctrl+C)")
        end
        local eb = CreateFrame("EditBox", nil, f)
        eb:SetMultiLine(true)
        eb:SetMaxLetters(0)
        eb:SetFontObject(GameFontHighlight)
        eb:SetScript("OnEscapePressed", function(box) box:ClearFocus() end)
        eb:SetPoint("TOPLEFT", 14, -32)
        eb:SetPoint("BOTTOMRIGHT", -14, 14)
        local tex = eb:CreateTexture(nil, "BACKGROUND")
        tex:SetAllPoints()
        tex:SetColorTexture(0.02, 0.02, 0.02, 0.9)
        f.editBox = eb
        if f.CloseButton then
            f.CloseButton:SetScript("OnClick", function() f:Hide() end)
        end
        f:SetScript("OnHide", function() eb:ClearFocus() end)
        f:SetMovable(true)
        f:EnableMouse(true)
        f:RegisterForDrag("LeftButton")
        f:SetScript("OnDragStart", function(frame) frame:StartMoving() end)
        f:SetScript("OnDragStop", function(frame) frame:StopMovingOrSizing() end)
        table.insert(UISpecialFrames, "PalDumpCopyFrame")  -- Esc закрывает
    end
    f.editBox:SetText(text)
    f:Show()
    f.editBox:SetFocus()
    f.editBox:HighlightText()
    print("[PalLog] Окно экспорта: выделено всё — Ctrl+C (копия), Ctrl+V вставь куда нужно.")
end

SLASH_PALDUMPLOG1 = "/paldumplog"
SlashCmdList["PALDUMPLOG"] = function(msg)
    msg = tostring(msg or ""):lower():gsub("^%s+", ""):gsub("%s+$", "")
    if msg == "" or msg == "on" or msg == "off" then
        local v = (msg == "" and not PalDumpDB.cfg.log) or (msg == "on")
        PalDumpDB.cfg.log = v
        addLine(v and "=== ЛОГ ВКЛЮЧЁН ===" or "=== ЛОГ ВЫКЛЮЧЕН ===", nil)
        print("[PalLog] Боевой лог: " .. (v and "ВКЛЮЧЁН (касты, бафы/дебафы, хилы по мне, суммоны)" or "ВЫКЛЮЧЕН"))
        print("[PalLog] /paldumplog snap — снимок аур | /paldumplog show 30 | /paldumplog export 200 — ОКНО ДЛЯ Ctrl+C | /paldumplog clear")
    elseif msg == "snap" then
        local oks, errs = pcall(Snapshot)
        if not oks then print("[PalDump] snap ОШИБКА: " .. tostring(errs)) end
    elseif msg:match("^show") then
        local n = tonumber(msg:match("show%s+(%d+)")) or 20
        local L = PalDumpDB.log
        for i = math.max(1, #L - n + 1), #L do print("  " .. L[i]) end
        print(("[PalLog] показано %d из %d (весь файл: WTF\\Account\\<аккаунт>\\SavedVariables\\PalDump.lua)")
            :format(math.min(n, #L), #L))
    elseif msg:match("^export") then
        local n = tonumber(msg:match("export%s+(%d+)")) or 100
        local L = PalDumpDB.log
        local first = math.max(1, #L - n + 1)
        local buf = {}
        for i = first, #L do buf[#buf + 1] = L[i] end
        print(("[PalLog] Экспорт строк %d..%d из %d — окно открыто, Ctrl+A → Ctrl+C"):format(first, #L, #L))
        ShowExport(table.concat(buf, "\n"))
    elseif msg == "dmg on" or msg == "dmg off" then
        PalDumpDB.cfg.dmg = (msg == "dmg on")
        print("[PalLog] Писать урон: " .. (PalDumpDB.cfg.dmg and "ВКЛ (лог будет расти быстро)" or "ВЫКЛ"))
    elseif msg == "auto on" or msg == "auto off" then
        PalDumpDB.cfg.auto = (msg == "auto on")
        print("[PalLog] Авто-дамп книги/талантов на входе: " .. (PalDumpDB.cfg.auto and "ВКЛЮЧЁН" or "ВЫКЛЮЧЕН"))
        if PalDumpDB.cfg.auto then print("[PalLog] Проверь после входа в мир: не появился ли попап Paldump") end
    elseif msg == "dump" then
        print("[PalLog] Ручной авто-дамп...")
        AutoDump(true)
    elseif msg == "diag on" or msg == "diag off" then
        DiagSet(msg == "diag on")
    elseif msg == "cleu on" or msg == "cleu off" then
        PalDumpDB.cfg.cleu = (msg == "cleu on")
        if PalDumpDB.cfg.cleu then
            PalDumpMainFrame:RegisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
            print("[PalLog] CLEU: ВКЛ — при следующем боевом событии возможен попап, жми «Пропустить»")
        else
            PalDumpMainFrame:UnregisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
            print("[PalLog] CLEU: ВЫКЛ (лог работает через UNIT_AURA/UNIT_SPELLCAST)")
        end
    elseif msg == "clear" then
        PalDumpDB.log = {}
        lastKey, lastN = nil, 0
        print("[PalLog] Лог очищен")
    else
        print("Использование: /paldumplog [on|off|snap|show N|export N|dump|auto on|diag on|cleu on|dmg on|clear]")
    end
end

---------------------------------------------------------------------
-- СОБЫТИЯ ЖИЗНИ и АВТО-ДАМП — в конце файла (нужны локалы из /paldump)
---------------------------------------------------------------------
---------------------------------------------------------------------
-- /paldump — выгрузка книги и талантов (как раньше)
---------------------------------------------------------------------
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
    local okp, errp = pcall(function()
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
    end)
    if not okp then print("[PalDump] /paldump ОШИБКА: " .. tostring(errp)) end
end

-- АВТО-ДАМП: книга + таланты текущей спеки пишутся в файл сами при входе
-- и при смене спеки — руками вызывать /paldump больше не обязательно.
-- (global — чтобы видел замыкание slash-команды, объявленного выше)
function AutoDump(force)
    if not force and PalDumpDB.cfg.auto ~= true then return end -- v4.4: на входе выключен (попап)
    local okd, errd = pcall(function()
    local specName, specID = SpecInfo()
    local book, skipped = DumpBook()
    if #book == 0 then return end -- книга ещё не загрузилась, попробует при смене спеки
    local tal = DumpTalents()
    PalDumpDB.specs[tostring(specID)] = {
        name = specName, book = book, talents = tal,
        time = date("%Y-%m-%d %H:%M"), auto = true,
    }
    addLine(string.format("=== АВТО-ДАМП спек %s (%s): способностей %d (пропущено %d), талантов %d ===",
        tostring(specName), tostring(specID), #book, skipped, #tal), nil)
    print(("[PalDump] авто-дамп: %s (%s): способностей %d, талантов %d — сохранено")
        :format(tostring(specName), tostring(specID), #book, #tal))
    end)
    if not okd then print("[PalDump] авто-дамп ОШИБКА: " .. tostring(errd)) end
end

PalDumpMainFrame = CreateFrame("Frame", "PalDumpMainFrame")
PalDumpMainFrame:RegisterEvent("PLAYER_ENTER_WORLD")
PalDumpMainFrame:RegisterEvent("PLAYER_SPECIALIZATION_CHANGED")
PalDumpMainFrame:RegisterEvent("PLAYER_REGEN_DISABLED")
PalDumpMainFrame:RegisterEvent("PLAYER_REGEN_ENABLED")
PalDumpMainFrame:RegisterEvent("PLAYER_LOGOUT")
-- v4.7: разрешённые события (T16-T18 подтверждены — БЕЗ попапа)
pcall(PalDumpMainFrame.RegisterEvent, PalDumpMainFrame, "UNIT_AURA")
pcall(PalDumpMainFrame.RegisterEvent, PalDumpMainFrame, "UNIT_SPELLCAST_SUCCEEDED")
pcall(PalDumpMainFrame.RegisterEvent, PalDumpMainFrame, "COMBAT_TEXT_UPDATE")
-- CLEU в этом клиенте = ЗАПРЕЩЁННОЕ действие (попап при входе) — только по флагу:
if PalDumpDB.cfg.cleu == true then
    PalDumpMainFrame:RegisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
end
PalDumpMainFrame:SetScript("OnEvent", function(_, event, ...)
    local a1, a2, a3 = ...
    local ok, err = pcall(function()
    if event == "COMBAT_LOG_EVENT_UNFILTERED" then
        OnCLEU()
    elseif event == "UNIT_AURA" then
        OnUnitAura(a1)
    elseif event == "UNIT_SPELLCAST_SUCCEEDED" then
        OnCastOk(a1, a3)
    elseif event == "COMBAT_TEXT_UPDATE" then
        OnCombatText(a1, a2, a3)
    elseif event == "PLAYER_ENTER_WORLD" then
        playerGUID = UnitGUID("player")
        summoned = {}
        auraTrack, stormRing, stormNames = {}, {}, {}
        auraCache = {}
        startTime = GetTime()
        local _, build = GetBuildInfo()
        -- ужимаем лог, если накопился за много сессий
        local L = PalDumpDB.log
        if #L > 6000 then
            local keep = {}
            for i = #L - 4000 + 1, #L do keep[#keep + 1] = L[i] end
            PalDumpDB.log = keep
        end
        addLine(string.format("===== СЕССИЯ %s, клиент %s =====", date("%Y-%m-%d %H:%M:%S"), build or "?"), nil)
        print(("[PalLog] Лог активен: /paldumplog export — выгрузка в чат, /paldumplog show 20 — последние строки (клиент %s)"):format(build or "?"))
        if PalDumpDB.cfg.auto ~= true then
            print("[PalLog] Авто-дамп выключен (ловим попап): /paldumplog dump — вручную | /paldumplog auto on — на входе")
        end
        -- v4.8: БАННЕР — что стоит и что включено (главный индикатор версии)
        print(("[PalLog] PalDump v4.8 | лог: UNIT_AURA+SPELLCAST+COMBAT_TEXT | CLEU=%s | auto=%s | состояние: %s"):format(
            PalDumpDB.cfg.cleu == true and "вкл" or "выкл",
            PalDumpDB.cfg.auto == true and "вкл" or "выкл",
            PalDumpDB.cfg.log and "ЛОГ ВКЛ" or "ЛОГ ВЫКЛ"))
        if C_Timer and C_Timer.After then
            C_Timer.After(5, AutoDump)
            -- база для дельт аур: снап через 3 с (данные к этому моменту загружены)
            C_Timer.After(3, function() auraCache["player"] = SnapAuras("player") end)
        end
    elseif event == "PLAYER_SPECIALIZATION_CHANGED" then
        if C_Timer and C_Timer.After then C_Timer.After(3, AutoDump) end
    elseif event == "PLAYER_REGEN_DISABLED" then
        if PalDumpDB.cfg.log then addLine("=== ВСТУПИЛ В БОЙ ===", nil) end
    elseif event == "PLAYER_REGEN_ENABLED" then
        if PalDumpDB.cfg.log then addLine("=== ВЫШЕЛ ИЗ БОЯ ===", nil) end
    elseif event == "PLAYER_LOGOUT" then
        addLine("=== ВЫХОД ===", nil)
        print(("[PalLog] Выход: лог %d строк — файл сохраняется"):format(#PalDumpDB.log))
    end
    end)
    if not ok then
        print("[PalDump] ОШИБКА события (блок-попап?): " .. tostring(err))
        addLine("=== ERR " .. tostring(err) .. " ===", nil)
    end
end)
-- ДИАГНОСТИКА TAINT (v4.1): если всплывает попап «Модификация Paldump
-- заблокирована…», печатаем в чат событие, аддон и функцию — так видно, что
-- именно блокируется (если это не PalDump — покажет имя настоящего виновника).
local diag = CreateFrame("Frame")
diag:SetScript("OnEvent", function(_, event, ...)
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring((select(i, ...))) end
    local ln = event .. ": " .. table.concat(parts, ", ")
    local stack = (debugstack and debugstack(3, 6, 6) or ""):gsub("%s+$", "")
    -- 3 повтора, чтобы строку НЕЛЬЗЯ было пропустить в скролле чата
    for _ = 1, 3 do print("[PalDump] " .. ln) end
    if stack ~= "" then print("[PalDump] стек вызова:\n" .. stack) end
    addLine("=== TAINT " .. ln .. " ===", nil)
    if stack ~= "" then addLine("=== STACK " .. stack:gsub("\n", " >> ") .. " ===", nil) end
end)
-- v4.5: регистрация ADDON_ACTION_* ПО ФЛАГУ. По умолчанию ВЫКЛ: в клиенте 12.x
-- сам факт подписки не-Blizzard аддона на эти события даёт попап «только для
-- интерфейсу Blizzard» (подтвердится макросом T8). /paldumplog diag on|off.
function DiagSet(on)
    if on then
        diag:RegisterEvent("ADDON_ACTION_FORBIDDEN")
        diag:RegisterEvent("ADDON_ACTION_BLOCKED")
        PalDumpDB.cfg.diag = true
        print("[PalDump] Ловушка попапа: ВКЛ (следи за строками [PalDump] ADDON_ACTION_*)")
    else
        diag:UnregisterAllEvents()
        PalDumpDB.cfg.diag = false
        print("[PalDump] Ловушка попапа: ВЫКЛ")
    end
end
if PalDumpDB.cfg.diag == true then DiagSet(true) end
