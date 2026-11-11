# Install the iPhone test build from Windows

This is an early native test build. Opening, home, save/load, Training and Reading pass locally. The user confirmed that the full IPA runs on an iPhone; the complete game is unfinished. Requires iOS 15 or later.

## Update the installed game

Download **`Monochrome-unsigned.ipa`** from **[iOS build 14 - battle fixes, save states and cheats](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-14-1)**. This update is **1,055,615 bytes (about 1 MB)** and reuses full build 5 assets. Actions built it in 40 seconds; the downloaded archive passed ZIP CRC, SHA-256, ARM64, iOS 15 and bundle checks. It fixes attack animations, enemy defeat rendering and the battle meter reset. The reported fight passes through completion locally, including another run with guard and enemy actions. It adds separate save states and optional cheats. The complete game is still unfinished; the new iPhone menu controls need device verification.

Install it with Sideloadly **over the existing Monochrome app**, using the same Apple ID and bundle identifier as before. **Do not uninstall the current app**: its Documents directory contains the game assets and saved slots. After updating, use the original title's Continue/Load option to select your save. Controls start hidden so the game picture can use more space. Tap the floating three-dot button to show/hide the bottom controls; drag that button to move it. Its position and visibility are saved. The bottom Menu button offers Return to title and **Enable mouse mode**. In mouse mode, drag like a trackpad to move the visible cursor, lift and tap to click, or hold without moving for at least half a second and release to right-click. Use **Menu -> Use direct touch** to switch back. The mode is saved separately from game slots. Choose **Menu -> Show FPS monitor** to see FPS, frame time, CPU/game-loop time, the recent peak frame time and slow frames. Choose Hide FPS monitor to turn it off. This setting persists. The target is the original game's 60 FPS; the monitor does not measure GPU time. The × button now sends the game's Cancel/right-click input, including to custom menus.

This small IPA requires the existing asset cache. For a first installation or a device without that cache, install the full build below before applying build 14. Keep using the same signing/install settings to preserve the app data.

## Separate save states and cheats

Show the controls with the floating three-dot button, then choose **Menu -> Save states -> Save state** and a slot (1-3). Choose Load state to restore it, including dialogue, choices and animations. These slots are separate from the game's normal saves. Loading a state restores game memory; it does not rewind normal save files or cheat settings. Normal launches still open the title screen.

To send a state for troubleshooting, open **Files -> On My iPhone -> Monochrome -> states**, long-press **slot1.json**, **slot2.json** or **slot3.json**, and choose **Share**. The folder appears after your first state save. You can attach that JSON together with `last-error.json` when reporting a problem.

Tap the movable **C** button for the cheat panel. Search by name, ID or value. Choose **New search** once, then play and use **Refine results** to narrow those matches; Changed/Unchanged/Increased/Decreased compare against the last scan. Use **Any / baseline** first when the value is unknown. Select a variable to Set value, Freeze, Pin or Rename it. The panel preserves results when closed. Toggle Cheats OFF to release freezes. Pins and freezes remain in `cheats.json`, separate from saves.

The control strip includes **A** and **B**, sending the original game's literal keys. For PC saves, download the Windows converter from the latest release and follow [PC save conversion/import](PC_SAVES.md). Copy its output into the app's `imports` folder, then use **Menu -> Import PC save**.

## First installation: full game IPA

Sign into the GitHub account with access to your private repository. Open **[iOS development build 5 — includes the game](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-5-1)** and download **`Monochrome-unsigned.ipa`** under Assets. It is **1,005,677,313 bytes (about 1 GB)**. The older approximately 1 MB files contain no game assets and cannot start the game on their own.

## Sign and install on Windows

1. Install [Sideloadly from its official site](https://sideloadly.io/). Follow its Windows instructions for the required web versions of iTunes and iCloud.
2. Connect the iPhone by USB, unlock it and trust the computer. Select it in Sideloadly.
3. Drag the downloaded IPA into Sideloadly, use your own Apple ID and start installation. Sideloadly signs the unsigned file before installing it. See its [official FAQ](https://sideloadly.io/faq.html).
4. On the phone, trust your developer profile under **Settings → General → VPN & Device Management**, if requested. On iOS 16 or later, enable **Settings → Privacy & Security → Developer Mode**, restart, and confirm. [Apple's instructions](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device).
5. Open **Monochrome**. A full development build may need time for its first local asset installation. The game runs offline afterward.

A free Apple account's signature lasts seven days; refresh with Sideloadly. Install updates over the existing app using the same account and bundle identifier to retain data. Avoid uninstalling the app between development builds. These signing/update details are documented in the [Sideloadly FAQ](https://sideloadly.io/faq.html); the user has successfully installed thin updates over the existing game.

## First checks and controls

Tap the floating three-dot button to show the controls, or drag it somewhere comfortable. Try the opening and New Game, then Training, Reading and Save. Tap the original game buttons; the updated bottom strip also provides arrows, Confirm, Cancel, Sub and Menu. Menu offers mouse mode and Return to title, with confirmation before discarding unsaved progress. Normal launches show the title so you can load a saved slot. In the title's Load menu, hover/select a slot then use the bottom Confirm button if a direct tap does not confirm it.

If it stops, keep the displayed error and the app's files in **Files → On My iPhone → Monochrome** for troubleshooting. Do not delete its saved data. Device startup, memory, audio and later gameplay are still being validated.
