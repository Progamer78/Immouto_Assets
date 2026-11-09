# Install the iPhone test build from Windows

This is an early native test build. Opening, home, save/load, Training and Reading pass locally. The user confirmed that the full IPA runs on an iPhone; the complete game is unfinished. Requires iOS 15 or later.

## Update the installed game

Download **`Monochrome-unsigned.ipa`** from **[iOS build 7 - iPhone feedback fixes](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-7-1)**. This update is **1,002,775 bytes (about 1 MB)** and reuses the assets installed by full build 5. Its Actions build passed in 43 seconds; the downloaded archive passed ZIP, SHA-256 and ARM64 checks. The updated version still needs testing on the phone.

Install it with Sideloadly **over the existing Monochrome app**, using the same Apple ID and bundle identifier as before. **Do not uninstall the current app**: its Documents directory contains the game assets and saved slots. After updating, use the original title's Continue/Load option to select your save. The new bottom Menu button offers Return to title; controls now fit within the iPhone safe area.

This small IPA requires the existing asset cache. For a first installation or a device without that cache, install the full build below before applying build 7. Same-app data retention depends on the signing/install path and still needs confirmation with this update.

## First installation: full game IPA

Sign into the GitHub account with access to your private repository. Open **[iOS development build 5 — includes the game](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-5-1)** and download **`Monochrome-unsigned.ipa`** under Assets. It is **1,005,677,313 bytes (about 1 GB)**. The older approximately 1 MB files contain no game assets and cannot start the game on their own.

## Sign and install on Windows

1. Install [Sideloadly from its official site](https://sideloadly.io/). Follow its Windows instructions for the required web versions of iTunes and iCloud.
2. Connect the iPhone by USB, unlock it and trust the computer. Select it in Sideloadly.
3. Drag the downloaded IPA into Sideloadly, use your own Apple ID and start installation. Sideloadly signs the unsigned file before installing it. See its [official FAQ](https://sideloadly.io/faq.html).
4. On the phone, trust your developer profile under **Settings → General → VPN & Device Management**, if requested. On iOS 16 or later, enable **Settings → Privacy & Security → Developer Mode**, restart, and confirm. [Apple's instructions](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device).
5. Open **Monochrome**. A full development build may need time for its first local asset installation. The game runs offline afterward.

A free Apple account's signature lasts seven days; refresh with Sideloadly. Install updates over the existing app using the same account and bundle identifier to retain data. Avoid uninstalling the app between development builds. These signing/update details are documented in the [Sideloadly FAQ](https://sideloadly.io/faq.html); this project's device update persistence is still unverified.

## First checks and controls

Try the opening and New Game, then Training, Reading and Save. Tap the original game buttons; the updated bottom strip also provides arrows, Confirm, Cancel, Sub and Menu. Menu offers Return to title, with confirmation before discarding unsaved progress. Normal launches show the title so you can load a saved slot. In the title's Load menu, hover/select a slot then use the bottom Confirm button if a direct tap does not confirm it.

If it stops, keep the displayed error and the app's files in **Files → On My iPhone → Monochrome** for troubleshooting. Do not delete its saved data. Device startup, memory, audio and later gameplay are still being validated.
