/*
 * This file is part of the DuelWagerSystem module for AzerothCore.
 */

#include "DuelWagerSystem.h"
#include "Chat.h"
#include "Corpse.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "LootMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "SocialMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
uint32 constexpr SpellDuel = 7266;
constexpr std::string_view DuelWagerClientCommandPrefix = "HCDW\tCOMMAND\t";

struct PendingWagerDuel
{
    ObjectGuid ChallengerGuid;
    ObjectGuid TargetGuid;
    uint32 Stake = 0;
    time_t ExpiresAt = 0;
};

struct ActiveWagerDuel
{
    ObjectGuid ChallengerGuid;
    ObjectGuid TargetGuid;
    uint32 Stake = 0;
};

struct PendingEquipmentDuel
{
    ObjectGuid ChallengerGuid;
    ObjectGuid TargetGuid;
    time_t ExpiresAt = 0;
};

struct ActiveEquipmentDuel
{
    ObjectGuid ChallengerGuid;
    ObjectGuid TargetGuid;
};

struct PendingEquipmentDeath
{
    ObjectGuid WinnerGuid;
    ObjectGuid LoserGuid;
    uint32 WinnerAccount = 0;
    std::string WinnerName;
    uint8 WinnerRace = 0;
    uint8 WinnerClass = 0;
    uint8 WinnerLevel = 0;
};

struct EquipmentLootCorpse
{
    ObjectGuid WinnerGuid;
    ObjectGuid LoserGuid;
    uint32 MapId = 0;
    uint32 InstanceId = 0;
    uint64 ExpiresAt = 0;
};

struct EquipmentLootItemData
{
    uint8 Slot = 0;
    uint32 Entry = 0;
    uint32 Count = 0;
    int32 RandomPropertyId = 0;
    uint32 RandomSuffix = 0;
};

struct DuelWagerKey
{
    uint64 First = 0;
    uint64 Second = 0;

    bool operator==(DuelWagerKey const& other) const
    {
        return First == other.First && Second == other.Second;
    }
};

struct DuelWagerKeyHash
{
    std::size_t operator()(DuelWagerKey const& key) const
    {
        return std::hash<uint64>{}(key.First) ^ (std::hash<uint64>{}(key.Second) << 1);
    }
};

std::unordered_map<ObjectGuid, PendingWagerDuel> PendingByTarget;
std::unordered_map<ObjectGuid, ObjectGuid> PendingTargetByChallenger;
std::unordered_map<DuelWagerKey, ActiveWagerDuel, DuelWagerKeyHash> ActiveWagers;
std::unordered_map<ObjectGuid, PendingEquipmentDuel> PendingEquipmentByTarget;
std::unordered_map<ObjectGuid, ObjectGuid> PendingEquipmentTargetByChallenger;
std::unordered_map<DuelWagerKey, ActiveEquipmentDuel, DuelWagerKeyHash> ActiveEquipmentDuels;
std::unordered_map<DuelWagerKey, uint64, DuelWagerKeyHash> DuelStartedAtMs;
std::unordered_map<ObjectGuid, PendingEquipmentDeath> PendingEquipmentDeaths;
std::unordered_map<ObjectGuid, EquipmentLootCorpse> EquipmentLootCorpses;
uint32 PendingCleanupTimer = 5 * IN_MILLISECONDS;

// ---------------------------------------------------------------------------
// Equipment-duel persistence (Custom database)
// ---------------------------------------------------------------------------
constexpr char const* DuelWagerDatabase = "Custom";
constexpr char const* DuelEquipmentTable = "`Custom`.`duel_equipment`";
constexpr char const* DuelEquipmentItemsTable = "`Custom`.`duel_equipment_items`";
constexpr char const* DuelGoldTable = "`Custom`.`duel_gold_history`";
constexpr char const* DuelNormalTable = "`Custom`.`duel_normal_history`";

std::string EscapeDuelDbString(std::string value)
{
    CharacterDatabase.EscapeString(value);
    return value;
}

uint32 GetDuelPlayerAccountId(Player const* player)
{
    return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
}

std::string GuidKeyString(ObjectGuid guid)
{
    return std::to_string(guid.GetRawValue());
}

void EnsureDuelEquipmentTables()
{
    CharacterDatabase.DirectExecute(
        "CREATE DATABASE IF NOT EXISTS `{}` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci",
        DuelWagerDatabase);

    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS {} ("
        "`corpse_guid` varchar(128) NOT NULL,"
        "`created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "`expires_at` timestamp NULL DEFAULT NULL,"
        "`despawned_at` timestamp NULL DEFAULT NULL,"
        "`status` varchar(16) NOT NULL DEFAULT 'spawned',"
        "`winner_guid` int unsigned NOT NULL DEFAULT '0',"
        "`winner_account` int unsigned NOT NULL DEFAULT '0',"
        "`winner_name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',"
        "`winner_race` tinyint unsigned NOT NULL DEFAULT '0',"
        "`winner_class` tinyint unsigned NOT NULL DEFAULT '0',"
        "`winner_level` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_guid` int unsigned NOT NULL DEFAULT '0',"
        "`loser_account` int unsigned NOT NULL DEFAULT '0',"
        "`loser_name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',"
        "`loser_race` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_class` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_level` tinyint unsigned NOT NULL DEFAULT '0',"
        "`map_id` smallint unsigned NOT NULL DEFAULT '0',"
        "`zone_id` int unsigned NOT NULL DEFAULT '0',"
        "`area_id` int unsigned NOT NULL DEFAULT '0',"
        "`position_x` float NOT NULL DEFAULT '0',"
        "`position_y` float NOT NULL DEFAULT '0',"
        "`position_z` float NOT NULL DEFAULT '0',"
        "`orientation` float NOT NULL DEFAULT '0',"
        "`item_count` int unsigned NOT NULL DEFAULT '0',"
        "`looted_count` int unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`corpse_guid`),"
        "KEY `idx_winner` (`winner_guid`),"
        "KEY `idx_loser` (`loser_guid`),"
        "KEY `idx_created_at` (`created_at`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Equipment duel skeleton events'",
        DuelEquipmentTable);

    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS {} ("
        "`id` bigint unsigned NOT NULL AUTO_INCREMENT,"
        "`corpse_guid` varchar(128) NOT NULL,"
        "`equip_slot` tinyint unsigned NOT NULL DEFAULT '0',"
        "`item_entry` int unsigned NOT NULL DEFAULT '0',"
        "`item_name` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '',"
        "`item_count` int unsigned NOT NULL DEFAULT '0',"
        "`random_property_id` int NOT NULL DEFAULT '0',"
        "`random_suffix` int unsigned NOT NULL DEFAULT '0',"
        "`quality` int unsigned NOT NULL DEFAULT '0',"
        "`item_class` int unsigned NOT NULL DEFAULT '0',"
        "`item_subclass` int unsigned NOT NULL DEFAULT '0',"
        "`inventory_type` int unsigned NOT NULL DEFAULT '0',"
        "`item_level` int unsigned NOT NULL DEFAULT '0',"
        "`required_level` int unsigned NOT NULL DEFAULT '0',"
        "`is_looted` tinyint unsigned NOT NULL DEFAULT '0',"
        "`looted_at` timestamp NULL DEFAULT NULL,"
        "`looter_guid` int unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`id`),"
        "KEY `idx_corpse_guid` (`corpse_guid`),"
        "KEY `idx_item_entry` (`item_entry`),"
        "KEY `idx_is_looted` (`is_looted`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Equipment duel dropped gear'",
        DuelEquipmentItemsTable);

    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS {} ("
        "`id` bigint unsigned NOT NULL AUTO_INCREMENT,"
        "`created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "`winner_guid` int unsigned NOT NULL DEFAULT '0',"
        "`winner_name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',"
        "`winner_race` tinyint unsigned NOT NULL DEFAULT '0',"
        "`winner_class` tinyint unsigned NOT NULL DEFAULT '0',"
        "`winner_level` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_guid` int unsigned NOT NULL DEFAULT '0',"
        "`loser_name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',"
        "`loser_race` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_class` tinyint unsigned NOT NULL DEFAULT '0',"
        "`loser_level` tinyint unsigned NOT NULL DEFAULT '0',"
        "`stake_copper` int unsigned NOT NULL DEFAULT '0',"
        "`prize_copper` int unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`id`), KEY `idx_gold_winner` (`winner_guid`),"
        "KEY `idx_gold_loser` (`loser_guid`), KEY `idx_gold_created` (`created_at`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Completed gold wager duels'",
        DuelGoldTable);
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS {} ("
        "`id` bigint unsigned NOT NULL AUTO_INCREMENT,`created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "`duration_ms` int unsigned NOT NULL DEFAULT '0',`winner_guid` int unsigned NOT NULL,`winner_name` varchar(12) NOT NULL,"
        "`winner_race` tinyint unsigned NOT NULL,`winner_class` tinyint unsigned NOT NULL,`winner_level` tinyint unsigned NOT NULL,"
        "`loser_guid` int unsigned NOT NULL,`loser_name` varchar(12) NOT NULL,`loser_race` tinyint unsigned NOT NULL,"
        "`loser_class` tinyint unsigned NOT NULL,`loser_level` tinyint unsigned NOT NULL,PRIMARY KEY (`id`),"
        "KEY `idx_normal_winner` (`winner_guid`),KEY `idx_normal_loser` (`loser_guid`),KEY `idx_normal_created` (`created_at`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Completed normal duels'", DuelNormalTable);
}

