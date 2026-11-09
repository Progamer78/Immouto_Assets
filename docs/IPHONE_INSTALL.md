# Install the iPhone test build from Windows

This is an early native test build. Opening, home, save/load, Training and Reading pass locally. It has not been tested on an iPhone, and the full game is unfinished. Requires iOS 15 or later.

## Download the IPA

Sign into the GitHub account with access to [the private repository's Releases](https://github.com/Progamer78/Immouto_Assets/releases). Download `Monochrome-unsigned.ipa` from the **full-game build** linked here once packaging finishes. The older approximately 1 MB files contain no game assets and cannot start the game on their own.

## Sign and install on Windows

1. Install [Sideloadly from its official site](https://sideloadly.io/). Follow its Windows instructions for the required web versions of iTunes and iCloud.
2. Connect the iPhone by USB, unlock it and trust the computer. Select it in Sideloadly.
3. Drag the downloaded IPA into Sideloadly, use your own Apple ID and start installation. Sideloadly signs the unsigned file before installing it. See its [official FAQ](https://sideloadly.io/faq.html).
4. On the phone, trust your developer profile under **Settings → General → VPN & Device Management**, if requested. On iOS 16 or later, enable **Settings → Privacy & Security → Developer Mode**, restart, and confirm. [Apple's instructions](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device).
5. Open **Monochrome**. A full development build may need time for its first local asset installation. The game runs offline afterward.

A free Apple account's signature lasts seven days; refresh with Sideloadly. Install updates over the existing app using the same account and bundle identifier to retain data. Avoid uninstalling the app between development builds. These signing/update details are documented in the [Sideloadly FAQ](https://sideloadly.io/faq.html); this project's device update persistence is still unverified.

## First checks and controls

Try the opening and New Game, then Training, Reading and Save. Tap the original game buttons; the bottom strip also provides arrows, Confirm, Cancel and Sub. In the title's Load menu, hover/select a slot then use the bottom Confirm button if a direct tap does not confirm it.

If it stops, keep the displayed error and the app's files in **Files → On My iPhone → Monochrome** for troubleshooting. Do not delete its saved data. Device startup, memory, audio and later gameplay are still being validated.
