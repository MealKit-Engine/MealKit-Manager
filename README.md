# MealKit Manager

Hub app for the [MealKit](https://github.com/MealKit-Engine/MealKit) tool family: your projects, the engine versions they use, and the MealKit tools, in one window. Free, MIT.

> **Beta (v0.x).** MealKit is under active development: APIs and file formats may change between versions until v1.0, and projects are moved forward with the upgrade tool. Feedback and bug reports are welcome.

- **Projects**: recent and favorite projects (the same list the editor keeps), search, create a new project, import a folder, open it in its engine's editor, upgrade it when the engine is newer, show the folder. A project's modules are managed in the editor's [Modules] window.
- **Settings > Engines**: the engines your projects point to, plus folders you add. Builds an engine's editor when it is missing. Installs engines from the release index (`scripts/install.ps1`: download, SHA-256 check, extract) without building them.
- **Tools**: every MealKit tool of the release index (MkAudio, MkUI, MkRender, ...) and every installed one in one list, each with one button: install (matched to an installed engine), update, or open with a project (the arrow picks another). Update All, uninstall, and a project's More > Open with Tool (installs first when needed).

The Manager does not link the engine. It talks to it only through files and command lines, so one Manager works with every engine version:

| What | Where |
|---|---|
| recent and favorite projects (shared with the editor) | `%LOCALAPPDATA%\MealKit\recent_projects.txt`, `favorite_projects.txt` |
| engines added in the Manager | `%LOCALAPPDATA%\MealKit\engines.txt` |
| language (`ko`, `en`) and extra tool folders | `%LOCALAPPDATA%\MealKit\editor_settings.txt` (`language`, `toolPath` lines) |
| a project's engine | `mealkit.project`: `engine <folder>`, `engineVersion <version>` |
| an engine | `VERSION`, `build\Editor\bin\MealKitEditor.exe`, `tools\new_project.ps1`, `tools\upgrade_project.ps1` |
| tools | `%LOCALAPPDATA%\MealKit\tools\<tool>\mealkit-tool.txt` |

When the MealKit editor is started without `--project`, it starts an installed Manager (`%LOCALAPPDATA%\MealKit\manager\MealKitManager.exe --engine <that engine>`) instead of its own project manager.

## Build

Windows, Visual Studio 2022 (C++ workload), CMake 3.25+, Ninja:

```bat
build.bat            :: build\bin\MealKitManager.exe + tests
build.bat install    :: also install to %LOCALAPPDATA%\MealKit\manager
```

Other platforms: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build`.

## Command line

```
MealKitManager [--engine <engine folder>] [--lang ko|en]
MealKitManager --list                                   print engines, projects and tools
MealKitManager [--engine <folder>] --new <Name> --path <folder> [--template arena|empty]   create a project without a window (empty: engine 0.7.21+)
MealKitManager --open <project folder>                  open a project in its engine's editor
MealKitManager (from an unzipped package, SETUP beside it)   install into %LOCALAPPDATA%\MealKit\manager with Start menu + desktop shortcuts, start it
MealKitManager --releases                               engines in the release index (MEALKIT_RELEASE_INDEX, url or path)
MealKitManager --install-tool <name>                    install or update a tool from the index into %LOCALAPPDATA%\MealKit\tools\<name> (refused while it or the editor runs)
MealKitManager --install-engine <version>               download, check and install one into %LOCALAPPDATA%\MealKit\engines\<version>
MealKitManager --uninstall [--yes]                      remove the installed Manager, its shortcuts and Installed apps entry (engines stay)
MealKitManager --frames N --capture shot.png            draw N frames, save the last one, quit
```

## Working on it

- It doesn't link the engine: it talks to the engine, the editor and the tools only through files and command lines, and calls the engine's own scripts (`tools\new_project.ps1`, `upgrade_project.ps1`, `build.bat`) as they are. A feature those scripts lack is built in the engine first.
- The user folder, tool manifests, module list lines and the live protocol version come from [MealKit-Contract](https://github.com/MealKit-Engine/MealKit-Contract) (`third_party/mkcontract`); change them there and copy a tag in with its `tools/vendor.ps1`.
- Data is in `src/Catalog.*` (no window, under test), running other programs in `src/Job.*`, the screen in `src/App.*`. SDL and Dear ImGui stay at the same versions as MkAudio.
- Contributing: see the [MealKit contributing guide](https://github.com/MealKit-Engine/.github/blob/main/CONTRIBUTING.md).

## Third-party

[SDL3](https://github.com/libsdl-org/SDL) (zlib) and [Dear ImGui](https://github.com/ocornut/imgui) (MIT) in `third_party/`, the same versions as MkAudio.
[mkcontract](https://github.com/MealKit-Engine/MealKit-Contract) (MIT) in `third_party/mkcontract/` reads and writes the shared files in the table above, the same code the MealKit editor uses
(its `docs/CONTRACT.md` is the spec). Update it with that repo's `tools/vendor.ps1 -To third_party/mkcontract`.
