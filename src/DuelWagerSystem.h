/*
 * This file is part of the DuelWagerSystem module for AzerothCore.
 */

#ifndef MODULE_DUEL_WAGER_SYSTEM_H
#define MODULE_DUEL_WAGER_SYSTEM_H

#include "Define.h"

namespace DuelWagerSystem
{
struct Settings
{
    bool Enabled = true;
    bool EquipmentEnabled = true;
    uint32 TimeoutSeconds = 60;
    float MaxDistance = 30.0f;
    uint32 EquipmentCorpseDurationSeconds = 1800;
};

Settings const& GetSettings();
void LoadConfig();
void EnsureConfigLoaded();
void PrintStatus(char const* eventName);
bool IsEnabled();
}

#endif