void RecordNormalDuel(Player* winner, Player* loser, uint32 durationMs)
{
    CharacterDatabase.DirectExecute(
        "INSERT INTO {} (`duration_ms`,`winner_guid`,`winner_name`,`winner_race`,`winner_class`,`winner_level`,`loser_guid`,`loser_name`,`loser_race`,`loser_class`,`loser_level`) VALUES ({},{},'{}',{},{},{},{},'{}',{},{},{})",
        DuelNormalTable, durationMs, winner->GetGUID().GetCounter(), EscapeDuelDbString(winner->GetName()), uint32(winner->getRace()), uint32(winner->getClass()), uint32(winner->GetLevel()), loser->GetGUID().GetCounter(), EscapeDuelDbString(loser->GetName()), uint32(loser->getRace()), uint32(loser->getClass()), uint32(loser->GetLevel()));
}

void RecordGoldDuel(Player* winner, Player* loser, uint32 stake, uint32 prize)
{
    if (!winner || !loser)
        return;

    CharacterDatabase.DirectExecute(
        "INSERT INTO {} (`winner_guid`, `winner_name`, `winner_race`, `winner_class`, `winner_level`, "
        "`loser_guid`, `loser_name`, `loser_race`, `loser_class`, `loser_level`, `stake_copper`, `prize_copper`) "
        "VALUES ({}, '{}', {}, {}, {}, {}, '{}', {}, {}, {}, {}, {})",
        DuelGoldTable,
        winner->GetGUID().GetCounter(), EscapeDuelDbString(winner->GetName()), uint32(winner->getRace()),
        uint32(winner->getClass()), uint32(winner->GetLevel()), loser->GetGUID().GetCounter(),
        EscapeDuelDbString(loser->GetName()), uint32(loser->getRace()), uint32(loser->getClass()),
        uint32(loser->GetLevel()), stake, prize);
}

void RecordDuelEquipmentCorpse(Corpse* bones, Player* loser, PendingEquipmentDeath const& pending,
    std::vector<EquipmentLootItemData> const& items, uint64 expiresAt)
{
    if (!bones || !loser)
        return;

    uint32 zoneId = 0;
    uint32 areaId = 0;
    loser->GetZoneAndAreaId(zoneId, areaId);

    std::string const corpseGuid = GuidKeyString(bones->GetGUID());
    std::string const winnerName = EscapeDuelDbString(pending.WinnerName);
    std::string const loserName = EscapeDuelDbString(loser->GetName());

    CharacterDatabase.DirectExecute(
        "REPLACE INTO {} "
        "(`corpse_guid`, `created_at`, `expires_at`, `status`, "
        "`winner_guid`, `winner_account`, `winner_name`, `winner_race`, `winner_class`, `winner_level`, "
        "`loser_guid`, `loser_account`, `loser_name`, `loser_race`, `loser_class`, `loser_level`, "
        "`map_id`, `zone_id`, `area_id`, `position_x`, `position_y`, `position_z`, `orientation`, `item_count`, `looted_count`) "
        "VALUES ('{}', FROM_UNIXTIME({}), FROM_UNIXTIME({}), 'spawned', "
        "{}, {}, '{}', {}, {}, {}, "
        "{}, {}, '{}', {}, {}, {}, "
        "{}, {}, {}, {}, {}, {}, {}, {}, 0)",
        DuelEquipmentTable, corpseGuid,
        static_cast<uint64>(GameTime::GetGameTime().count()), expiresAt,
        pending.WinnerGuid.GetCounter(), pending.WinnerAccount, winnerName,
        uint32(pending.WinnerRace), uint32(pending.WinnerClass), uint32(pending.WinnerLevel),
        loser->GetGUID().GetCounter(), GetDuelPlayerAccountId(loser), loserName,
        uint32(loser->getRace()), uint32(loser->getClass()), uint32(loser->GetLevel()),
        bones->GetMapId(), zoneId, areaId,
        bones->GetPositionX(), bones->GetPositionY(), bones->GetPositionZ(), bones->GetOrientation(),
        uint32(items.size()));

    for (EquipmentLootItemData const& data : items)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(data.Entry);
        std::string const itemName = EscapeDuelDbString(proto ? proto->Name1 : std::string());

        CharacterDatabase.DirectExecute(
            "INSERT INTO {} "
            "(`corpse_guid`, `equip_slot`, `item_entry`, `item_name`, `item_count`, `random_property_id`, `random_suffix`, "
            "`quality`, `item_class`, `item_subclass`, `inventory_type`, `item_level`, `required_level`) "
            "VALUES ('{}', {}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {})",
            DuelEquipmentItemsTable, corpseGuid, uint32(data.Slot), data.Entry, itemName, data.Count,
            data.RandomPropertyId, data.RandomSuffix,
            proto ? uint32(proto->Quality) : 0u, proto ? uint32(proto->Class) : 0u,
            proto ? uint32(proto->SubClass) : 0u, proto ? uint32(proto->InventoryType) : 0u,
            proto ? proto->ItemLevel : 0u, proto ? proto->RequiredLevel : 0u);
    }
}

void RecordDuelEquipmentItemLooted(ObjectGuid corpseGuid, uint32 itemEntry, ObjectGuid looterGuid)
{
    std::string const corpse = GuidKeyString(corpseGuid);
    CharacterDatabase.DirectExecute(
        "UPDATE {} SET `is_looted` = 1, `looted_at` = FROM_UNIXTIME({}), `looter_guid` = {} "
        "WHERE `corpse_guid` = '{}' AND `item_entry` = {} AND `is_looted` = 0 LIMIT 1",
        DuelEquipmentItemsTable, static_cast<uint64>(GameTime::GetGameTime().count()),
        looterGuid.GetCounter(), corpse, itemEntry);

    CharacterDatabase.DirectExecute(
        "UPDATE {} SET `looted_count` = `looted_count` + 1 WHERE `corpse_guid` = '{}'",
        DuelEquipmentTable, corpse);

    CharacterDatabase.DirectExecute(
        "UPDATE {} SET `status` = 'looted' WHERE `corpse_guid` = '{}' AND `looted_count` >= `item_count` AND `status` <> 'looted'",
        DuelEquipmentTable, corpse);
}

void MarkDuelEquipmentCorpseStatus(ObjectGuid corpseGuid, char const* status)
{
    CharacterDatabase.DirectExecute(
        "UPDATE {} SET `status` = '{}', `despawned_at` = FROM_UNIXTIME({}) WHERE `corpse_guid` = '{}' AND `status` NOT IN ('looted')",
        DuelEquipmentTable, status, static_cast<uint64>(GameTime::GetGameTime().count()),
        GuidKeyString(corpseGuid));
}

DuelWagerKey MakeWagerKey(ObjectGuid first, ObjectGuid second)
{
    uint64 firstRaw = first.GetRawValue();
    uint64 secondRaw = second.GetRawValue();
    if (secondRaw < firstRaw)
        std::swap(firstRaw, secondRaw);

    return { firstRaw, secondRaw };
}

bool IsProtectedEquipmentSlot(uint8 slot)
{
    return slot == EQUIPMENT_SLOT_MAINHAND ||
        slot == EQUIPMENT_SLOT_OFFHAND ||
        slot == EQUIPMENT_SLOT_RANGED;
}

std::vector<EquipmentLootItemData> CollectEquipmentDuelItems(Player* player)
{
    std::vector<EquipmentLootItemData> items;
    if (!player)
        return items;

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (IsProtectedEquipmentSlot(slot))
            continue;

        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        EquipmentLootItemData data;
        data.Slot = slot;
        data.Entry = item->GetEntry();
        data.Count = item->GetCount();
        data.RandomPropertyId = item->GetItemRandomPropertyId();
        data.RandomSuffix = item->GetItemSuffixFactor();
        items.push_back(data);
    }

    return items;
}

void RefreshCorpseLoot(Loot& loot, ObjectGuid corpseGuid)
{
    uint8 unlootedCount = 0;
    for (LootItem& item : loot.items)
    {
        item.is_blocked = false;
        item.rollWinnerGUID = ObjectGuid::Empty;
        if (!item.is_looted && unlootedCount < std::numeric_limits<uint8>::max())
            ++unlootedCount;
    }

    loot.unlootedCount = unlootedCount;
    loot.loot_type = LOOT_CORPSE;
    loot.suppressAchievementUpdates = true;
    loot.sourceWorldObjectGUID = corpseGuid;
    loot.sourceGameObject = nullptr;
    loot.roundRobinPlayer.Clear();
}

bool AddEquipmentLootToCorpse(Corpse* corpse, EquipmentLootItemData const& data)
{
    if (!corpse || !data.Entry || !data.Count)
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(data.Entry);
    if (!proto)
        return false;

    uint32 remaining = data.Count;
    uint32 added = 0;
    while (remaining && corpse->loot.items.size() < MAX_NR_LOOT_ITEMS)
    {
        uint32 const stackCount = std::max<uint32>(
            1, std::min<uint32>(
                   remaining,
                   std::min<uint32>(proto->GetMaxStackSize(), std::numeric_limits<uint8>::max())));

        LootStoreItem storeItem(
            data.Entry, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0,
            stackCount, uint8(stackCount));
        std::size_t const firstNewItem = corpse->loot.items.size();
        corpse->loot.AddItem(storeItem);

        for (std::size_t index = firstNewItem; index < corpse->loot.items.size(); ++index)
        {
            corpse->loot.items[index].randomPropertyId = data.RandomPropertyId;
            corpse->loot.items[index].randomSuffix = data.RandomSuffix;
            ++added;
        }

        remaining -= stackCount;
    }

    RefreshCorpseLoot(corpse->loot, corpse->GetGUID());
    return added > 0 && remaining == 0;
}

