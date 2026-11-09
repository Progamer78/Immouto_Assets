# PROJECT: Native iOS Port of a Specific WOLF RPG Editor Game

## Primary Objective

You are an expert systems programmer and game-engine developer specializing in WOLF RPG Editor, reverse engineering for interoperability, C++, native iOS development, ARM64, graphics programming, and game-engine portability.

Your assignment is to make the specific WOLF RPG Editor game provided in `OriginalGame/` fully playable on my iPhone.

**I want to play the original game natively on iOS, at its intended full speed, with the highest achievable compatibility, accuracy, and stability.**

This is NOT a universal WOLF RPG player. Only the supplied game needs to work.

I do not care which programming language, framework, graphics API, or architecture you choose, provided it delivers the best practical result.

Do not assume that the game must be converted to another particular game engine.

The end product must be a standalone iOS IPA that runs locally and does not require a Windows computer, streaming service, remote server, or internet connection.

## 1. Non-negotiable requirements

The finished application must:

- Execute natively on iOS ARM64.
- Run the original game's content and gameplay logic.
- Preserve all original graphics, audio, maps, events, dialogue, menus, and mechanics.
- Operate at the original game's intended speed.
- Support the entire game, not merely the title screen or opening area.
- Provide accurate movement, collisions, animations, and event timing.
- Support saving and loading.
- Provide functional touchscreen controls.
- Support physical controllers where practical.
- Work entirely offline.
- Produce an installable IPA through a reproducible iOS build process.

Do not substitute a visually similar remake for the actual game.

Do not simplify or omit content to make the task appear completed.

Do not use Windows emulation, remote execution, screen streaming, or a browser-based workaround as the final solution.

The objective is a genuine native iOS implementation of this game's required runtime functionality.

## 2. Inspect the actual game first

The complete original game is supplied in `OriginalGame/`.

Before selecting a technical implementation, inspect the game thoroughly.

Identify:

- Exact WOLF RPG Editor generation and version, if determinable.
- Whether the game uses Game.exe or GamePro.exe.
- Whether game resources are stored loosely or inside archives.
- Maps and event data.
- Common events and their dependencies.
- Databases, variables, and switches.
- Image, font, audio, and video formats.
- Rendering resolution and expected frame rate.
- Input requirements.
- Save format and state requirements.
- Custom gameplay systems.
- Windows-specific dependencies.
- Any encryption or protection that prevents authorized access to required data.

Do not assume the game follows standard RPG conventions.

WOLF RPG Editor games can implement complex custom systems through their event data.

Inspect those systems before deciding what must be recreated.

Preserve the original files. Perform extraction and transformation in separate directories.

If an essential resource is inaccessible, identify exactly what is missing rather than inventing replacement content.

## 3. Select the best technical approach

Evaluate multiple implementations before committing to one.

### Approach A: Port an existing WOLF-compatible runtime

Investigate existing open-source WOLF RPG runtime implementations.

Determine whether any provide sufficient support for this specific game's formats and event commands.

If an existing implementation is suitable and legally reusable, prefer adapting it for ARM64 iOS rather than recreating its functionality.

Possible investigation targets:

https://github.com/take-cheeze/rpg-maker-clone

https://github.com/djytw/wolf-rpg-formats

https://github.com/popupie/wolftools

Do not assume these tools are complete runtimes or support the supplied game's version.

Verify actual functionality and licensing.

### Approach B: Create a game-specific WOLF runtime

If no suitable complete runtime exists, implement the minimum necessary WOLF runtime functionality required by this game.

Preserve the original maps, event commands, databases, variables, and assets.

Use the original data directly or transform it into a documented intermediate representation.

Implement the actual event semantics rather than hardcoding every scene.

Only support features required by this title, but support those features correctly.

### Approach C: Direct native port

If a game-specific runtime is not practical, recreate the original game's necessary systems using a suitable native iOS-compatible framework.

Preserve the original content and functionality.

Use automated data conversion wherever possible.

Do not manually reconstruct large amounts of data that can be parsed and transformed programmatically.

### Decision criteria

Choose the approach with the best balance of:

1. Original-game compatibility and behavioral accuracy.
2. Reliable native iOS performance.
3. Engineering feasibility.
4. Reuse of existing working code.
5. Ability to reproduce the complete game.
6. Maintainability and efficient development.

Do not choose a framework because it is popular or convenient.

Choose it because it best solves the actual technical problem.

Explain your architectural decision concisely, then begin implementing it.

## 4. Native iOS implementation

Use native compiled code for the runtime.

C, C++, Objective-C++, Swift, Rust, or another suitable native implementation may be used.

Use Metal, SDL with an iOS-supported rendering backend, or another appropriate graphics solution.

Reuse libraries where practical.

Avoid developing graphics, audio, or input systems from scratch when reliable native libraries already provide them.

Preserve the original game's resolution, aspect ratio, visual effects, rendering order, and timing.

The application must correctly handle:

- iPhone screen dimensions.
- Display scaling.
- Application suspension and resumption.
- Audio interruptions.
- iOS filesystem restrictions.
- Persistent saves.
- Memory usage.
- Touchscreen input.
- Device orientation.

Aim for smooth performance without changing the original game's intended gameplay speed.

Do not assume faster rendering automatically means correct behavior.

## 5. Original game compatibility

Faithfully reproduce all systems required by this game.

These may include:

- Event triggers and execution.
- Parallel and automatically running events.
- Common event calls.
- Conditional branches and loops.
- Variables, switches, and databases.
- Player movement and collision.
- Map transfers and scrolling.
- Picture manipulation.
- Dialogue and choices.
- Menus and inventory.
- Combat or custom gameplay mechanics.
- Sound and music.
- Screen transitions.
- Save and load operations.

