local ADDON_NAME = ...
local SSDuelWager = CreateFrame("Frame")
local SERVER_PREFIX = "HCDW"
local POPUP_AMOUNT = "SERVERSYSTEMS_DUEL_AMOUNT_POPUP"
local POPUP_DUEL_TYPE = "SERVERSYSTEMS_DUEL_TYPE_POPUP"
local GOLD_ICON = "|TInterface\\MoneyFrame\\UI-GoldIcon:15:15:0:0|t"
local SKULL_ICON = "|TInterface\\Icons\\INV_Misc_Bone_HumanSkull_01:16:16:0:0|t"

local activeRequest
local activeDuelTypeTooltipButton
local originalStartDuel
local originalUnitPopupOnClick

local locale = GetLocale and GetLocale() or "enUS"
local strings = {
    enUS = {
        SELECT_PLAYER = "Select a player first.",
        AMOUNT_POPUP = "Gold duel with %s\n\n" .. GOLD_ICON .. " Gold amount:",
        ENTER_AMOUNT = "Enter an amount, for example 10g.",
        TARGET_NOT_FOUND = "Could not find the target player.",
        DUEL_POPUP = "%s has challenged you to a gold duel for %s.",
        TYPE_POPUP = "Select duel type with %s:",
        TYPE_NORMAL = "Normal",
        TYPE_MONEY = "Gold",
        TYPE_EQUIPMENT = "Mak'gora (Death)",
        EQUIPMENT_DISABLED = "Equipment duels are not available yet.",
        TIP_NORMAL_TITLE = "Normal Duel",
        TIP_NORMAL_BODY = "Challenge the selected player to a standard duel. No gold or equipment is wagered.",
        TIP_MONEY_TITLE = "Gold Duel",
        TIP_MONEY_BODY = "Both players wager the chosen gold amount. The winner receives both stakes. Cancelled or interrupted duels are refunded.",
        TIP_EQUIPMENT_TITLE = "Mak'gora (Death Duel)",
        TIP_EQUIPMENT_BODY = "High-stakes duel to the death! The loser suffers permanent character death and lockout.",
    },
    esES = {
        SELECT_PLAYER = "Selecciona un jugador primero.",
        AMOUNT_POPUP = "Duelo por Oro con %s\n\n" .. GOLD_ICON .. " Cantidad en oro:",
        ENTER_AMOUNT = "Escribe una cantidad, por ejemplo 10g.",
        TARGET_NOT_FOUND = "No se pudo encontrar el jugador objetivo.",
        DUEL_POPUP = "%s te ha retado a un duelo de oro por %s.",
        TYPE_POPUP = "Selecciona el tipo de duelo con %s:",
        TYPE_NORMAL = "Normal",
        TYPE_MONEY = "Oro",
        TYPE_EQUIPMENT = "Mak'gora (Muerte)",
        EQUIPMENT_DISABLED = "Los duelos por equipamiento aún no están disponibles.",
        TIP_NORMAL_TITLE = "Duelo normal",
        TIP_NORMAL_BODY = "Reta al jugador seleccionado a un duelo normal. No se apuesta oro ni equipamiento.",
        TIP_MONEY_TITLE = "Duelo por Oro",
        TIP_MONEY_BODY = "Ambos jugadores apuestan oro con la cantidad elegida. El ganador recibe ambas apuestas. Si se cancela o interrumpe, se devuelve.",
        TIP_EQUIPMENT_TITLE = "Mak'gora (Duelo a Muerte)",
        TIP_EQUIPMENT_BODY = "Duelo a muerte de alto riesgo! El perdedor muere permanentemente y su personaje queda bloqueado.",
    }
}
strings.enGB = strings.enUS
strings.koKR = strings.enUS
strings.frFR = strings.enUS
strings.deDE = strings.enUS
strings.zhCN = strings.enUS
strings.zhTW = strings.enUS
strings.esMX = strings.esES
strings.ruRU = strings.enUS

local L = strings[locale] or strings.enUS
local TOOLTIP_CURSOR_OFFSET_X = 18
local TOOLTIP_CURSOR_OFFSET_Y = 18
local TOOLTIP_SCREEN_PADDING = 8

local DUEL_TYPE_TOOLTIPS = {
    [1] = function()
        return L.TIP_NORMAL_TITLE, L.TIP_NORMAL_BODY
    end,
    [2] = function()
        return GOLD_ICON .. " " .. L.TIP_MONEY_TITLE, L.TIP_MONEY_BODY
    end,
    [3] = function()
        return SKULL_ICON .. " " .. L.TIP_EQUIPMENT_TITLE, L.TIP_EQUIPMENT_BODY
    end,
}