bool RemoveEquipmentDuelItem(Player* loser, EquipmentLootItemData const& data)
{
    if (!loser || !data.Entry)
        return false;

    Item* item = loser->GetItemByPos(INVENTORY_SLOT_BAG_0, data.Slot);
    if (!item || item->GetEntry() != data.Entry || item->IsInTrade())
        return false;

    loser->DestroyItem(INVENTORY_SLOT_BAG_0, data.Slot, true);
    return true;
}

void DespawnEquipmentLootCorpse(ObjectGuid corpseGuid)
{
    auto itr = EquipmentLootCorpses.find(corpseGuid);
    if (itr == EquipmentLootCorpses.end())
        return;

    if (Map* map = sMapMgr->FindMap(itr->second.MapId, itr->second.InstanceId))
    {
        if (Corpse* corpse = map->GetCorpse(corpseGuid))
        {
            corpse->loot.clear();
            corpse->RemoveFlag(CORPSE_FIELD_DYNAMIC_FLAGS, CORPSE_DYNFLAG_LOOTABLE);
            corpse->RemoveFlag(CORPSE_FIELD_FLAGS, CORPSE_FLAG_LOOTABLE);
            corpse->ForceValuesUpdateAtIndex(CORPSE_FIELD_DYNAMIC_FLAGS);
            corpse->ForceValuesUpdateAtIndex(CORPSE_FIELD_FLAGS);
        }
    }

    MarkDuelEquipmentCorpseStatus(corpseGuid, "despawned");
    EquipmentLootCorpses.erase(itr);
}

// Despawn and untrack every equipment skeleton that still belongs to a given
// loser. Called when that loser is about to drop a fresh skeleton so a leftover
// one from a previous duel can never block or collide with the new one. The new
// skeleton keeps its own full duration; only the player's stale ones are cleared.
void DespawnPlayerEquipmentLootCorpses(ObjectGuid loserGuid)
{
    std::vector<ObjectGuid> staleCorpses;
    for (auto const& [corpseGuid, tracked] : EquipmentLootCorpses)
        if (tracked.LoserGuid == loserGuid)
            staleCorpses.push_back(corpseGuid);

    for (ObjectGuid const& corpseGuid : staleCorpses)
        DespawnEquipmentLootCorpse(corpseGuid);
}

std::vector<std::string> ParseCommandTokens(std::string_view args)
{
    std::vector<std::string> tokens;

    for (size_t index = 0; index < args.size();)
    {
        while (index < args.size() && std::isspace(static_cast<unsigned char>(args[index])))
            ++index;

        if (index >= args.size())
            break;

        char const quote = args[index];
        if (quote == '"' || quote == '\'')
        {
            ++index;
            size_t const start = index;
            while (index < args.size() && args[index] != quote)
                ++index;

            tokens.emplace_back(args.substr(start, index - start));
            if (index < args.size())
                ++index;
            continue;
        }

        size_t const start = index;
        while (index < args.size() && !std::isspace(static_cast<unsigned char>(args[index])))
            ++index;

        tokens.emplace_back(args.substr(start, index - start));
    }

    return tokens;
}

std::string ExtractPlayerName(std::string token)
{
    size_t const marker = token.find("Hplayer:");
    if (marker != std::string::npos)
    {
        size_t const nameStart = marker + 8;
        size_t const nameEnd = token.find('|', nameStart);
        if (nameEnd != std::string::npos && nameEnd > nameStart)
            token = token.substr(nameStart, nameEnd - nameStart);
    }

    return normalizePlayerName(token) ? token : std::string();
}

std::optional<uint32> ParseMoneyAmount(std::string amountText)
{
    amountText.erase(std::remove(amountText.begin(), amountText.end(), ','), amountText.end());
    amountText.erase(std::remove_if(amountText.begin(), amountText.end(), [](unsigned char c)
    {
        return std::isspace(c);
    }), amountText.end());

    if (amountText.empty())
        return std::nullopt;

    for (char& c : amountText)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    uint64 copper = 0;
    uint64 number = 0;
    bool hasDigits = false;
    bool hasUnit = false;

    auto addNumber = [&](uint64 multiplier)
    {
        copper += number * multiplier;
        number = 0;
        hasDigits = false;
    };

    for (char c : amountText)
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            hasDigits = true;
            number = (number * 10) + uint64(c - '0');
            if (number > MAX_MONEY_AMOUNT)
                return std::nullopt;
            continue;
        }

        if (!hasDigits)
            return std::nullopt;

        switch (c)
        {
            case 'g':
                hasUnit = true;
                addNumber(GOLD);
                break;
            case 's':
                hasUnit = true;
                addNumber(SILVER);
                break;
            case 'c':
                hasUnit = true;
                addNumber(COPPER);
                break;
            default:
                return std::nullopt;
        }

        if (copper > MAX_MONEY_AMOUNT / 2)
            return std::nullopt;
    }

    if (hasDigits)
        copper += number * (hasUnit ? COPPER : GOLD);

    if (!copper || copper > MAX_MONEY_AMOUNT / 2)
        return std::nullopt;

    return static_cast<uint32>(copper);
}

std::string FormatMoney(uint32 copper)
{
    uint32 const gold = copper / GOLD;
    copper %= GOLD;
    uint32 const silver = copper / SILVER;
    copper %= SILVER;

    auto coin = [](uint32 amount, char const* icon)
    {
        return Acore::StringFormat("{}|TInterface\\MoneyFrame\\{}:0:0:2:0|t", amount, icon);
    };

    std::string text;
    if (gold)
        text += coin(gold, "UI-GoldIcon");
    if (silver)
    {
        if (!text.empty())
            text += " ";
        text += coin(silver, "UI-SilverIcon");
    }
    if (copper || text.empty())
    {
        if (!text.empty())
            text += " ";
        text += coin(copper, "UI-CopperIcon");
    }

    return text;
}

char const* SelectLocalizedText(LocaleConstant locale, char const* english, char const* spanish)
{
    switch (locale)
    {
        case LOCALE_esES:
        case LOCALE_esMX:
            return spanish;
        case LOCALE_enUS:
        case LOCALE_koKR:
        case LOCALE_frFR:
        case LOCALE_deDE:
        case LOCALE_zhCN:
        case LOCALE_zhTW:
        case LOCALE_ruRU:
        default:
            return english;
    }
}

char const* DuelWagerMessageText(char const* text)
{
    static thread_local std::string messageText;
    std::string_view message = text ? std::string_view(text) : std::string_view();
    std::string_view constexpr prefix = "DuelWager: ";
    if (message.substr(0, prefix.size()) == prefix)
        message.remove_prefix(prefix.size());

    messageText.assign(message.data(), message.size());
    return messageText.c_str();
}

char const* Tr(Player const* player, char const* english, char const* spanish)
{
    if (!player || !player->GetSession())
        return DuelWagerMessageText(english);

    return DuelWagerMessageText(SelectLocalizedText(player->GetSession()->GetSessionDbLocaleIndex(), english, spanish));
}

char const* Tr(ChatHandler const& handler, char const* english, char const* spanish)
{
    return DuelWagerMessageText(SelectLocalizedText(LocaleConstant(handler.GetSessionDbLocaleIndex()), english, spanish));
}

bool IsDuelAllowedInCurrentArea(Player* player)
{
    if (!player)
        return false;

    AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId());
    return !area || (area->flags & AREA_FLAG_ALLOW_DUELS);
}

bool HasEnoughRoomForPossibleWin(Player* player, uint32 stake)
{
    return player && player->GetMoney() <= uint32(MAX_MONEY_AMOUNT - stake);
}

bool ValidateWagerDuelPlayers(Player* challenger, Player* target, uint32 stake, ChatHandler& handler, bool allowExistingDuel = false)
{
    if (!challenger || !target)
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: the player is no longer connected.", "DuelWager: el jugador ya no esta conectado."));
        return false;
    }

    if (challenger == target)
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: you cannot challenge yourself.", "DuelWager: no puedes retarte a ti mismo."));
        return false;
    }

    if (!allowExistingDuel && (challenger->duel || target->duel))
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: one of the players is already in a duel.", "DuelWager: uno de los jugadores ya esta en duelo."));
        return false;
    }

    if (!challenger->IsAlive() || !target->IsAlive())
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: both players must be alive.", "DuelWager: ambos jugadores deben estar vivos."));
        return false;
    }

    if (challenger->IsInFlight() || target->IsInFlight())
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: you cannot start a gold duel while either player is flying.", "DuelWager: no puedes iniciar un duelo por oro mientras alguien esta volando."));
        return false;
    }

    if (challenger->GetMap() != target->GetMap() || !challenger->IsWithinDistInMap(target, DuelWagerSystem::GetSettings().MaxDistance))
    {
        handler.PSendSysMessage(Tr(handler,
            "DuelWager: both players must be nearby to accept the gold duel. Max distance: {} yards.",
            "DuelWager: deben estar cerca para aceptar el duelo por oro. Distancia maxima: {} yardas."),
            DuelWagerSystem::GetSettings().MaxDistance);
        return false;
    }

    if (!IsDuelAllowedInCurrentArea(challenger) || !IsDuelAllowedInCurrentArea(target))
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: duels are not allowed in this zone.", "DuelWager: los duelos no estan permitidos en esta zona."));
        return false;
    }

    if (target->GetSocial() && target->GetSocial()->HasIgnore(challenger->GetGUID()))
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: that player is ignoring you.", "DuelWager: ese jugador te tiene ignorado."));
        return false;
    }

    if (!challenger->HasEnoughMoney(stake))
    {
        handler.PSendSysMessage(Tr(handler,
            "DuelWager: {} does not have enough gold to wager {}.",
            "DuelWager: {} no tiene suficiente oro para apostar {}."),
            challenger->GetName(), FormatMoney(stake));
        return false;
    }

    if (!target->HasEnoughMoney(stake))
    {
        handler.PSendSysMessage(Tr(handler,
            "DuelWager: {} does not have enough gold to wager {}.",
            "DuelWager: {} no tiene suficiente oro para apostar {}."),
            target->GetName(), FormatMoney(stake));
        return false;
    }

    if (!HasEnoughRoomForPossibleWin(challenger, stake) || !HasEnoughRoomForPossibleWin(target, stake))
    {
        handler.SendSysMessage(Tr(handler,
            "DuelWager: one of the players is too close to the gold cap to receive the prize.",
            "DuelWager: uno de los jugadores esta demasiado cerca del limite de oro para recibir el premio."));
        return false;
    }

    return true;
}

