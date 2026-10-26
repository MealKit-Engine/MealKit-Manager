# MealKit Manager

Hub app for the [MealKit](https://github.com/MealKit-Engine/MealKit) tool family: your projects, the engine versions they use, and the MealKit tools, in one window. Free, MIT.

- **Projects**: recent and favorite projects (the same list the editor keeps), search, create a new project, import a folder, open it in its engine's editor, upgrade it when the engine is newer, show the folder.
- **Engines**: the engines your projects point to, plus folders you add. Builds an engine's editor when it is missing.
- **Tools**: MealKit tools found by their `mealkit-tool.txt` manifest (for example MealKit Audio), opened with the selected project.

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
MealKitManager [--engine <folder>] --new <Name> --path <folder>   create a project without a window
MealKitManager --open <project folder>                  open a project in its engine's editor
MealKitManager --frames N --capture shot.png            draw N frames, save the last one, quit
```

## Third-party

[SDL3](https://github.com/libsdl-org/SDL) (zlib) and [Dear ImGui](https://github.com/ocornut/imgui) (MIT) in `third_party/`, the same versions as MealKit Audio.