local function Print(message)
    if ServerSystems and ServerSystems.Print then
        ServerSystems.Print(message)
    elseif DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage(tostring(message), 1.0, 0.82, 0.0)
    end
end

local function SendServerCommand(command)
    local playerName = UnitName("player")
    if not playerName or playerName == "" then
        return
    end

    if RegisterAddonMessagePrefix then
        RegisterAddonMessagePrefix(SERVER_PREFIX)
    end

    if SendAddonMessage then
        local ok = pcall(
            SendAddonMessage,
            SERVER_PREFIX,
            "COMMAND\t" .. command,
            "WHISPER",
            playerName)
        if ok then
            return
        end
    end

    Print("Could not send the duel request to the server.")
end

local function Trim(value)
    return (value or ""):gsub("^%s+", ""):gsub("%s+$", "")
end

local function GetPopupTextRegion(popup)
    if not popup then
        return
    end

    if popup.text then
        return popup.text
    end

    local name = popup.GetName and popup:GetName()
    return name and _G[name .. "Text"]
end

local function GetPopupButton(popup, index)
    if not popup or not index then
        return
    end

    local button = popup["button" .. index]
    if button then
        return button
    end

    local name = popup.GetName and popup:GetName()
    return name and _G[name .. "Button" .. index]
end

local function PositionDuelTypeTooltipBelowCursor()
    if not GameTooltip or not activeDuelTypeTooltipButton or not UIParent or not GetCursorPosition then
        return
    end

    local scale = UIParent.GetEffectiveScale and UIParent:GetEffectiveScale() or 1
    if scale == 0 then
        scale = 1
    end

    local cursorX, cursorY = GetCursorPosition()
    cursorX = (cursorX or 0) / scale
    cursorY = (cursorY or 0) / scale

    local screenWidth = UIParent.GetWidth and UIParent:GetWidth() or 1024
    local screenHeight = UIParent.GetHeight and UIParent:GetHeight() or 768
    local tooltipWidth = GameTooltip.GetWidth and GameTooltip:GetWidth() or 260
    local tooltipHeight = GameTooltip.GetHeight and GameTooltip:GetHeight() or 80

    local x = cursorX + TOOLTIP_CURSOR_OFFSET_X
    local y = cursorY - TOOLTIP_CURSOR_OFFSET_Y

    if x + tooltipWidth > screenWidth - TOOLTIP_SCREEN_PADDING then
        x = screenWidth - tooltipWidth - TOOLTIP_SCREEN_PADDING
    end
    if x < TOOLTIP_SCREEN_PADDING then
        x = TOOLTIP_SCREEN_PADDING
    end

    if y - tooltipHeight < TOOLTIP_SCREEN_PADDING then
        y = cursorY + tooltipHeight + TOOLTIP_CURSOR_OFFSET_Y
    end
    if y > screenHeight - TOOLTIP_SCREEN_PADDING then
        y = screenHeight - TOOLTIP_SCREEN_PADDING
    end

    GameTooltip:ClearAllPoints()
    GameTooltip:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", x, y)
end

local function DuelTypeTooltipOnUpdate()
    if not activeDuelTypeTooltipButton or not GameTooltip or not GameTooltip:IsShown() then
        activeDuelTypeTooltipButton = nil
        SSDuelWager:SetScript("OnUpdate", nil)
        return
    end

    PositionDuelTypeTooltipBelowCursor()
end

local function HideDuelTypeTooltip(button)
    if button and activeDuelTypeTooltipButton and button ~= activeDuelTypeTooltipButton then
        return
    end

    activeDuelTypeTooltipButton = nil
    SSDuelWager:SetScript("OnUpdate", nil)

    if GameTooltip then
        GameTooltip:Hide()
    end
end

local function ShowDuelTypeTooltip(button, tooltipProvider)
    if not GameTooltip or not button or not tooltipProvider then
        return
    end

    local title, body = tooltipProvider()
    activeDuelTypeTooltipButton = button
    GameTooltip:SetOwner(button, "ANCHOR_NONE")
    GameTooltip:SetText(title or "", 1.0, 0.82, 0.0, 1, true)
    if body and body ~= "" then
        GameTooltip:AddLine(body, 1.0, 1.0, 1.0, true)
    end
    GameTooltip:Show()
    PositionDuelTypeTooltipBelowCursor()
    SSDuelWager:SetScript("OnUpdate", DuelTypeTooltipOnUpdate)
end

local function AttachDuelTypeTooltip(button, tooltipProvider)
    if not button or button.ServerSystemsDuelTypeTooltipHooked then
        return
    end

    button.ServerSystemsDuelTypeTooltipHooked = true
    button:SetScript("OnEnter", function(self)
        ShowDuelTypeTooltip(self, tooltipProvider)
    end)
    button:SetScript("OnLeave", function(self)
        HideDuelTypeTooltip(self)
    end)