[[maybe_unused]] void SendPlayerMessage(ObjectGuid guid, std::string const& message)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        ChatHandler(player->GetSession()).SendSysMessage(message);
}

void SendLocalizedPlayerMessage(ObjectGuid guid, char const* english, char const* spanish)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        ChatHandler(player->GetSession()).SendSysMessage(Tr(player, english, spanish));
}

void SendDuelWagerAddonMessage(Player* player, std::string const& payload)
{
    if (!player || !player->GetSession())
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player->GetGUID(), player->GetGUID(), "HCDW\t" + payload, 0);
    player->SendDirectMessage(&data);
}

void SendDuelWagerAddonMessage(ObjectGuid guid, std::string const& payload)
{
    SendDuelWagerAddonMessage(ObjectAccessor::FindConnectedPlayer(guid), payload);
}

void RemovePendingForTarget(ObjectGuid targetGuid)
{
    auto pendingItr = PendingByTarget.find(targetGuid);
    if (pendingItr == PendingByTarget.end())
        return;

    PendingTargetByChallenger.erase(pendingItr->second.ChallengerGuid);
    PendingByTarget.erase(pendingItr);
}

void RemovePendingForChallenger(ObjectGuid challengerGuid)
{
    auto targetItr = PendingTargetByChallenger.find(challengerGuid);
    if (targetItr == PendingTargetByChallenger.end())
        return;

    PendingByTarget.erase(targetItr->second);
    PendingTargetByChallenger.erase(targetItr);
}

void RemovePendingEquipmentForTarget(ObjectGuid targetGuid)
{
    auto pendingItr = PendingEquipmentByTarget.find(targetGuid);
    if (pendingItr == PendingEquipmentByTarget.end())
        return;

    PendingEquipmentTargetByChallenger.erase(pendingItr->second.ChallengerGuid);
    PendingEquipmentByTarget.erase(pendingItr);
}

void RemovePendingEquipmentForChallenger(ObjectGuid challengerGuid)
{
    auto targetItr = PendingEquipmentTargetByChallenger.find(challengerGuid);
    if (targetItr == PendingEquipmentTargetByChallenger.end())
        return;

    PendingEquipmentByTarget.erase(targetItr->second);
    PendingEquipmentTargetByChallenger.erase(targetItr);
}

Player* FindKnownOrConnectedPlayer(ObjectGuid guid, Player* first = nullptr, Player* second = nullptr)
{
    if (first && first->GetGUID() == guid)
        return first;

    if (second && second->GetGUID() == guid)
        return second;

    return ObjectAccessor::FindConnectedPlayer(guid);
}

std::optional<PendingWagerDuel> FindPendingForPair(ObjectGuid first, ObjectGuid second)
{
    auto firstAsTarget = PendingByTarget.find(first);
    if (firstAsTarget != PendingByTarget.end() && firstAsTarget->second.ChallengerGuid == second)
        return firstAsTarget->second;

    auto secondAsTarget = PendingByTarget.find(second);
    if (secondAsTarget != PendingByTarget.end() && secondAsTarget->second.ChallengerGuid == first)
        return secondAsTarget->second;

    return std::nullopt;
}

std::optional<PendingEquipmentDuel> FindPendingEquipmentForPair(ObjectGuid first, ObjectGuid second)
{
    auto firstAsTarget = PendingEquipmentByTarget.find(first);
    if (firstAsTarget != PendingEquipmentByTarget.end() && firstAsTarget->second.ChallengerGuid == second)
        return firstAsTarget->second;

    auto secondAsTarget = PendingEquipmentByTarget.find(second);
    if (secondAsTarget != PendingEquipmentByTarget.end() && secondAsTarget->second.ChallengerGuid == first)
        return secondAsTarget->second;

    return std::nullopt;
}

bool HasNormalDuelBetween(Player* first, Player* second)
{
    return first && second && first->duel && second->duel &&
        first->duel->Opponent == second && second->duel->Opponent == first;
}

bool HasPendingNormalDuelBetween(Player* challenger, Player* target)
{
    return HasNormalDuelBetween(challenger, target) &&
        challenger->duel->State == DUEL_STATE_CHALLENGED &&
        target->duel->State == DUEL_STATE_CHALLENGED;
}

void CancelNormalDuelForPending(PendingWagerDuel const& pending, Player* preferredCanceller = nullptr)
{
    Player* challenger = FindKnownOrConnectedPlayer(pending.ChallengerGuid, preferredCanceller);
    Player* target = FindKnownOrConnectedPlayer(pending.TargetGuid, preferredCanceller);

    if (!HasNormalDuelBetween(challenger, target))
        return;

    Player* canceller = preferredCanceller ? preferredCanceller : target;
    if (!canceller || !canceller->duel)
        canceller = target && target->duel ? target : challenger;

    if (canceller && canceller->duel && canceller->duel->State != DUEL_STATE_COMPLETED)
        canceller->DuelComplete(DUEL_INTERRUPTED);
}

void ExpirePendingIfNeeded(ObjectGuid targetGuid)
{
    auto pendingItr = PendingByTarget.find(targetGuid);
    if (pendingItr == PendingByTarget.end())
        return;

    if (pendingItr->second.ExpiresAt > GameTime::GetGameTime().count())
        return;

    PendingWagerDuel const pending = pendingItr->second;

    SendLocalizedPlayerMessage(pending.ChallengerGuid,
        "DuelWager: your gold duel request expired.",
        "DuelWager: tu reto de duelo por oro expiro.");
    SendLocalizedPlayerMessage(pending.TargetGuid,
        "DuelWager: the gold duel request expired.",
        "DuelWager: el reto de duelo por oro expiro.");
    SendDuelWagerAddonMessage(pending.ChallengerGuid, "EXPIRED");
    SendDuelWagerAddonMessage(pending.TargetGuid, "EXPIRED");
    RemovePendingForTarget(targetGuid);
    CancelNormalDuelForPending(pending);
}

void ExpireAllPendingRequests()
{
    time_t const now = GameTime::GetGameTime().count();
    std::vector<ObjectGuid> expiredTargets;

    for (auto const& [targetGuid, pending] : PendingByTarget)
        if (pending.ExpiresAt <= now)
            expiredTargets.push_back(targetGuid);

    for (ObjectGuid const& targetGuid : expiredTargets)
        ExpirePendingIfNeeded(targetGuid);
}

void ExpirePendingEquipmentIfNeeded(ObjectGuid targetGuid)
{
    auto pendingItr = PendingEquipmentByTarget.find(targetGuid);
    if (pendingItr == PendingEquipmentByTarget.end())
        return;

    if (pendingItr->second.ExpiresAt > GameTime::GetGameTime().count())
        return;

    PendingEquipmentDuel const pending = pendingItr->second;

    SendLocalizedPlayerMessage(pending.ChallengerGuid,
        "DuelWager: your equipment duel request expired.",
        "DuelWager: tu reto de duelo por equipamiento expiro.");
    SendLocalizedPlayerMessage(pending.TargetGuid,
        "DuelWager: the equipment duel request expired.",
        "DuelWager: el reto de duelo por equipamiento expiro.");
    SendDuelWagerAddonMessage(pending.ChallengerGuid, "EQUIPMENT_CANCELLED");
    SendDuelWagerAddonMessage(pending.TargetGuid, "EQUIPMENT_CANCELLED");
    RemovePendingEquipmentForTarget(targetGuid);
}

void ExpireAllPendingEquipmentRequests()
{
    time_t const now = GameTime::GetGameTime().count();
    std::vector<ObjectGuid> expiredTargets;

    for (auto const& [targetGuid, pending] : PendingEquipmentByTarget)
        if (pending.ExpiresAt <= now)
            expiredTargets.push_back(targetGuid);

    for (ObjectGuid const& targetGuid : expiredTargets)
        ExpirePendingEquipmentIfNeeded(targetGuid);
}

std::optional<DuelWagerKey> FindActiveWagerKeyForPlayer(ObjectGuid guid)
{
    for (auto const& [key, wager] : ActiveWagers)
        if (wager.ChallengerGuid == guid || wager.TargetGuid == guid)
            return key;

    return std::nullopt;
}

std::optional<DuelWagerKey> FindActiveEquipmentKeyForPlayer(ObjectGuid guid)
{
    for (auto const& [key, duel] : ActiveEquipmentDuels)
        if (duel.ChallengerGuid == guid || duel.TargetGuid == guid)
            return key;

    return std::nullopt;
}

