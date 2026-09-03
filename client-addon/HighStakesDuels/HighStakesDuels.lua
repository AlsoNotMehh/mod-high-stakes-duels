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
        DUEL_POPUP_GOLD = "%s has challenged you to a gold duel for %s.",
        DUEL_POPUP_MAKGORA = SKULL_ICON .. " |cFFFF1111MAK'GORA (DEATH DUEL)|r\n\n%s has challenged you to a duel to the death!\n\n|cFFFF4444WARNING: The loser suffers permanent character death!|r",
        TYPE_POPUP = "Select duel type with %s:",
        TYPE_NORMAL = "Normal",
        TYPE_MONEY = "Gold",
        TYPE_MAKGORA = "Mak'gora (Death)",
        TIP_NORMAL_TITLE = "Normal Duel",
        TIP_NORMAL_BODY = "Challenge the selected player to a standard duel. No gold is wagered and nobody dies.",
        TIP_MONEY_TITLE = "Gold Duel",
        TIP_MONEY_BODY = "Both players wager the chosen gold amount. The winner receives both stakes. Cancelled or interrupted duels are refunded.",
        TIP_MAKGORA_TITLE = "Mak'gora (Death Duel)",
        TIP_MAKGORA_BODY = "High-stakes duel to the death! The loser suffers permanent character death and lockout.",
        MAKGORA_SENT = "Mak'gora duel request sent to %s.",
    },
    esES = {
        SELECT_PLAYER = "Selecciona un jugador primero.",
        AMOUNT_POPUP = "Duelo por Oro con %s\n\n" .. GOLD_ICON .. " Cantidad en oro:",
        ENTER_AMOUNT = "Escribe una cantidad, por ejemplo 10g.",
        TARGET_NOT_FOUND = "No se pudo encontrar el jugador objetivo.",
        DUEL_POPUP_GOLD = "%s te ha retado a un duelo de oro por %s.",
        DUEL_POPUP_MAKGORA = SKULL_ICON .. " |cFFFF1111¡DUELO A MUERTE (MAK'GORA)!|r\n\n%s te ha retado a un duelo a muerte.\n\n|cFFFF4444¡ADVERTENCIA: El perdedor morirá de forma permanente y su personaje quedará bloqueado!|r",
        TYPE_POPUP = "Selecciona el tipo de duelo con %s:",
        TYPE_NORMAL = "Normal",
        TYPE_MONEY = "Oro",
        TYPE_MAKGORA = "Mak'gora (Muerte)",
        TIP_NORMAL_TITLE = "Duelo normal",
        TIP_NORMAL_BODY = "Reta al jugador seleccionado a un duelo normal amistoso sin apuestas.",
        TIP_MONEY_TITLE = "Duelo por Oro",
        TIP_MONEY_BODY = "Ambos jugadores apuestan oro con la cantidad elegida. El ganador recibe ambas apuestas. Si se cancela o interrumpe, se devuelve el oro.",
        TIP_MAKGORA_TITLE = "Mak'gora (Duelo a Muerte)",
        TIP_MAKGORA_BODY = "¡Duelo a muerte de máximo riesgo! El perdedor muere de forma permanente y su personaje queda bloqueado.",
        MAKGORA_SENT = "Reto de Mak'gora a muerte enviado a %s.",
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
        return SKULL_ICON .. " " .. L.TIP_MAKGORA_TITLE, L.TIP_MAKGORA_BODY
    end,
}

local function Print(message)
    if DEFAULT_CHAT_FRAME then
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

    if popup.text and popup.text.GetText then
        return popup.text
    end

    local name = popup.GetName and popup:GetName()
    if name and _G[name .. "Text"] then
        return _G[name .. "Text"]
    end

    for _, child in ipairs({ popup:GetRegions() }) do
        if child and child.GetObjectType and child:GetObjectType() == "FontString" then
            return child
        end
    end
end

local function FindDuelRequestPopup()
    for index = 1, STATICPOPUP_NUMDIALOGS or 4 do
        local popup = _G["StaticPopup" .. index]
        if popup and popup:IsShown() and popup.which == "DUEL_REQUESTED" then
            return popup
        end
    end
