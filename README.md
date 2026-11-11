# MealKit Manager

Hub app for the [MealKit](https://github.com/MealKit-Engine/MealKit) tool family: your projects, the engine versions they use, and the MealKit tools, in one window. Free, MIT.

> **Beta (v0.x).** MealKit is under active development: APIs and file formats may change between versions until v1.0, and projects are moved forward with the upgrade tool. Feedback and bug reports are welcome.

- **Projects**: recent and favorite projects (the same list the editor keeps), search, create a new project, import a folder, open it in its engine's editor, upgrade it when the engine is newer, show the folder.
- **Modules**: the source modules of the selected project and the module registry ([MealKit-Modules](https://github.com/MealKit-Engine/MealKit-Modules)). Add, update and remove run the project's engine `tools\module.ps1`.
- **Engines**: the engines your projects point to, plus folders you add. Builds an engine's editor when it is missing.
- **Tools**: MealKit tools found by their `mealkit-tool.txt` manifest (for example MkAudio), opened with the selected project.

The Manager does not link the engine. It talks to it only through files and command lines, so one Manager works with every engine version:

| What | Where |
|---|---|
| recent and favorite projects (shared with the editor) | `%LOCALAPPDATA%\MealKit\recent_projects.txt`, `favorite_projects.txt` |
| engines added in the Manager | `%LOCALAPPDATA%\MealKit\engines.txt` |
| language (`ko`, `en`) and extra tool folders | `%LOCALAPPDATA%\MealKit\editor_settings.txt` (`language`, `toolPath` lines) |
| a project's engine | `mealkit.project`: `engine <folder>`, `engineVersion <version>` |
| an engine | `VERSION`, `build\Editor\bin\MealKitEditor.exe`, `tools\new_project.ps1`, `tools\upgrade_project.ps1` |
| tools | `%LOCALAPPDATA%\MealKit\tools\<tool>\mealkit-tool.txt` |
| a project's modules and the registry | `tools\module.ps1 search -Tsv -Project <folder>` (one line per module), `add`/`update`/`remove`. The engine caches the registry list in `%LOCALAPPDATA%\MealKit\module-index.json` |

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