end

local function AttachDuelTypePopupTooltips(popup)
    for index = 1, 3 do
        AttachDuelTypeTooltip(GetPopupButton(popup, index), DUEL_TYPE_TOOLTIPS[index])
    end
end

local function EnsureDuelTypeCloseButton(popup)
    if not popup then
        return
    end

    local button = popup.ServerSystemsDuelTypeCloseButton
    if not button then
        button = CreateFrame("Button", nil, popup, "UIPanelCloseButton")
        button:SetWidth(24)
        button:SetHeight(24)
        button:SetPoint("TOPRIGHT", popup, "TOPRIGHT", -5, -5)
        button:SetFrameLevel((popup:GetFrameLevel() or 1) + 5)
        button:SetScript("OnClick", function(self)
            HideDuelTypeTooltip()
            self:GetParent():Hide()
        end)

        popup.ServerSystemsDuelTypeCloseButton = button
    end

    button:Show()
end

local function HideDuelTypeCloseButton(popup)
    local button = popup and popup.ServerSystemsDuelTypeCloseButton
    if button then
        button:Hide()
    end
end

local function FindDuelRequestPopup()
    local popupCount = STATICPOPUP_NUMDIALOGS or 4
    for index = 1, popupCount do
        local popup = _G["StaticPopup" .. index]
        if popup and popup.which == "DUEL_REQUESTED" and popup:IsShown() then
            return popup
        end
    end
end

local function UpdateDuelPopup()
    if not activeRequest then
        return
    end

    local popup = FindDuelRequestPopup()
    local textRegion = GetPopupTextRegion(popup)
    if textRegion then
        textRegion:SetText(string.format(L.DUEL_POPUP, activeRequest.challenger, activeRequest.amount))
    end
end

local function OpenAmountPopup(targetName)
    if not targetName or targetName == "" then
        Print(L.SELECT_PLAYER)
        return
    end

    StaticPopup_Show(POPUP_AMOUNT, targetName, nil, { name = targetName })
end

local function ResolveDuelUnit(data)
    if data and data.unit and UnitExists(data.unit) and UnitIsPlayer(data.unit) then
        return data.unit
    end

    if data and data.name and UnitExists("target") and UnitIsPlayer("target") and UnitName("target") == data.name then
        return "target"
    end
end

local function StartNormalDuel(data)
    local unit = ResolveDuelUnit(data)
    if not unit then
        Print(L.TARGET_NOT_FOUND)
        return
    end

    if originalStartDuel then
        originalStartDuel(unit, 1)
    elseif StartDuel then
        StartDuel(unit, 1)
    end
end

local function OpenDuelTypePopup(target)
    if not target or not target.name or target.name == "" then
        Print(L.SELECT_PLAYER)
        return
    end

    StaticPopup_Show(POPUP_DUEL_TYPE, target.name, nil, target)
end

local function ShowIncomingRequest(challenger, amount)
    challenger = Trim(challenger)
    amount = Trim(amount)

    if challenger == "" or amount == "" then
        return
    end

    if activeRequest and activeRequest.challenger == challenger and activeRequest.amount == amount then
        UpdateDuelPopup()
        return
    end

    activeRequest = {
        challenger = challenger,
        amount = amount
    }

    UpdateDuelPopup()
end

StaticPopupDialogs[POPUP_AMOUNT] = {
    text = L.AMOUNT_POPUP,
    button1 = ACCEPT,
    button2 = CANCEL,
    hasEditBox = 1,
    maxLetters = 24,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    preferredIndex = 3,
    OnShow = function(self)
        self.editBox:SetText("")
        self.editBox:SetFocus()
    end,
    OnAccept = function(self, data)
        local amount = Trim(self.editBox:GetText())
        if amount == "" then
            Print(L.ENTER_AMOUNT)
            return
        end

        local targetName = data and data.name
        if not targetName or targetName == "" then
            Print(L.TARGET_NOT_FOUND)
            return
        end

        SendServerCommand('duel "' .. targetName .. '" "' .. amount .. '"')
    end,
    EditBoxOnEnterPressed = function(self)
        local parent = self:GetParent()
        StaticPopup_OnClick(parent, 1)
    end,
    EditBoxOnEscapePressed = function(self)
        self:GetParent():Hide()
    end
}