end

local function PositionDuelTypeTooltip(button)
    if not button or not GameTooltip then
        return
    end

    local cursorX, cursorY = GetCursorPosition()
    local scale = UIParent:GetEffectiveScale() or 1
    cursorX = cursorX / scale
    cursorY = cursorY / scale

    GameTooltip:ClearAllPoints()
    local tooltipWidth = GameTooltip:GetWidth() or 0
    local screenWidth = UIParent:GetWidth() or 0

    if cursorX + TOOLTIP_CURSOR_OFFSET_X + tooltipWidth > screenWidth - TOOLTIP_SCREEN_PADDING then
        GameTooltip:SetPoint(
            "BOTTOMRIGHT",
            UIParent,
            "BOTTOMLEFT",
            cursorX - TOOLTIP_CURSOR_OFFSET_X,
            cursorY - TOOLTIP_CURSOR_OFFSET_Y)
    else
        GameTooltip:SetPoint(
            "BOTTOMLEFT",
            UIParent,
            "BOTTOMLEFT",
            cursorX + TOOLTIP_CURSOR_OFFSET_X,
            cursorY - TOOLTIP_CURSOR_OFFSET_Y)
    end
end

local function UpdateDuelTypeTooltipPosition()
    if activeDuelTypeTooltipButton and GameTooltip and GameTooltip:IsShown() then
        PositionDuelTypeTooltip(activeDuelTypeTooltipButton)
    end
end

local function ShowDuelTypeTooltip(button, buttonIndex)
    local tooltipFunc = DUEL_TYPE_TOOLTIPS[buttonIndex]
    if not tooltipFunc or not button or not GameTooltip then
        return
    end

    local title, body = tooltipFunc()
    if not title or not body then
        return
    end

    activeDuelTypeTooltipButton = button
    GameTooltip:SetOwner(button, "ANCHOR_NONE")
    PositionDuelTypeTooltip(button)
    GameTooltip:AddLine(title, 1.0, 0.82, 0.0)
    GameTooltip:AddLine(body, 1.0, 1.0, 1.0, true)
    GameTooltip:Show()
end

local function HideDuelTypeTooltip()
    activeDuelTypeTooltipButton = nil
    if GameTooltip then
        GameTooltip:Hide()
    end
end

local function AttachDuelTypePopupTooltips(popup)
    if not popup then
        return
    end

    local buttons = {
        popup.button1 or _G[popup:GetName() .. "Button1"],
        popup.button2 or _G[popup:GetName() .. "Button2"],
        popup.button3 or _G[popup:GetName() .. "Button3"],
    }

    for index, button in ipairs(buttons) do
        if button and not button.__SSDuelWagerHooked then
            button.__SSDuelWagerHooked = true
            button:HookScript("OnEnter", function(self)
                ShowDuelTypeTooltip(self, index)
            end)
            button:HookScript("OnLeave", function()
                HideDuelTypeTooltip()
            end)
        end
    end
end

local function EnsureDuelTypeCloseButton(popup)
    if not popup then
        return
    end

    local closeButton = popup.__SSDuelWagerCloseButton
    if not closeButton then
        closeButton = CreateFrame("Button", nil, popup, "UIPanelCloseButton")
        closeButton:SetPoint("TOPRIGHT", popup, "TOPRIGHT", -3, -3)
        closeButton:SetScript("OnClick", function()
            StaticPopup_Hide(POPUP_DUEL_TYPE)
        end)
        popup.__SSDuelWagerCloseButton = closeButton
    end

    closeButton:Show()
end

local function HideDuelTypeCloseButton(popup)
    if popup and popup.__SSDuelWagerCloseButton then
        popup.__SSDuelWagerCloseButton:Hide()
    end
end

local function UpdateDuelPopup()
    if not activeRequest then
        return
    end

    local popup = FindDuelRequestPopup()
    local textRegion = GetPopupTextRegion(popup)
    if textRegion then
        if activeRequest.isMakgora then
            textRegion:SetText(string.format(L.DUEL_POPUP_MAKGORA, activeRequest.challenger))
        else
            textRegion:SetText(string.format(L.DUEL_POPUP_GOLD, activeRequest.challenger, activeRequest.amount))
        end
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