bool StartEquipmentChallenge(Player* challenger, Player* target, ChatHandler& handler)
{
    if (!DuelWagerSystem::GetSettings().EquipmentEnabled)
    {
        handler.SendSysMessage(Tr(handler, "DuelWager: equipment duels are disabled.", "DuelWager: los duelos por equipamiento estan desactivados."));
        return false;
    }

    if (!ValidateWagerDuelPlayers(challenger, target, 0, handler))
        return false;

    ExpirePendingEquipmentIfNeeded(target->GetGUID());

    auto incomingItr = PendingEquipmentByTarget.find(target->GetGUID());
    if (incomingItr != PendingEquipmentByTarget.end() && incomingItr->second.ChallengerGuid != challenger->GetGUID())
    {
        handler.PSendSysMessage(Tr(handler,
            "DuelWager: {} already has a pending equipment duel request.",
            "DuelWager: {} ya tiene un reto de duelo por equipamiento pendiente."), target->GetName());
        return false;
    }

    auto outgoingItr = PendingEquipmentTargetByChallenger.find(challenger->GetGUID());
    if (outgoingItr != PendingEquipmentTargetByChallenger.end())
    {
        if (Player* oldTarget = ObjectAccessor::FindConnectedPlayer(outgoingItr->second))
        {
            ChatHandler(oldTarget->GetSession()).PSendSysMessage(Tr(oldTarget,
                "DuelWager: {} cancelled their equipment duel request.",
                "DuelWager: {} cancelo su reto de duelo por equipamiento."), challenger->GetName());
        }

        RemovePendingEquipmentForChallenger(challenger->GetGUID());
    }

    PendingEquipmentDuel pending;
    pending.ChallengerGuid = challenger->GetGUID();
    pending.TargetGuid = target->GetGUID();
    pending.ExpiresAt = GameTime::GetGameTime().count() + DuelWagerSystem::GetSettings().TimeoutSeconds;

    PendingEquipmentByTarget[target->GetGUID()] = pending;
    PendingEquipmentTargetByChallenger[challenger->GetGUID()] = target->GetGUID();

    SpellCastResult const castResult = challenger->CastSpell(target, SpellDuel, false);
    if (castResult != SPELL_CAST_OK || !HasPendingNormalDuelBetween(challenger, target))
    {
        RemovePendingEquipmentForTarget(target->GetGUID());
        handler.PSendSysMessage(Tr(handler,
            "DuelWager: could not start the equipment duel request. Code: {}.",
            "DuelWager: no se pudo iniciar el reto de duelo por equipamiento. Codigo: {}."), uint32(castResult));
        return false;
    }

    ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
        "DuelWager: {} challenged you to an equipment duel. The loser will forfeit their equipped items.",
        "DuelWager: {} te reto a un duelo por equipamiento. El perdedor perdera sus objetos equipados."),
        challenger->GetName());
    SendDuelWagerAddonMessage(target, Acore::StringFormat("EQUIPMENT_REQUEST\t{}", challenger->GetName()));
    SendDuelWagerAddonMessage(challenger, Acore::StringFormat("EQUIPMENT_SENT\t{}", target->GetName()));
    return true;
}

void TryCreateEquipmentLootCorpse(Player* loser)
{
    if (!loser)
        return;

    auto pendingItr = PendingEquipmentDeaths.find(loser->GetGUID());
    if (pendingItr == PendingEquipmentDeaths.end())
        return;

    PendingEquipmentDeath pending = pendingItr->second;
    PendingEquipmentDeaths.erase(pendingItr);

    Player* winner = ObjectAccessor::FindConnectedPlayer(pending.WinnerGuid);
    Corpse* corpse = loser->GetCorpse();
    if (!corpse || corpse->GetType() == CORPSE_BONES)
        return;

    std::vector<EquipmentLootItemData> items = CollectEquipmentDuelItems(loser);
    corpse->loot.clear();
    corpse->loot.lootOwnerGUID = loser->GetGUID();
    corpse->lootRecipient = winner;

    std::vector<EquipmentLootItemData> movedItems;
    movedItems.reserve(items.size());
    for (EquipmentLootItemData const& itemData : items)
    {
        if (RemoveEquipmentDuelItem(loser, itemData) &&
            AddEquipmentLootToCorpse(corpse, itemData))
        {
            movedItems.push_back(itemData);
        }
    }

    ObjectGuid const corpseGuid = corpse->GetGUID();
    loser->SpawnCorpseBones(false);
    Corpse* bones = loser->GetMap() ? loser->GetMap()->GetCorpse(corpseGuid) : nullptr;
    if (bones && bones->loot.empty() && !movedItems.empty())
    {
        bones->loot.clear();
        bones->loot.lootOwnerGUID = loser->GetGUID();
        bones->lootRecipient = winner;
        for (EquipmentLootItemData const& itemData : movedItems)
            AddEquipmentLootToCorpse(bones, itemData);
    }

    if (!bones || bones->loot.empty())
        return;

    // The new skeleton is confirmed. Now (and only now) clear any older skeleton
    // this same loser still had lingering from a previous duel, so stale tracking
    // can never block this one — while never leaving the loser with no skeleton if
    // creation had failed above. The new bones have a fresh GUID, so this never
    // touches the skeleton we just built.
    DespawnPlayerEquipmentLootCorpses(loser->GetGUID());

    RefreshCorpseLoot(bones->loot, bones->GetGUID());
    bones->SetFlag(CORPSE_FIELD_FLAGS, CORPSE_FLAG_LOOTABLE);
    bones->SetFlag(CORPSE_FIELD_DYNAMIC_FLAGS, CORPSE_DYNFLAG_LOOTABLE);
    bones->ForceValuesUpdateAtIndex(CORPSE_FIELD_FLAGS);
    bones->ForceValuesUpdateAtIndex(CORPSE_FIELD_DYNAMIC_FLAGS);
    bones->lootRecipient = winner;

    // Re-broadcast the skeleton now that the lootable flags are set (AddToMap
    // sent it before they were applied) so every nearby player sees it.
    bones->UpdateObjectVisibility(true);

    uint64 const expiresAt = static_cast<uint64>(GameTime::GetGameTime().count()) +
        DuelWagerSystem::GetSettings().EquipmentCorpseDurationSeconds;

    EquipmentLootCorpses[bones->GetGUID()] = {
        pending.WinnerGuid,
        loser->GetGUID(),
        bones->GetMapId(),
        bones->GetInstanceId(),
        expiresAt
    };

    RecordDuelEquipmentCorpse(bones, loser, pending, movedItems, expiresAt);

    if (winner && winner->GetSession())
    {
        ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
            "DuelWager: {} dropped their equipped gear onto a lootable skeleton. Only you can loot it.",
            "DuelWager: {} dejo su equipo equipado en un esqueleto looteable. Solo tu puedes lootearlo."),
            loser->GetName());
    }
}

void RefundActiveWager(ActiveWagerDuel const& wager, Player* first = nullptr, Player* second = nullptr)
{
    if (Player* challenger = FindKnownOrConnectedPlayer(wager.ChallengerGuid, first, second))
    {
        challenger->ModifyMoney(static_cast<int32>(wager.Stake), false);
        ChatHandler(challenger->GetSession()).PSendSysMessage(Tr(challenger,
            "DuelWager: duel interrupted. {} was refunded.",
            "DuelWager: duelo interrumpido. Se devolvieron {}."), FormatMoney(wager.Stake));
    }

    if (Player* target = FindKnownOrConnectedPlayer(wager.TargetGuid, first, second))
    {
        target->ModifyMoney(static_cast<int32>(wager.Stake), false);
        ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
            "DuelWager: duel interrupted. {} was refunded.",
            "DuelWager: duelo interrumpido. Se devolvieron {}."), FormatMoney(wager.Stake));
    }
}

bool TryActivateWagerDuel(PendingWagerDuel const& pending, Player* challenger, Player* target, ChatHandler& handler)
{
    if (!ValidateWagerDuelPlayers(challenger, target, pending.Stake, handler, true))
        return false;

    challenger->ModifyMoney(-static_cast<int32>(pending.Stake), false);
    target->ModifyMoney(-static_cast<int32>(pending.Stake), false);

    ActiveWagers[MakeWagerKey(challenger->GetGUID(), target->GetGUID())] = { challenger->GetGUID(), target->GetGUID(), pending.Stake };
    RemovePendingForTarget(pending.TargetGuid);

    ChatHandler(challenger->GetSession()).PSendSysMessage(Tr(challenger,
        "DuelWager: gold duel accepted. Total prize: {}.",
        "DuelWager: duelo por oro aceptado. Premio total: {}."), FormatMoney(pending.Stake * 2));
    ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
        "DuelWager: gold duel accepted. Total prize: {}.",
        "DuelWager: duelo por oro aceptado. Premio total: {}."), FormatMoney(pending.Stake * 2));
    SendDuelWagerAddonMessage(challenger, "STARTED");
    SendDuelWagerAddonMessage(target, "STARTED");
    return true;
}

void CancelPendingWagerDuel(PendingWagerDuel const& pending, Player* canceller = nullptr)
{
    SendDuelWagerAddonMessage(pending.ChallengerGuid, "CANCELLED");
    SendDuelWagerAddonMessage(pending.TargetGuid, "CANCELLED");
    RemovePendingForTarget(pending.TargetGuid);
    CancelNormalDuelForPending(pending, canceller);
}

