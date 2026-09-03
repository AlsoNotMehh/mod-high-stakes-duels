#ifndef _DUEL_WAGER_H_
#define _DUEL_WAGER_H_

#include "Common.h"
#include "ObjectGuid.h"
#include <unordered_map>
#include <string>

enum DuelWagerType : uint8
{
    DUEL_TYPE_NONE  = 0,
    DUEL_TYPE_GOLD  = 1,
    DUEL_TYPE_DEATH = 2 // Mak'gora / Hardcore Death Duel
};

struct ActiveDuelWager
{
    ObjectGuid ChallengerGuid;
    ObjectGuid TargetGuid;
    DuelWagerType Type = DUEL_TYPE_NONE;
    uint32 GoldStake = 0;
    time_t StartedAt = 0;
    time_t ExpiresAt = 0;
    bool Accepted = false;
};

class DuelWagerMgr
{
public:
    static DuelWagerMgr* instance();

    void LoadConfig();
    void EnsureDatabaseTables();

    bool IsEnabled() const { return _enabled; }
    bool IsGoldDuelEnabled() const { return _goldEnabled; }
    bool IsDeathDuelEnabled() const { return _deathDuelEnabled; }
    uint32 GetMinLevel() const { return _minLevel; }
    bool IsAnnounceWinner() const { return _announceWinner; }

    bool RequestGoldDuel(Player* challenger, Player* target, uint32 goldAmount);
    bool RequestDeathDuel(Player* challenger, Player* target);
    bool AcceptDuel(Player* target);
    void CancelDuel(Player* player);

    void OnDuelStart(Player* player1, Player* player2);
    void OnDuelEnd(Player* winner, Player* loser, bool fled);

    void RecordNormalDuel(Player* winner, Player* loser, uint32 durationMs);
    void RecordGoldDuel(Player* winner, Player* loser, uint32 stake, uint32 prize, uint32 durationMs);
    void RecordMakgoraDuel(Player* winner, Player* loser, uint32 durationMs, bool fled);

private:
    DuelWagerMgr() = default;

    bool _enabled = true;
    bool _goldEnabled = true;
    bool _deathDuelEnabled = true;
    uint32 _minLevel = 10;
    bool _announceWinner = true;
    uint32 _timeoutSeconds = 60;
    float _maxDistance = 30.0f;

    std::unordered_map<ObjectGuid, ActiveDuelWager> _pendingWagers;
    std::unordered_map<ObjectGuid, ActiveDuelWager> _activeWagers;
};

#define sDuelWagerMgr DuelWagerMgr::instance()

#endif // _DUEL_WAGER_H_