StaticPopupDialogs[POPUP_DUEL_TYPE] = {
    text = L.TYPE_POPUP,
    button1 = L.TYPE_NORMAL,
    button2 = L.TYPE_MONEY,
    button3 = L.TYPE_EQUIPMENT,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    noCancelOnEscape = 1,
    preferredIndex = 3,
    OnShow = function(self)
        AttachDuelTypePopupTooltips(self)
        EnsureDuelTypeCloseButton(self)
    end,
    OnHide = function(self)
        HideDuelTypeTooltip()
        HideDuelTypeCloseButton(self)
    end,
    OnAccept = function(self, data)
        StartNormalDuel(data)
    end,
    OnCancel = function(self, data, reason)
        if reason == "clicked" then
            OpenAmountPopup(data and data.name)
        end
    end,
    OnAlt = function(self)
        Print(L.EQUIPMENT_DISABLED)
    end,
}

local function GetPopupTarget()
    local dropdownFrame = UIDROPDOWNMENU_INIT_MENU

    if dropdownFrame then
        if dropdownFrame.name and dropdownFrame.name ~= "" then
            return {
                name = dropdownFrame.name,
                unit = dropdownFrame.unit,
            }
        end

        if dropdownFrame.unit and UnitExists(dropdownFrame.unit) and UnitIsPlayer(dropdownFrame.unit) then
            return {
                name = UnitName(dropdownFrame.unit),
                unit = dropdownFrame.unit,
            }
        end
    end

    if UnitExists("target") and UnitIsPlayer("target") then
        return {
            name = UnitName("target"),
            unit = "target",
        }
    end
end

local function OnUnitPopupClick(self)
    local value = self and self.value
    if not value and this then
        value = this.value
    end

    if value == "DUEL" then
        OpenDuelTypePopup(GetPopupTarget())
        return
    end

    if originalUnitPopupOnClick then
        return originalUnitPopupOnClick(self)
    end
end

local function InstallDuelTypeSelector()
    if not originalStartDuel and StartDuel then
        originalStartDuel = StartDuel
    end

    if UnitPopup_OnClick and UnitPopup_OnClick ~= OnUnitPopupClick then
        originalUnitPopupOnClick = UnitPopup_OnClick
        UnitPopup_OnClick = OnUnitPopupClick
    end
end

local function SplitTabs(message)
    local parts = {}

    message = message or ""
    local startIndex = 1
    while true do
        local tabIndex = string.find(message, "\t", startIndex, true)
        if not tabIndex then
            table.insert(parts, string.sub(message, startIndex))
            break
        end

        table.insert(parts, string.sub(message, startIndex, tabIndex - 1))
        startIndex = tabIndex + 1
    end

    return parts
end

local function HandleServerAddonMessage(message)
    local parts = SplitTabs(message)
    local opcode = parts[1]

    if opcode == "REQUEST" then
        ShowIncomingRequest(parts[2], parts[3])
    elseif opcode == "EXPIRED" then
        activeRequest = nil
    elseif opcode == "CANCELLED" then
        activeRequest = nil
    elseif opcode == "DECLINED" then
        activeRequest = nil
    elseif opcode == "STARTED" then
        activeRequest = nil
    elseif opcode == "REFUNDED" then
        activeRequest = nil
    end
end

local function StripColorCodes(message)
    return (message or ""):gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
end

local function HandleSystemMessage(message)
    message = StripColorCodes(message)

    local challenger, amount = string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Use")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Do you")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Use")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Do you")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Usa")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Deseas")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Usa")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Deseas")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^HardcoreSystem:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Use")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^HardcoreSystem:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Do you")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^HardcoreSystem:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Usa")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

    challenger, amount = string.match(message or "", "^HardcoreSystem:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Deseas")
    if challenger and amount then
        ShowIncomingRequest(challenger, amount)
        return
    end

end

SSDuelWager:SetScript("OnEvent", function(self, event, ...)
    if event == "PLAYER_LOGIN" then
        if RegisterAddonMessagePrefix then
            RegisterAddonMessagePrefix(SERVER_PREFIX)
        end

        InstallDuelTypeSelector()

        if StaticPopup_Show then
            hooksecurefunc("StaticPopup_Show", function(which)
                if which == "DUEL_REQUESTED" then
                    UpdateDuelPopup()
                end
            end)
        end

    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message = ...
        prefix = prefix or arg1
        message = message or arg2

        if prefix == SERVER_PREFIX then
            HandleServerAddonMessage(message)
        end
    elseif event == "CHAT_MSG_SYSTEM" then
        local message = ... or arg1
        HandleSystemMessage(message)
    elseif event == "DUEL_REQUESTED" then
        UpdateDuelPopup()
    elseif event == "DUEL_FINISHED" then
        activeRequest = nil
    end
end)

SSDuelWager:RegisterEvent("PLAYER_LOGIN")
SSDuelWager:RegisterEvent("CHAT_MSG_ADDON")
SSDuelWager:RegisterEvent("CHAT_MSG_SYSTEM")
SSDuelWager:RegisterEvent("DUEL_REQUESTED")
SSDuelWager:RegisterEvent("DUEL_FINISHED")