bool StartAcceptedWagerDuel(Player* challenger, Player* target, PendingWagerDuel const& pending, ChatHandler& handler)
{
    if (!HasPendingNormalDuelBetween(challenger, target))
    {
        handler.SendSysMessage(Tr(handler,
            "DuelWager: the normal duel request is no longer active.",
            "DuelWager: el reto de duelo normal ya no esta activo."));
        RemovePendingForTarget(pending.TargetGuid);
        return false;
    }

    if (!TryActivateWagerDuel(pending, challenger, target, handler))
    {
        CancelPendingWagerDuel(pending, target);
        return false;
    }

    time_t const startTime = GameTime::GetGameTime().count() + 3;
    challenger->duel->StartTime = startTime;
    target->duel->StartTime = startTime;
    challenger->duel->State = DUEL_STATE_COUNTDOWN;
    target->duel->State = DUEL_STATE_COUNTDOWN;

    challenger->SendDuelCountdown(3000);
    target->SendDuelCountdown(3000);
    return true;
}

void TryActivateCountdownWagerDuel(Player* player)
{
    if (!player || !player->duel || player->duel->State != DUEL_STATE_COUNTDOWN)
        return;

    Player* opponent = player->duel->Opponent;
    if (!opponent)
        return;

    std::optional<PendingWagerDuel> pending = FindPendingForPair(player->GetGUID(), opponent->GetGUID());
    if (!pending)
        return;

    Player* challenger = FindKnownOrConnectedPlayer(pending->ChallengerGuid, player, opponent);
    Player* target = FindKnownOrConnectedPlayer(pending->TargetGuid, player, opponent);
    if (!challenger || !target)
    {
        CancelPendingWagerDuel(*pending, player);
        return;
    }

    ChatHandler handler(challenger->GetSession());
    if (!TryActivateWagerDuel(*pending, challenger, target, handler))
        CancelPendingWagerDuel(*pending, player);
}

bool HandleChallengeCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !handler->GetSession())
        return false;

    Player* challenger = handler->GetSession()->GetPlayer();
    if (!DuelWagerSystem::IsEnabled())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: gold duels are disabled.", "DuelWager: los duelos por oro estan desactivados."));
        return true;
    }

    std::vector<std::string> const tokens = ParseCommandTokens(args ? args : "");
    if (tokens.size() == 2 && (StringEqualI(tokens[0], "equipment") || StringEqualI(tokens[0], "equipamiento")))
    {
        std::string const targetName = ExtractPlayerName(tokens[1]);
        if (targetName.empty())
        {
            handler->SendSysMessage(Tr(*handler, "DuelWager: invalid name.", "DuelWager: nombre invalido."));
            return true;
        }

        Player* target = ObjectAccessor::FindPlayerByName(targetName, true);
        if (!target)
        {
            handler->PSendSysMessage(Tr(*handler, "DuelWager: {} is not online.", "DuelWager: {} no esta conectado."), targetName);
            return true;
        }

        StartEquipmentChallenge(challenger, target, *handler);
        return true;
    }

    if (tokens.size() != 2)
    {
        handler->SendSysMessage(Tr(*handler,
            "Use: .duel Name 10g | .duel equipment Name | .duel accept | .duel decline | .duel cancel",
            "Uso: .duelo Nombre 10g | .duelo equipamiento Nombre | .duelo aceptar | .duelo rechazar | .duelo cancelar"));
        return true;
    }

    std::string const targetName = ExtractPlayerName(tokens[0]);
    if (targetName.empty())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: invalid name.", "DuelWager: nombre invalido."));
        return true;
    }

    Player* target = ObjectAccessor::FindPlayerByName(targetName, true);
    if (!target)
    {
        handler->PSendSysMessage(Tr(*handler, "DuelWager: {} is not online.", "DuelWager: {} no esta conectado."), targetName);
        return true;
    }

    std::optional<uint32> stake = ParseMoneyAmount(tokens[1]);
    if (!stake)
    {
        handler->SendSysMessage(Tr(*handler,
            "DuelWager: invalid amount. Use something like 10, 10g, 50s, or 1g50s.",
            "DuelWager: cantidad invalida. Usa por ejemplo 10, 10g, 50s o 1g50s."));
        return true;
    }

    if (!ValidateWagerDuelPlayers(challenger, target, *stake, *handler))
        return true;

    ExpirePendingIfNeeded(target->GetGUID());

    auto incomingItr = PendingByTarget.find(target->GetGUID());
    if (incomingItr != PendingByTarget.end() && incomingItr->second.ChallengerGuid != challenger->GetGUID())
    {
        handler->PSendSysMessage(Tr(*handler,
            "DuelWager: {} already has a pending gold duel request.",
            "DuelWager: {} ya tiene un reto de duelo por oro pendiente."), target->GetName());
        return true;
    }

    auto outgoingItr = PendingTargetByChallenger.find(challenger->GetGUID());
    if (outgoingItr != PendingTargetByChallenger.end())
    {
        auto oldPendingItr = PendingByTarget.find(outgoingItr->second);
        if (oldPendingItr != PendingByTarget.end())
        {
            PendingWagerDuel const oldPending = oldPendingItr->second;
            SendDuelWagerAddonMessage(oldPending.TargetGuid, Acore::StringFormat("CANCELLED\t{}", challenger->GetName()));
            SendDuelWagerAddonMessage(oldPending.ChallengerGuid, "CANCELLED");
            RemovePendingForTarget(oldPending.TargetGuid);
            CancelNormalDuelForPending(oldPending, challenger);
        }
        else
            PendingTargetByChallenger.erase(outgoingItr);
    }

    PendingWagerDuel pending;
    pending.ChallengerGuid = challenger->GetGUID();
    pending.TargetGuid = target->GetGUID();
    pending.Stake = *stake;
    pending.ExpiresAt = GameTime::GetGameTime().count() + DuelWagerSystem::GetSettings().TimeoutSeconds;

    PendingByTarget[target->GetGUID()] = pending;
    PendingTargetByChallenger[challenger->GetGUID()] = target->GetGUID();

    SendDuelWagerAddonMessage(target, Acore::StringFormat("REQUEST\t{}\t{}\t{}", challenger->GetName(), FormatMoney(*stake), *stake));

    SpellCastResult const castResult = challenger->CastSpell(target, SpellDuel, false);
    if (castResult != SPELL_CAST_OK || !HasPendingNormalDuelBetween(challenger, target))
    {
        RemovePendingForTarget(target->GetGUID());
        SendDuelWagerAddonMessage(target, "CANCELLED");
        handler->PSendSysMessage(Tr(*handler,
            "DuelWager: could not start the normal duel request. Code: {}.",
            "DuelWager: no se pudo iniciar el reto de duelo normal. Codigo: {}."), uint32(castResult));
        return true;
    }

    ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
        "DuelWager: {} challenged you to a gold duel for {}. Do you accept or decline?",
        "DuelWager: {} te reto a un duelo de oro por {}. Deseas aceptar o rechazar?"),
        challenger->GetName(), FormatMoney(*stake));

    handler->PSendSysMessage(Tr(*handler,
        "DuelWager: gold duel request sent to {} for {}.",
        "DuelWager: reto de duelo por oro enviado a {} por {}."),
        target->GetName(), FormatMoney(*stake));
    SendDuelWagerAddonMessage(challenger, Acore::StringFormat("SENT\t{}\t{}\t{}", target->GetName(), FormatMoney(*stake), *stake));
    return true;
}

bool HandleAcceptCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !handler->GetSession())
        return false;

    Player* target = handler->GetSession()->GetPlayer();
    if (!DuelWagerSystem::IsEnabled())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: gold duels are disabled.", "DuelWager: los duelos por oro estan desactivados."));
        return true;
    }

    ExpirePendingIfNeeded(target->GetGUID());

    auto pendingItr = PendingByTarget.find(target->GetGUID());
    if (pendingItr == PendingByTarget.end())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: you have no pending gold duel requests.", "DuelWager: no tienes retos de duelo por oro pendientes."));
        return true;
    }

    PendingWagerDuel pending = pendingItr->second;
    Player* challenger = ObjectAccessor::FindConnectedPlayer(pending.ChallengerGuid);

    std::vector<std::string> const tokens = ParseCommandTokens(args ? args : "");
    if (!tokens.empty())
    {
        std::string const challengerName = ExtractPlayerName(tokens[0]);
        if (!challenger || challengerName.empty() || !StringEqualI(challenger->GetName(), challengerName))
        {
            handler->SendSysMessage(Tr(*handler, "DuelWager: that player does not match your pending request.", "DuelWager: ese jugador no coincide con tu reto pendiente."));
            return true;
        }
    }

    if (!challenger)
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: the player is no longer connected.", "DuelWager: el jugador ya no esta conectado."));
        RemovePendingForTarget(target->GetGUID());
        return true;
    }

    if (!StartAcceptedWagerDuel(challenger, target, pending, *handler))
        return true;

    return true;
}

bool HandleDeclineCommand(ChatHandler* handler, char const* /*args*/)
{
    if (!handler || !handler->GetSession())
        return false;

    Player* target = handler->GetSession()->GetPlayer();
    ExpirePendingIfNeeded(target->GetGUID());

    auto pendingItr = PendingByTarget.find(target->GetGUID());
    if (pendingItr == PendingByTarget.end())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: you have no pending gold duel requests.", "DuelWager: no tienes retos de duelo por oro pendientes."));
        return true;
    }

    PendingWagerDuel const pending = pendingItr->second;
    if (Player* challenger = ObjectAccessor::FindConnectedPlayer(pending.ChallengerGuid))
    {
        ChatHandler(challenger->GetSession()).PSendSysMessage(Tr(challenger,
            "DuelWager: {} declined your gold duel.",
            "DuelWager: {} rechazo tu duelo por oro."), target->GetName());
    }
    SendDuelWagerAddonMessage(pending.ChallengerGuid, Acore::StringFormat("DECLINED\t{}", target->GetName()));
    SendDuelWagerAddonMessage(target, "DECLINED");
    handler->SendSysMessage(Tr(*handler, "DuelWager: you declined the gold duel.", "DuelWager: rechazaste el duelo por oro."));
    RemovePendingForTarget(target->GetGUID());
    CancelNormalDuelForPending(pending, target);
    return true;
}

