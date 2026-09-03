/*
 * This file is part of the DuelWagerSystem module for AzerothCore.
 */

#include "DuelWagerSystem.h"
#include "WorldScript.h"

class DuelWagerSystemWorldScript : public WorldScript
{
public:
    DuelWagerSystemWorldScript() : WorldScript("DuelWagerSystemWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        DuelWagerSystem::LoadConfig();
        DuelWagerSystem::PrintStatus(reload ? "config reloaded" : "config loaded");
    }

    void OnStartup() override
    {
        DuelWagerSystem::PrintStatus("module startup check passed");
    }
};

void AddDuelWagerSystemWorldScripts()
{
    new DuelWagerSystemWorldScript();
}