local function ShowIncomingRequest(challenger, amount, isMakgora)
    challenger = Trim(challenger)
    amount = Trim(amount)

    if challenger == "" then
        return
    end

    activeRequest = {
        challenger = challenger,
        amount = amount,
        isMakgora = isMakgora or false
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
            Print(L.SELECT_PLAYER)
            return
        end

        SendServerCommand("duel " .. targetName .. " " .. amount)
    end,
    EditBoxOnEnterPressed = function(self, data)
        local parent = self:GetParent()
        local acceptButton = parent and (parent.button1 or _G[parent:GetName() .. "Button1"])
        if acceptButton and acceptButton:IsEnabled() then
            StaticPopupDialogs[POPUP_AMOUNT].OnAccept(parent, data)
            parent:Hide()
        end
    end,
    EditBoxOnEscapePressed = function(self)
        self:GetParent():Hide()
    end,
}

StaticPopupDialogs[POPUP_DUEL_TYPE] = {
    text = L.TYPE_POPUP,
    button1 = L.TYPE_NORMAL,
    button2 = L.TYPE_MONEY,
    button3 = L.TYPE_MAKGORA,
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
    OnAlt = function(self, data)
        local targetName = data and data.name
        if not targetName or targetName == "" then
            Print(L.SELECT_PLAYER)
            return
        end
        SendServerCommand("duel makgora " .. targetName)
        Print(string.format(L.MAKGORA_SENT, targetName))
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
        ShowIncomingRequest(parts[2], parts[3], false)
    elseif opcode == "MAKGORA_REQUEST" or opcode == "EQUIPMENT_REQUEST" then
        ShowIncomingRequest(parts[2], "Mak'gora", true)
    elseif opcode == "EXPIRED" or opcode == "CANCELLED" or opcode == "DECLINED" or opcode == "STARTED" or opcode == "REFUNDED" then
        activeRequest = nil
    end
end

local function StripColorCodes(message)
    return (message or ""):gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
end

local function HandleSystemMessage(message)
    message = StripColorCodes(message)

    -- Mak'gora incoming detection
    local challenger = string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to a Mak'gora")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo a muerte")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to an equipment duel")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo por equipamiento")
    if challenger then
        ShowIncomingRequest(challenger, "Mak'gora", true)
        return
    end

    -- Gold incoming detection
    local amount
    challenger, amount = string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Use")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+challenged you to a gold duel for%s+(.+)%.%s+Do you")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Usa")
        or string.match(message or "", "^DuelWager:%s+(.+)%s+te reto a un duelo de oro por%s+(.+)%.%s+Deseas")

    if challenger and amount then
        ShowIncomingRequest(challenger, amount, false)
        return
    end
end

local function OnChatMsgAddon(_, _, prefix, message)
    if prefix == SERVER_PREFIX then
        HandleServerAddonMessage(message)
    end
end

local function OnChatMsgSystem(_, _, message)
    HandleSystemMessage(message)
end

SSDuelWager:SetScript("OnEvent", function(self, event, ...)
    if event == "CHAT_MSG_ADDON" then
        OnChatMsgAddon(self, event, ...)
    elseif event == "CHAT_MSG_SYSTEM" then
        OnChatMsgSystem(self, event, ...)
    elseif event == "PLAYER_LOGIN" then
        InstallDuelTypeSelector()
    end
end)

SSDuelWager:RegisterEvent("CHAT_MSG_ADDON")
SSDuelWager:RegisterEvent("CHAT_MSG_SYSTEM")
SSDuelWager:RegisterEvent("PLAYER_LOGIN")

hooksecurefunc("StaticPopup_Show", function(which)
    if which == "DUEL_REQUESTED" then
        UpdateDuelPopup()
    end
end)

SSDuelWager:SetScript("OnUpdate", function()
    UpdateDuelTypeTooltipPosition()
end)