bool HandleCancelCommand(ChatHandler* handler, char const* /*args*/)
{
    if (!handler || !handler->GetSession())
        return false;

    Player* challenger = handler->GetSession()->GetPlayer();
    auto targetItr = PendingTargetByChallenger.find(challenger->GetGUID());
    if (targetItr == PendingTargetByChallenger.end())
    {
        handler->SendSysMessage(Tr(*handler, "DuelWager: you have no outgoing pending requests.", "DuelWager: no tienes retos enviados pendientes."));
        return true;
    }

    ObjectGuid const targetGuid = targetItr->second;
    auto pendingItr = PendingByTarget.find(targetGuid);
    std::optional<PendingWagerDuel> pending;
    if (pendingItr != PendingByTarget.end())
        pending = pendingItr->second;

    if (Player* target = ObjectAccessor::FindConnectedPlayer(targetGuid))
    {
        ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
            "DuelWager: {} cancelled their gold duel request.",
            "DuelWager: {} cancelo su reto de duelo por oro."), challenger->GetName());
    }
    SendDuelWagerAddonMessage(targetGuid, Acore::StringFormat("CANCELLED\t{}", challenger->GetName()));
    SendDuelWagerAddonMessage(challenger, "CANCELLED");
    if (pending)
    {
        RemovePendingForTarget(pending->TargetGuid);
        CancelNormalDuelForPending(*pending, challenger);
    }
    else
        RemovePendingForChallenger(challenger->GetGUID());
    handler->SendSysMessage(Tr(*handler, "DuelWager: request cancelled.", "DuelWager: reto cancelado."));
    return true;
}
}

using namespace Acore::ChatCommands;

class DuelWagerCommandScript : public CommandScript
{
public:
    DuelWagerCommandScript() : CommandScript("DuelWagerCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable duelCommandTable =
        {
            { "accept",  HandleAcceptCommand,    SEC_GAMEMASTER, Console::No },
            { "aceptar", HandleAcceptCommand,    SEC_GAMEMASTER, Console::No },
            { "decline", HandleDeclineCommand,   SEC_GAMEMASTER, Console::No },
            { "reject",  HandleDeclineCommand,   SEC_GAMEMASTER, Console::No },
            { "rechazar", HandleDeclineCommand,  SEC_GAMEMASTER, Console::No },
            { "cancel",  HandleCancelCommand,    SEC_GAMEMASTER, Console::No },
            { "cancelar", HandleCancelCommand,   SEC_GAMEMASTER, Console::No },
            { "",        HandleChallengeCommand, SEC_GAMEMASTER, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "duel", duelCommandTable },
            { "duelo", duelCommandTable }
        };

        return commandTable;
    }
};

class DuelWagerPlayerScript : public PlayerScript
{
public:
    DuelWagerPlayerScript() : PlayerScript("DuelWagerPlayerScript", {
        PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_DUEL_START,
        PLAYERHOOK_ON_DUEL_END,
        PLAYERHOOK_ON_PLAYER_RELEASED_GHOST,
        PLAYERHOOK_ON_PLAYER_RESURRECT,
        PLAYERHOOK_ON_LOOT_ITEM
    }) { }

