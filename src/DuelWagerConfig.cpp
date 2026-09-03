/*
 * This file is part of the DuelWagerSystem module for AzerothCore.
 */

#include "DuelWagerSystem.h"
#include "Config.h"
#include "Log.h"

namespace DuelWagerSystem
{
namespace
{
bool ConfigLoaded = false;
Settings CurrentSettings;
}

Settings const& GetSettings()
{
    EnsureConfigLoaded();
    return CurrentSettings;
}

void LoadConfig()
{
    Settings settings;

    settings.Enabled = sConfigMgr->GetOption<bool>(
        "DuelWagerSystem.Enable",
        sConfigMgr->GetOption<bool>("HardcoreSystem.DuelWager.Enable", true, false),
        false);
    settings.EquipmentEnabled = sConfigMgr->GetOption<bool>(
        "DuelWagerSystem.Equipment.Enable",
        true,
        false);
    settings.TimeoutSeconds = sConfigMgr->GetOption<uint32>(
        "DuelWagerSystem.TimeoutSeconds",
        sConfigMgr->GetOption<uint32>("HardcoreSystem.DuelWager.TimeoutSeconds", 60, false),
        false);
    settings.MaxDistance = sConfigMgr->GetOption<float>(
        "DuelWagerSystem.MaxDistance",
        sConfigMgr->GetOption<float>("HardcoreSystem.DuelWager.MaxDistance", 30.0f, false),
        false);
    settings.EquipmentCorpseDurationSeconds = sConfigMgr->GetOption<uint32>(
        "DuelWagerSystem.Equipment.CorpseDurationSeconds",
        1800,
        false);

    CurrentSettings = settings;
    ConfigLoaded = true;
}

void EnsureConfigLoaded()
{
    if (!ConfigLoaded)
        LoadConfig();
}

bool IsEnabled()
{
    return GetSettings().Enabled;
}

void PrintStatus(char const* eventName)
{
    Settings const& settings = GetSettings();

    LOG_INFO("server.loading",
        "DuelWagerSystem: {}. Enable={}, EquipmentEnable={}, Timeout={}s, MaxDistance={}, EquipmentCorpseDuration={}s",
        eventName, settings.Enabled, settings.EquipmentEnabled, settings.TimeoutSeconds, settings.MaxDistance, settings.EquipmentCorpseDurationSeconds);
}
}