Preserve the distinction between blocking events, parallel events, and other event execution modes.

Preserve timing and ordering dependencies.

Do not ignore unsupported commands.

Inventory the event-command types used by the actual game and implement them systematically.

Use a shared interpreter or equivalent runtime logic wherever repeated commands occur.

Avoid hardcoding individual conversations, map transitions, or cutscenes if they can be executed from the game's original data.

## 6. Controls

The game must be playable using the iPhone touchscreen.

Implement controls appropriate to the actual game's requirements.

Possible controls include:

- Virtual directional pad.
- Confirm/action button.
- Cancel/menu button.
- Additional configurable buttons.
- Touch-to-click input.
- Virtual mouse cursor if needed.
- Bluetooth controller support.

Keep the original gameplay mechanics intact.

Allow useful control customization where practical.

Ensure controls do not unnecessarily obscure important game content.

## 7. Audio, graphics, and performance

Preserve original assets wherever technically possible.

Convert unsupported formats only when necessary.

Use appropriate decoding and rendering libraries.

Do not replace original graphics or audio with approximations unless absolutely required.

Preserve:

- Pixel alignment.
- Original animation timing.
- Layer ordering.
- Transparency and blending.
- Music looping.
- Sound effect timing.
- Correct text rendering.
- Japanese and other required character encodings.

Use hardware acceleration where practical.

Prevent unnecessary CPU usage, repeated allocations, and avoidable asset conversions during gameplay.

Performance must be tested rather than assumed.

## 8. Save and load compatibility

Implement reliable persistent saving on iOS.

Preserve all game state required to restore progress.

Test:

- Saving during normal gameplay.
- Closing and reopening the application.
- Loading previous progress.
- Event and variable persistence.
- Correct map and player restoration.

Compatibility with original Windows save files is desirable but secondary to reliable native iOS saving.

## 9. Testing requirements

The final game must be meaningfully tested against the original.

Where authorized and safe, run the original Windows version as a behavioral reference.

Use available gameplay recordings, screenshots, and other reference material when useful.

Verify:

- Game startup.
- Main menu.
- Starting a new game.
- Opening sequences.
- Movement and collisions.
- Dialogue and events.
- Map changes.
- Gameplay systems.
- Saving and loading.
- Progression across the game.
- Endgame and ending behavior where practical.

A title screen alone is not proof of success.

A working starting map alone is not proof of success.

Create reproducible tests for event execution and data parsing where possible.

Maintain a compatibility report identifying every known problem.

Do not claim the game is fully functional without sufficient gameplay testing.

## 10. Build the IPA

Produce a reproducible native iOS build.

My development computer runs Windows with VS Code and Codex.

You may use GitHub Actions on macOS runners for Xcode compilation.

Create the required project and CI workflow.

The workflow should support building a signed, installable IPA using appropriately configured signing credentials.

Do not embed signing secrets in source code.

If credentials are unavailable, generate the build project and unsigned build artifacts where supported, and document the remaining signing requirements.

Do not falsely claim that an IPA exists unless the build actually succeeds.

Keep the resulting application standalone, offline, and independent of the original Windows executable.

## 11. Development process

Follow this order:

**Phase 1: Inspection**

Analyze the game, identify formats, inventory events, and determine its dependencies.

**Phase 2: Runtime selection**

Evaluate existing WOLF-compatible implementations and choose the most effective native strategy.

**Phase 3: Data loading**

Implement correct loading of original game data and resources.

**Phase 4: Runtime implementation**

Make the original title screen and first playable sequence function with accurate events, movement, and rendering.

**Phase 5: Complete game functionality**

Implement all additional event commands and gameplay systems required by this game.

**Phase 6: iOS integration**

Add touch controls, screen scaling, persistent storage, audio, and application lifecycle support.

**Phase 7: IPA build and validation**

Compile, install when possible, test, diagnose failures, and improve compatibility.

Prioritize actual gameplay over decorative interface development.

Preserve working code and create Git checkpoints.

Do not overwrite or alter the original game files.

## 12. Working instructions for Codex

Work autonomously and make reasonable engineering decisions.

Inspect actual files instead of relying on assumptions.

Use available tooling to test implementations.

Do not waste time building generic systems unrelated to this particular game.

Prefer targeted fixes and reusable implementations.

Maintain a concise `PORT_STATUS.md` documenting:

- Confirmed game version.
- Chosen architecture and justification.
- Supported event commands.
- Supported gameplay systems.
- Verified functionality.
- Known incompatibilities.
- Current iOS build status.
- Remaining work.

Continue implementing after the initial analysis whenever feasible.

If an essential blocker prevents progress, identify it precisely and propose the best technically valid alternative.

Do not silently change the project's objectives.

## 13. Deliverables

Deliver:

1. Complete native iOS application source code.
2. Required runtime components.
3. Tools for parsing or converting the original game's data.
4. Original game assets integrated appropriately.
5. Functional touchscreen controls.
6. Reliable save and load functionality.
7. Reproducible GitHub Actions IPA build workflow.
8. Installation instructions.
9. Compatibility and testing results.
10. A built IPA when the environment and signing configuration permit.

## Final directive

The goal is not to demonstrate that a WOLF RPG game can theoretically run on iOS.

The goal is to make **this exact supplied game actually playable on my iPhone**.

Prioritize native execution, original-game fidelity, performance, stability, and complete gameplay compatibility.

Do not produce a generic demonstration or unrelated game.

Investigate the most practical native solution, implement it, and validate the result against the original game.