    void OnPlayerBeforeSendChatMessage(
        Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (!player || type != CHAT_MSG_WHISPER || lang != LANG_ADDON ||
            !msg.starts_with(DuelWagerClientCommandPrefix))
        {
            return;
        }

        std::string_view const action =
            std::string_view(msg).substr(DuelWagerClientCommandPrefix.size());
        if (action.size() <= 200 && action.starts_with("duel "))
        {
            std::string const arguments(action.substr(5));
            ChatHandler handler(player->GetSession());
            HandleChallengeCommand(&handler, arguments.c_str());
        }

        // Keep the addon packet private and harmless after processing.
        msg = "HCDW\tIGNORED";
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;

        ObjectGuid const guid = player->GetGUID();

        auto incomingItr = PendingByTarget.find(guid);
        if (incomingItr != PendingByTarget.end())
        {
            PendingWagerDuel const pending = incomingItr->second;
            if (Player* challenger = ObjectAccessor::FindConnectedPlayer(incomingItr->second.ChallengerGuid))
            {
                ChatHandler(challenger->GetSession()).PSendSysMessage(Tr(challenger,
                    "DuelWager: {} disconnected. Request cancelled.",
                    "DuelWager: {} se desconecto. Reto cancelado."), player->GetName());
            }
            SendDuelWagerAddonMessage(incomingItr->second.ChallengerGuid, Acore::StringFormat("CANCELLED\t{}", player->GetName()));
            RemovePendingForTarget(guid);
            CancelNormalDuelForPending(pending, player);
        }

        auto outgoingItr = PendingTargetByChallenger.find(guid);
        if (outgoingItr != PendingTargetByChallenger.end())
        {
            auto pendingItr = PendingByTarget.find(outgoingItr->second);
            std::optional<PendingWagerDuel> pending;
            if (pendingItr != PendingByTarget.end())
                pending = pendingItr->second;

            if (Player* target = ObjectAccessor::FindConnectedPlayer(outgoingItr->second))
            {
                ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
                    "DuelWager: {} disconnected. Request cancelled.",
                    "DuelWager: {} se desconecto. Reto cancelado."), player->GetName());
            }
            SendDuelWagerAddonMessage(outgoingItr->second, Acore::StringFormat("CANCELLED\t{}", player->GetName()));
            if (pending)
            {
                RemovePendingForTarget(pending->TargetGuid);
                CancelNormalDuelForPending(*pending, player);
            }
            else
                RemovePendingForChallenger(guid);
        }

        auto incomingEquipmentItr = PendingEquipmentByTarget.find(guid);
        if (incomingEquipmentItr != PendingEquipmentByTarget.end())
        {
            PendingEquipmentDuel const pending = incomingEquipmentItr->second;
            if (Player* challenger = ObjectAccessor::FindConnectedPlayer(pending.ChallengerGuid))
            {
                ChatHandler(challenger->GetSession()).PSendSysMessage(Tr(challenger,
                    "DuelWager: {} disconnected. Equipment duel request cancelled.",
                    "DuelWager: {} se desconecto. Reto de duelo por equipamiento cancelado."), player->GetName());
            }

            RemovePendingEquipmentForTarget(guid);
            CancelNormalDuelForPending({ pending.ChallengerGuid, pending.TargetGuid, 0, pending.ExpiresAt }, player);
        }

        auto outgoingEquipmentItr = PendingEquipmentTargetByChallenger.find(guid);
        if (outgoingEquipmentItr != PendingEquipmentTargetByChallenger.end())
        {
            auto pendingItr = PendingEquipmentByTarget.find(outgoingEquipmentItr->second);
            if (pendingItr != PendingEquipmentByTarget.end())
            {
                PendingEquipmentDuel const pending = pendingItr->second;
                if (Player* target = ObjectAccessor::FindConnectedPlayer(pending.TargetGuid))
                {
                    ChatHandler(target->GetSession()).PSendSysMessage(Tr(target,
                        "DuelWager: {} disconnected. Equipment duel request cancelled.",
                        "DuelWager: {} se desconecto. Reto de duelo por equipamiento cancelado."), player->GetName());
                }

                RemovePendingEquipmentForTarget(pending.TargetGuid);
                CancelNormalDuelForPending({ pending.ChallengerGuid, pending.TargetGuid, 0, pending.ExpiresAt }, player);
            }
            else
                RemovePendingEquipmentForChallenger(guid);
        }

        if (std::optional<DuelWagerKey> key = FindActiveWagerKeyForPlayer(guid))
        {
            auto wagerItr = ActiveWagers.find(*key);
            if (wagerItr != ActiveWagers.end())
            {
                ActiveWagerDuel const wager = wagerItr->second;
                ActiveWagers.erase(wagerItr);
                RefundActiveWager(wager, player);

                ObjectGuid const opponentGuid = wager.ChallengerGuid == guid ? wager.TargetGuid : wager.ChallengerGuid;
                if (Player* opponent = ObjectAccessor::FindConnectedPlayer(opponentGuid))
                {
                    ChatHandler(opponent->GetSession()).PSendSysMessage(Tr(opponent,
                        "DuelWager: {} disconnected. The gold duel was cancelled.",
                        "DuelWager: {} se desconecto. El duelo por oro fue cancelado."), player->GetName());
                    SendDuelWagerAddonMessage(opponent, "REFUNDED");
                }
            }
        }

        if (std::optional<DuelWagerKey> key = FindActiveEquipmentKeyForPlayer(guid))
            ActiveEquipmentDuels.erase(*key);

        PendingEquipmentDeaths.erase(guid);
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        TryActivateCountdownWagerDuel(player);

        if (player)
            ExpirePendingEquipmentIfNeeded(player->GetGUID());

        uint64 const now = GameTime::GetGameTime().count();
        std::vector<ObjectGuid> expiredCorpses;
        for (auto const& [corpseGuid, tracked] : EquipmentLootCorpses)
            if (tracked.ExpiresAt && tracked.ExpiresAt <= now)
                expiredCorpses.push_back(corpseGuid);

        for (ObjectGuid const& corpseGuid : expiredCorpses)
            DespawnEquipmentLootCorpse(corpseGuid);
    }

    void OnPlayerDuelStart(Player* player1, Player* player2) override
    {
        if (!player1 || !player2)
            return;

        DuelStartedAtMs[MakeWagerKey(player1->GetGUID(), player2->GetGUID())] = GameTime::GetGameTimeMS().count();
        std::optional<PendingWagerDuel> pending = FindPendingForPair(player1->GetGUID(), player2->GetGUID());
        if (pending)
        {
            Player* challenger = FindKnownOrConnectedPlayer(pending->ChallengerGuid, player1, player2);
            Player* target = FindKnownOrConnectedPlayer(pending->TargetGuid, player1, player2);
            if (!challenger || !target)
            {
                RemovePendingForTarget(pending->TargetGuid);
            }
            else
            {
                ChatHandler handler(challenger->GetSession());
                if (!TryActivateWagerDuel(*pending, challenger, target, handler))
                    RemovePendingForTarget(pending->TargetGuid);
            }
        }

        std::optional<PendingEquipmentDuel> pendingEquipment = FindPendingEquipmentForPair(player1->GetGUID(), player2->GetGUID());
        if (!pendingEquipment)
            return;

        ActiveEquipmentDuels[MakeWagerKey(player1->GetGUID(), player2->GetGUID())] = {
            pendingEquipment->ChallengerGuid,
            pendingEquipment->TargetGuid
        };
        RemovePendingEquipmentForTarget(pendingEquipment->TargetGuid);

    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        if (!winner || !loser)
            return;

        DuelWagerKey const key = MakeWagerKey(winner->GetGUID(), loser->GetGUID());
        uint64 const startedAt = DuelStartedAtMs.contains(key) ? DuelStartedAtMs[key] : GameTime::GetGameTimeMS().count();
        uint32 const durationMs = uint32(std::min<uint64>(UINT32_MAX, GameTime::GetGameTimeMS().count() - startedAt));
        DuelStartedAtMs.erase(key);
        auto equipmentItr = ActiveEquipmentDuels.find(key);
        if (equipmentItr != ActiveEquipmentDuels.end())
        {
            ActiveEquipmentDuels.erase(equipmentItr);

            if (type == DUEL_INTERRUPTED)
                return;

            PendingEquipmentDeath deathRecord;
            deathRecord.WinnerGuid = winner->GetGUID();
            deathRecord.LoserGuid = loser->GetGUID();
            deathRecord.WinnerAccount = GetDuelPlayerAccountId(winner);
            deathRecord.WinnerName = winner->GetName();
            deathRecord.WinnerRace = uint8(winner->getRace());
            deathRecord.WinnerClass = uint8(winner->getClass());
            deathRecord.WinnerLevel = uint8(winner->GetLevel());
            PendingEquipmentDeaths[loser->GetGUID()] = std::move(deathRecord);

            // The duel system leaves the loser alive at 1 HP just before this hook runs.
            // KillPlayer() alone transitions straight to DeathState::Corpse and skips the
            // DeathState::JustDied step that zeroes health, which would leave the loser stuck
            // half-dead. Force the proper death sequence so they die instantly and can release.
            if (loser->IsAlive())
            {
                loser->SetHealth(0);
                loser->setDeathState(DeathState::JustDied);
                loser->KillPlayer();
            }

            // A death-duel loss is unconditional: strip any self-resurrect
            // (soulstone, Reincarnation/ankh, etc.) so the loser cannot pop back
            // up and skip dropping their gear. They must release spirit, which is
            // what builds the lootable skeleton.
            loser->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);

            return;
        }

        auto wagerItr = ActiveWagers.find(key);
        if (wagerItr == ActiveWagers.end())
        {
            if (type != DUEL_INTERRUPTED)
            {
                std::optional<PendingEquipmentDuel> pendingEquipment = FindPendingEquipmentForPair(winner->GetGUID(), loser->GetGUID());
                if (pendingEquipment)
                    RemovePendingEquipmentForTarget(pendingEquipment->TargetGuid);
                else
                    RecordNormalDuel(winner, loser, durationMs);
                return;
            }

            std::optional<PendingWagerDuel> pending = FindPendingForPair(winner->GetGUID(), loser->GetGUID());
            if (pending)
            {
                RemovePendingForTarget(pending->TargetGuid);

                if (loser->GetGUID() == pending->TargetGuid)
                {
                    ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
                        "DuelWager: {} declined your gold duel.",
                        "DuelWager: {} rechazo tu duelo por oro."), loser->GetName());
                    ChatHandler(loser->GetSession()).SendSysMessage(Tr(loser,
                        "DuelWager: you declined the gold duel.",
                        "DuelWager: rechazaste el duelo por oro."));
                    SendDuelWagerAddonMessage(winner, Acore::StringFormat("DECLINED\t{}", loser->GetName()));
                    SendDuelWagerAddonMessage(loser, "DECLINED");
                }
                else
                {
                    ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
                        "DuelWager: {} cancelled their gold duel request.",
                        "DuelWager: {} cancelo su reto de duelo por oro."), loser->GetName());
                    ChatHandler(loser->GetSession()).SendSysMessage(Tr(loser,
                        "DuelWager: request cancelled.",
                        "DuelWager: reto cancelado."));
                    SendDuelWagerAddonMessage(winner, Acore::StringFormat("CANCELLED\t{}", loser->GetName()));
                    SendDuelWagerAddonMessage(loser, "CANCELLED");
                }
            }

            std::optional<PendingEquipmentDuel> pendingEquipment = FindPendingEquipmentForPair(winner->GetGUID(), loser->GetGUID());
            if (!pendingEquipment)
                return;

            RemovePendingEquipmentForTarget(pendingEquipment->TargetGuid);

            if (loser->GetGUID() == pendingEquipment->TargetGuid)
            {
                ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
                    "DuelWager: {} declined your equipment duel.",
                    "DuelWager: {} rechazo tu duelo por equipamiento."), loser->GetName());
                ChatHandler(loser->GetSession()).SendSysMessage(Tr(loser,
                    "DuelWager: you declined the equipment duel.",
                    "DuelWager: rechazaste el duelo por equipamiento."));
            }
            else
            {
                ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
                    "DuelWager: {} cancelled their equipment duel request.",
                    "DuelWager: {} cancelo su reto de duelo por equipamiento."), loser->GetName());
                ChatHandler(loser->GetSession()).SendSysMessage(Tr(loser,
                    "DuelWager: equipment duel request cancelled.",
                    "DuelWager: reto de duelo por equipamiento cancelado."));
            }

            return;
        }

        ActiveWagerDuel wager = wagerItr->second;
        ActiveWagers.erase(wagerItr);

        if (type == DUEL_INTERRUPTED)
        {
            RefundActiveWager(wager);
            SendDuelWagerAddonMessage(winner, "REFUNDED");
            SendDuelWagerAddonMessage(loser, "REFUNDED");
            return;
        }

        uint32 const prize = wager.Stake * 2;
        if (winner->ModifyMoney(static_cast<int32>(prize), false))
        {
            RecordGoldDuel(winner, loser, wager.Stake, prize);
            ChatHandler(winner->GetSession()).PSendSysMessage(Tr(winner,
                "DuelWager: you won the gold duel and received {}.",
                "DuelWager: ganaste el duelo por oro y recibiste {}."), FormatMoney(prize));
            ChatHandler(loser->GetSession()).PSendSysMessage(Tr(loser,
                "DuelWager: you lost the gold duel. Stake lost: {}.",
                "DuelWager: perdiste el duelo por oro. Apuesta perdida: {}."), FormatMoney(wager.Stake));
            SendDuelWagerAddonMessage(winner, Acore::StringFormat("WON\t{}", FormatMoney(prize)));
            SendDuelWagerAddonMessage(loser, Acore::StringFormat("LOST\t{}", FormatMoney(wager.Stake)));
            return;
        }

        RefundActiveWager(wager);
        ChatHandler(winner->GetSession()).SendSysMessage(Tr(winner,
            "DuelWager: you could not receive the prize because of the gold cap. The wager was refunded.",
            "DuelWager: no pudiste recibir el premio por limite de oro. La apuesta fue devuelta."));
        SendDuelWagerAddonMessage(winner, "REFUNDED");
        SendDuelWagerAddonMessage(loser, "REFUNDED");
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        TryCreateEquipmentLootCorpse(player);
    }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    {
        if (player)
            PendingEquipmentDeaths.erase(player->GetGUID());
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid lootguid) override
    {
        if (!player || !lootguid.IsCorpse())
            return;

        auto itr = EquipmentLootCorpses.find(lootguid);
        if (itr == EquipmentLootCorpses.end())
            return;

        if (item)
            RecordDuelEquipmentItemLooted(lootguid, item->GetEntry(), player->GetGUID());

        Corpse* corpse = ObjectAccessor::GetCorpse(*player, lootguid);
        if (!corpse || !corpse->loot.isLooted())
            return;

        EquipmentLootCorpses.erase(itr);
    }
};

class DuelWagerUpdateWorldScript : public WorldScript
{
public:
    DuelWagerUpdateWorldScript() : WorldScript("DuelWagerUpdateWorldScript", {
        WORLDHOOK_ON_UPDATE,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnStartup() override
    {
        EnsureDuelEquipmentTables();
    }

    void OnUpdate(uint32 diff) override
    {
        if (PendingCleanupTimer > diff)
        {
            PendingCleanupTimer -= diff;
            return;
        }

        PendingCleanupTimer = 5 * IN_MILLISECONDS;
        ExpireAllPendingRequests();
        ExpireAllPendingEquipmentRequests();
    }
};

void AddDuelWagerScripts()
{
    new DuelWagerCommandScript();
    new DuelWagerPlayerScript();
    new DuelWagerUpdateWorldScript();
}
