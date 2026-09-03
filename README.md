# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore Module: mod-high-stakes-duels

[![AzerothCore Module](https://img.shields.io/badge/AzerothCore-Module-red?style=flat-square&logo=github)](https://github.com/azerothcore/azerothcore-wotlk)
[![C++20](https://img.shields.io/badge/Language-C++20-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)
[![Branch 3.3.5a](https://img.shields.io/badge/Branch-3.3.5a-orange?style=flat-square)](https://github.com/azerothcore/azerothcore-wotlk)
[![License MIT](https://img.shields.io/badge/License-MIT-blue?style=flat-square)](LICENSE)
[![GitHub Stars](https://img.shields.io/github/stars/AlsoNotMehh/mod-high-stakes-duels?style=flat-square&color=yellow&logo=github)](https://github.com/AlsoNotMehh/mod-high-stakes-duels/stargazers)

A complete **High-Stakes Duels** system for **AzerothCore (WotLK 3.3.5a)** featuring **Gold Wagers**, **Mak'gora (Hardcore Death Duels)**, and an authentic Blizzard 3-option duel selection interface.

### 💡 Why this module?
Standard WoW duels carry zero risk and zero excitement: players fight, the loser kneels at 1 HP, and nothing changes. 

**`mod-high-stakes-duels`** introduces a complete high-stakes dueling ecosystem with an integrated Blizzard UI popup offering 3 distinct dueling options:
- ⚔️ **Normal Duel:** Standard friendly duel without stakes.
- 💰 **Gold Wager Duel:** Opens an authentic Blizzard gold entry dialog. Both players escrow matching gold stakes, and the victor takes the entire pot automatically.
- ☠️ **Mak'gora (Hardcore Death Duel):** The ultimate high-stakes showdown. Both players must confirm with strict warning dialogs. The loser suffers **permanent character death and Hardcore lockout**, while the realm announces the champion's victory.

## 📊 Feature Comparison

| Feature | Standard Duels | mod-high-stakes-duels |
| :--- | :---: | :---: |
| **Interactive 3-Option UI** | ❌ None (only standard duel) | ✅ **Right-click target menu with Normal, Gold, and Mak'gora** |
| **Gold Wager Duels** | ❌ No stakes (friendly only) | ✅ **Secure gold escrow with automatic winner payout** |
| **Death Duels (Mak'gora)** | ❌ Loser stays at 1 HP | ✅ **Loser suffers permanent character death & lockout** |
| **Safety Confirmations** | ❌ None | ✅ **Strict safety warnings before accepting death duels** |
| **Global Realm Announcements** | ❌ None | ✅ **Server-wide broadcast of Mak'gora victories** |
| **Fleeing Protection** | ❌ Loser keeps gold | ✅ **Fleeing duel boundaries forfeits the wager pot** |

## 🎮 Blizzard-Style In-Game UI

When right-clicking another player and choosing **Duel** in the unit menu, the companion addon opens a native dialog offering 3 options:
1. **Normal:** Initiates a standard duel.
2. **Gold:** Prompts for the wager amount with gold coin icons and input box.
3. **Mak'gora (Death):** Displays the skull icon and safety confirmation for a duel to the death.

*(Commands are also supported for keybindings and macros).*

## 🎮 100% Native Blizzard UI (Zero Chat Commands Required)

Everything is seamlessly integrated into the stock World of Warcraft 3.3.5a UI without typing chat commands:

1. **Initiate Duel:** Right-click any player portrait and click **"Duelo"**.
2. **Select Type:** A native Blizzard popup appears with 3 buttons:
   - ⚔️ **Normal** (Standard duel)
   - 💰 **Gold** (Opens the gold input dialog with gold coin icons)
   - ☠️ **Mak'gora (Death)** (High-stakes duel to the death)
3. **Accept / Decline:** The challenged player receives the standard **native Blizzard duel dialog in the center of the screen** with:
   - Clear wager or Mak'gora warning text
   - Native **[ Aceptar ]** (Accept) and **[ Rechazar ]** (Decline) buttons!

*(Slash commands remain supported only as optional macros).*

## 💾 Client Addon Installation (Optional)

To enable the 3-option right-click selection menu and gold input popups:
1. Copy the `client-addon/HighStakesDuels` folder into your game's `Interface/AddOns/` directory:
   ```text
   World of Warcraft/Interface/AddOns/HighStakesDuels
   ```
2. Enable the addon in the game client Addon menu.

## 📋 Configuration Reference (`DuelWager.conf`)

| Setting | Default | Description |
| :--- | :---: | :--- |
| `DuelWager.Enable` | `1` | Master switch for the duel wager module. |
| `DuelWager.Gold.Enable` | `1` | Enables gold wagering in 1v1 duels. |
| `DuelWager.DeathDuel.Enable` | `1` | Enables Hardcore Death Duels (Mak'gora). |
| `DuelWager.MinLevel` | `10` | Minimum level required to initiate a Death Duel. |
| `DuelWager.DeathDuel.AnnounceWinner` | `1` | Broadcasts Mak'gora victories server-wide. |
| `DuelWager.TimeoutSeconds` | `60` | Seconds before a pending challenge expires. |
| `DuelWager.MaxDistance` | `30.0` | Maximum distance allowed to start the duel. |

## 🛠️ Server Installation

1. Place the module in `azerothcore-wotlk/modules/`:
   ```bash
   cd azerothcore-wotlk/modules
   git clone https://github.com/AlsoNotMehh/mod-high-stakes-duels.git
   ```
2. Re-run CMake and compile your server:
   ```bash
   cmake -B build
   cmake --build build --config Release
   ```
3. Copy `conf/DuelWager.conf.dist` to your `worldserver` configs directory as `DuelWager.conf` and customize as needed.

## ⭐ Show your support

If you find this module helpful for your server, please consider giving it a star on GitHub! It helps more developers in the AzerothCore community discover the project.

## 🤝 Credits

- **Author & Enhancements:** [AlsoNotMehh](https://github.com/AlsoNotMehh) ([Discord](https://discord.com/users/1063304041419001966) / [Email](mailto:itsbrayanrodriguez@gmail.com))
- **Framework:** [AzerothCore](https://www.azerothcore.org)

## 📜 License

This project is licensed under the [MIT License](LICENSE).
