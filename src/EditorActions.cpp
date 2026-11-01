// Editor actions: menu/toolbar commands, Play Mode, graphics API, projects and per-frame update.

#include "EditorWindow.h"

#include <fstream>

namespace dxs::editor {

bool Editor::ShellCommand(int id) {
    if (id == ID_EXIT) {
        SendMessageW(window, WM_CLOSE, 0, 0);
        return true;
    }
    if (id >= ID_MENU_BAR_FIRST && id < ID_MENU_BAR_FIRST + MENU_COUNT) {
        OpenMainMenu(id - ID_MENU_BAR_FIRST);
        return true;
    }
    if (id == ID_DRAW_MODE_MENU || id == ID_ASPECT_MENU) {
        ShowViewDropdown(id);
        return true;
    }
    if (id >= ID_DRAW_MODE_FIRST && id < ID_DRAW_MODE_FIRST + 3) {
        SetDrawMode(id - ID_DRAW_MODE_FIRST);
        return true;
    }
    if (id >= ID_ASPECT_FIRST && id < ID_ASPECT_FIRST + ASPECT_COUNT) {
        SetGameAspect(id - ID_ASPECT_FIRST);
        return true;
    }
    if (id == ID_VIEW_TAB_FIRST || id == ID_VIEW_TAB_FIRST + 1) {
        SelectViewTab(id == ID_VIEW_TAB_FIRST + 1);
        return true;
    }
    if (id >= ID_PANEL_TAB_FIRST && id < ID_PANEL_TAB_FIRST + BOTTOM_TAB_COUNT) {
        SelectBottomTab(id - ID_PANEL_TAB_FIRST);
        return true;
    }
    if (id == ID_SHOW_CONSOLE) {
        auto& p = panels[3];
        p.hidden = p.collapsed = false;
        ShowWindow(p.window, p.floating ? SW_RESTORE : SW_SHOW);
        SelectBottomTab(1);
        Layout();
        return true;
    }
    if (id == ID_CLEAR_CONSOLE) {
        ClearConsolePanel(consolePanel);
        return true;
    }
    if (id == ID_PAUSE) {
        TogglePause();
        return true;
    }
    if (id == ID_STEP) {
        StepFrame();
        return true;
    }
    if (id >= ID_GRAPHICS_D3D12 && id <= ID_GRAPHICS_D3D9) {
        SwitchGraphicsApi(id == ID_GRAPHICS_D3D12   ? GraphicsApi::DIRECT3D12
                          : id == ID_GRAPHICS_D3D11 ? GraphicsApi::DIRECT3D11
                                                    : GraphicsApi::DIRECT3D9);
        return true;
    }
    if (id == ID_GRAPHICS_MENU || id == ID_LAYOUT_MENU) {
        ShowToolbarDropdown(id);
        return true;
    }
    if (id >= ID_ADD_COMPONENT_MENU_FIRST && id <= ID_ADD_COMPONENT_MENU_LAST) {
        const auto names = RegisteredComponentNames();
        const size_t index = static_cast<size_t>(id - ID_ADD_COMPONENT_MENU_FIRST);
        if (index < names.size()) AttachToSelection(names[index]);
        return true;
    }
    if (id == ID_OPEN_README) {
        const auto readme = base / L"README.md";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", readme.c_str(), nullptr, base.c_str(), SW_SHOWNORMAL)) <=
            32)
            Error(window, "Could not open README.md.");
        return true;
    }
    if (id == ID_ABOUT) {
        const std::wstring text = L"DxStudio\n\nUnity / Unreal style C++20 editor.\nRenderer: " +
                                  Wide(GraphicsApiDisplayName(renderer.Api())) +
                                  L"\nSwitch backends in Edit > Project Settings > Graphics API.";
        MessageBoxW(window, text.c_str(), L"About DxStudio", MB_OK | MB_ICONINFORMATION);
        return true;
    }
    if (id >= ID_SECTION_TOGGLE && id < ID_SECTION_TOGGLE + 3 && IsOverTitlebarMenu(groupLabels[id - ID_SECTION_TOGGLE])) {
        ShowTitlebarMenu(groupLabels[id - ID_SECTION_TOGGLE]);
        return true;
    }
    if (id >= ID_COMPONENT_HEADER_FIRST && id < ID_COMPONENT_HEADER_FIRST + COMPONENT_VIEW_LIMIT) {
        for (auto& view : vComponentViews) {
            if (GetDlgCtrlID(view.hHeader) != id) continue;
            if (IsOverTitlebarMenu(view.hHeader)) ShowTitlebarMenu(view.hHeader);
            else {
                collapsedComponents[view.sName] = !collapsedComponents[view.sName];
                LayoutPanel(2);
            }
            break;
        }
        return true;
    }
    if (id == ID_ADD_COMPONENT) {
        ShowAddComponentMenu();
        return true;
    }
    if (id >= ID_SECTION_TOGGLE && id < ID_SECTION_TOGGLE + 3) {
        aSectionOpen[id - ID_SECTION_TOGGLE] = !aSectionOpen[id - ID_SECTION_TOGGLE];
        LayoutPanel(2);
        return true;
    }
    if (id == ID_RESET_LAYOUT) {
        for (int i = 0; i < 4; ++i) {
            if (panels[i].floating) FloatPanel(i);
            panels[i].hidden = panels[i].collapsed = false;
        }
        for (int slot = 0; slot < SLOT_COUNT; ++slot) aSlotPanel[slot] = slot;
        Layout();
        return true;
    }
    if (id == ID_SAVE_LAYOUT) {
        const auto path = projectRoot / L"EditorLayout.dxl";
        auto temp = path;
        temp += L".tmp";
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            Error(window, "Could not save the workspace layout.");
            return true;
        }
        file << "DXLAYOUT 2 " << nDockLeft << ' ' << nDockRight << ' ' << nDockProject << '\n';
        for (const auto& panel : panels)
            file << (panel.floating ? 1 : 0) << ' ' << (panel.hidden ? 1 : 0) << ' ' << (panel.collapsed ? 1 : 0) << '\n';
        for (const int panelIndex : aSlotPanel) file << panelIndex << ' ';
        file << '\n';
        file.close();
        if (!file || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            Error(window, "Could not finalize the workspace layout.");
            return true;
        }
        SetStatus(L"Workspace layout saved in the active project.");
        return true;
    }
    if (id == ID_LOAD_LAYOUT) {
        const auto path = projectRoot / L"EditorLayout.dxl";
        std::ifstream file(path, std::ios::binary);
        std::string magic;
        unsigned version{};
        int left{}, right{}, project{}, floating[4]{}, hidden[4]{}, collapsed[4]{};
        int slots[SLOT_COUNT] = {0, 1, 2, 3};
        if (!file || !(file >> magic >> version >> left >> right >> project) || magic != "DXLAYOUT" || version < 1 || version > 2) {
            Error(window, "No valid saved workspace layout was found.");
            return true;
        }
        for (int i = 0; i < 4; ++i)
            if (!(file >> floating[i] >> hidden[i] >> collapsed[i]) || (floating[i] & ~1) || (hidden[i] & ~1) ||
                (collapsed[i] & ~1)) {
                Error(window, "The saved workspace layout is invalid.");
                return true;
            }
        if (version >= 2) {
            bool seen[SLOT_COUNT]{};
            for (int& panelIndex : slots) {
                if (!(file >> panelIndex) || panelIndex < 0 || panelIndex >= SLOT_COUNT || seen[panelIndex]) {
                    Error(window, "The saved workspace layout is invalid.");
                    return true;
                }
                seen[panelIndex] = true;
            }
        }
        std::copy(std::begin(slots), std::end(slots), std::begin(aSlotPanel));
        nDockLeft = left;
        nDockRight = right;
        nDockProject = project;
        nInspectorScroll = 0;
        for (int i = 0; i < 4; ++i) {
            if (panels[i].floating != (floating[i] != 0)) FloatPanel(i);
            panels[i].hidden = hidden[i] != 0;
            panels[i].collapsed = collapsed[i] != 0;
        }
        Layout();
        SetStatus(L"Saved workspace layout restored.");
        return true;
    }
    int index = -1;
    if (id >= ID_PANEL_SHOW && id < ID_PANEL_SHOW + 4) {
        index = id - ID_PANEL_SHOW;
        auto& p = panels[index];
        p.hidden = p.collapsed = false;
        ShowWindow(p.window, p.floating ? SW_RESTORE : SW_SHOW);
    } else if (id >= ID_PANEL_FLOAT && id < ID_PANEL_FLOAT + 4) {
        FloatPanel(id - ID_PANEL_FLOAT);
        return true;
    } else if (id >= ID_PANEL_MINIMIZE && id < ID_PANEL_MINIMIZE + 4) {
        index = id - ID_PANEL_MINIMIZE;
        auto& p = panels[index];
        if (p.floating)
            ShowWindow(p.window, IsIconic(p.window) ? SW_RESTORE : SW_MINIMIZE);
        else
            p.collapsed = !p.collapsed;
    } else if (id >= ID_PANEL_HIDE && id < ID_PANEL_HIDE + 4) {
        index = id - ID_PANEL_HIDE;
        panels[index].hidden = true;
    }
    if (index < 0) return false;
    Layout();
    return true;
}

void Editor::ShowObjectMenu(POINT point) {
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_ADD_EMPTY, L"Create Empty");
    AppendPrimitiveMenus(menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const UINT availability = vSelectedIndices.empty() ? MF_GRAYED : 0;
    AppendMenuW(menu, MF_STRING | availability, ID_FRAME_SELECTION, L"Frame selection\tF");
    AppendMenuW(menu, MF_STRING | availability, ID_DUPLICATE, L"Duplicate\tCtrl+D");
    AppendMenuW(menu, MF_STRING | availability, ID_DELETE, L"Delete selection");
    const int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (action) Command(action, BN_CLICKED);
}

void Editor::ShowComponentMenu(POINT point) {
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
    auto& entity = scene.vEntities[nSelected];
    const int selectedRow = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
    const int offset = entity.renderer == Entity::RendererKind::None ? 0 : 1;
    const int componentIndex = selectedRow - offset;
    const bool custom = componentIndex >= 0 && static_cast<size_t>(componentIndex) < entity.vComponents.size();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (selectedRow == LB_ERR ? MF_GRAYED : 0), ID_REMOVE_COMPONENT, L"Remove Component");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!custom || componentIndex == 0 ? MF_GRAYED : 0), ID_MOVE_COMPONENT_UP, L"Move Up");
    AppendMenuW(menu,
                MF_STRING | (!custom || static_cast<size_t>(componentIndex + 1) >= entity.vComponents.size() ? MF_GRAYED : 0),
                ID_MOVE_COMPONENT_DOWN, L"Move Down");
    const int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (action) Command(action, BN_CLICKED);
}

void Editor::RefreshTransport() {
    theme::SetButtonIcon(controls[9], bPlaying ? theme::Icon::STOP : theme::Icon::PLAY);
    for (HWND button : {controls[9], pauseButton, stepButton})
        InvalidateRect(button, nullptr, FALSE);
    // The toolbar tints blue while playing, as a clear Play Mode cue.
    RECT r{};
    GetClientRect(window, &r);
    RECT top{0, 0, r.right, TOP_CHROME_HEIGHT};
    InvalidateRect(window, &top, TRUE);
    for (HWND button : menuButtons)
        InvalidateRect(button, nullptr, FALSE);
    for (HWND button : {controls[4], controls[5], controls[6], controls[7], controls[10], graphicsButton, layoutButton})
        InvalidateRect(button, nullptr, FALSE);
}

void Editor::TogglePlay() {
    if (!bPlaying) {
        if (!Apply()) return;
        editSceneSnapshot = scene;
        editCameraSnapshot = camera;
        bPlaying = true;
        bPaused = bStepRequested = false;
        nPlayTick = GetTickCount64();
        Time::Reset();
        SetWindowTextW(controls[9], L"Stop");
        renderer.SetCamera(ActiveGameCamera(scene));
        SetStatus(L"PLAY MODE: project components are running. Press Stop or Ctrl+P to restore the edited scene.");
        EngineEvents::Get().onEnterPlayMode.Broadcast(scene);
        SelectViewTab(true);
    } else {
        bPlaying = false;
        bPaused = bStepRequested = false;
        EngineEvents::Get().onExitPlayMode.Broadcast(scene);
        scene = std::move(editSceneSnapshot);
        camera = editCameraSnapshot;
        nPlayTick = 0;
        SetWindowTextW(controls[9], L"Play");
        renderer.SetCamera(camera);
        vSelectedIndices.erase(
            std::remove_if(vSelectedIndices.begin(), vSelectedIndices.end(),
                           [&](int index) { return index < 0 || static_cast<size_t>(index) >= scene.vEntities.size(); }),
            vSelectedIndices.end());
        nSelected = vSelectedIndices.empty() ? -1 : vSelectedIndices.back();
        Populate();
        SelectViewTab(false);
        SetStatus(L"Play Mode stopped. Runtime changes were discarded.");
    }
    RefreshTransport();
}

void Editor::TogglePause() {
    bPaused = !bPaused;
    bStepRequested = false;
    nPlayTick = GetTickCount64();
    EngineEvents::Get().onPauseChanged.Broadcast(bPaused);
    SetStatus(bPaused ? L"Paused. Step advances one frame."
                     : (bPlaying ? L"PLAY MODE resumed." : L"Pause armed: Play starts paused."));
    RefreshTransport();
}

void Editor::StepFrame() {
    if (!bPlaying) TogglePlay();
    if (!bPlaying) return;
    if (!bPaused) {
        bPaused = true;
        EngineEvents::Get().onPauseChanged.Broadcast(true);
    }
    bStepRequested = true;
    SetStatus(L"Stepped one frame.");
    RefreshTransport();
}

void Editor::SwitchGraphicsApi(GraphicsApi api) {
    if (api == projectSettings.graphicsApi && renderer.IsInitialized() && renderer.Api() == api) return;
    projectSettings.graphicsApi = api;
    std::string error;
    if (!SaveProjectSettings(projectRoot / PROJECT_SETTINGS_FILE_NAME, projectSettings, error)) Debug::LogWarning(error);
    if (InitRenderer()) {
        const auto message = std::wstring(L"Graphics API: ") + Wide(GraphicsApiDisplayName(renderer.Api())) +
                             L". Exported games use this setting too.";
        SetStatus(message.c_str());
    }
}

void Editor::UpdateGraphicsBadge() {
    SetWindowTextW(
        graphicsButton,
        Wide(GraphicsApiShortName(renderer.IsInitialized() ? renderer.Api() : projectSettings.graphicsApi)).c_str());
    InvalidateRect(graphicsButton, nullptr, FALSE);
    Layout(); // the badge width follows its label
}

void Editor::AttachToSelection(const std::string& name) {
    if (vSelectedIndices.size() != 1 || nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) {
        Error(window, "Select exactly one GameObject to add a component.");
        return;
    }
    std::string error;
    if (!AttachComponent(scene.vEntities[nSelected], name, error)) {
        Error(window, error);
        return;
    }
    Inspect();
    SetStatus(L"Component attached. Save or export to keep it.");
}

void Editor::LoadSettings() {
    projectSettings = {};
    std::string error;
    const auto path = projectRoot / PROJECT_SETTINGS_FILE_NAME;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !LoadProjectSettings(path, projectSettings, error)) Debug::LogWarning(error);
}

bool Editor::SwitchProject(const std::filesystem::path& descriptor) {
    if (bPlaying) TogglePlay();
    std::filesystem::path root;
    std::string error;
    if (!LoadProjectDescriptor(descriptor, root, error)) {
        Error(window, error);
        return false;
    }
    Scene loaded;
    if (!LoadScene(root / L"scene.dxs", loaded, error)) {
        Error(window, error);
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(root / L"Default.hlsl", ec) || ec) {
        Error(window, "The project does not contain Default.hlsl.");
        return false;
    }
    renderer.Shutdown();
    bRendererReady = false;
    if (projectPanel) DestroyWindow(projectPanel);
    projectRoot = root;
    projectFile = descriptor;
    scene = std::move(loaded);
    LoadSettings();
    EnsureProjectPrimitives();
    projectPanel = CreateProjectPanel(panels[3].window, instance, projectRoot, base);
    nSelected = scene.vEntities.empty() ? -1 : 0;
    vSelectedIndices = nSelected < 0 ? std::vector<int>{} : std::vector<int>{nSelected};
    bSceneProtected = false;
    Populate();
    Layout();
    const auto title = L"DxStudio  —  " + projectFile.stem().wstring();
    SetWindowTextW(window, title.c_str());
    if (!InitRenderer()) return false;
    SetStatus(L"Project opened. Project C++ classes require a Debug or Release rebuild.");
    return true;
}

bool Editor::NewProject() {
    std::filesystem::path requested;
    std::string error;
    if (!ChooseProjectDescriptor(window, true, base, requested, error)) {
        if (!error.empty()) Error(window, error);
        return false;
    }
    const auto projectName = requested.stem().wstring();
    if (projectName.empty() || projectName == L"." || projectName == L"..") {
        Error(window, "Enter a valid project name.");
        return false;
    }
    const auto root = requested.parent_path() / projectName;
    const auto descriptor = root / (projectName + L".dxproject");
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        Error(window, "A folder with that project name already exists. Choose another name.");
        return false;
    }
    for (const auto& folder : {root / L"Scripts", root / L"Assets" / L"Models", root / L"Assets" / L"Materials",
                               root / L"Assets" / L"Textures", root / L"Assets" / L"Animations"}) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            Error(window, "Could not create the project folders: " + ec.message());
            return false;
        }
    }
    std::filesystem::copy_file(base / L"project" / L"Default.hlsl", root / L"Default.hlsl", ec);
    if (ec) {
        Error(window, "Could not copy the default shader: " + ec.message());
        return false;
    }
    Material defaultMaterial;
    defaultMaterial.sName = "Default";
    if (!SaveMaterial(root / L"Assets" / L"Materials" / L"Default.dxmat", defaultMaterial, error)) {
        Error(window, error);
        return false;
    }
    Scene fresh;
    fresh.projectRoot = root;
    Entity mainCamera;
    mainCamera.sName = "Main Camera";
    mainCamera.renderer = Entity::RendererKind::None;
    mainCamera.draw.position = {0.0f, 2.0f, -6.0f};
    mainCamera.vComponents = {"Camera"};
    mainCamera.componentProperties["Camera"] = {
        {"Active", "true"}, {"Orthographic", "false"}, {"Field of View", "60"}, {"Near Clip", "0.1"}, {"Far Clip", "1000"}};
    fresh.vEntities.push_back(std::move(mainCamera));
    if (!SaveScene(root / L"scene.dxs", fresh, error) || !SaveProjectDescriptor(descriptor, root, error)) {
        Error(window, error);
        return false;
    }
    std::wstring generationError;
    if (!GenerateVisualStudioProject(root, base, generationError)) {
        Error(window, Utf8(generationError));
        return false;
    }
    return SwitchProject(descriptor);
}

bool Editor::RunBuildScript(const wchar_t* scriptName, const wchar_t* label) {
    const auto script = base / scriptName;
    if (!std::filesystem::is_regular_file(script)) {
        Error(window, "The build script is missing.");
        return false;
    }
    if (IsTaskRunning()) {
        Error(window, "A build is already running (Build > Cancel build stops it).");
        return false;
    }
    std::wstring command = L"cmd.exe /d /s /c \"\"" + script.wstring() + L"\" \"" + projectRoot.wstring() + L"\"\"";
    // Expected Gradle task count: from the previous successful build (read
    // before the script overwrites the log), else a typical full build.
    const int nPreviousTasks = CountBuildLogTasks(base / std::filesystem::path(scriptName).stem() / L"build.log");
    const int nExpectedTasks = nPreviousTasks > 0 ? nPreviousTasks : 40;
    // The script, Gradle and its Java processes go into one job object, so
    // Cancel (or closing the editor) stops the whole tree.
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, base.c_str(),
                        &startup, &process)) {
        if (hJob) CloseHandle(hJob);
        Error(window, "Could not start the build process.");
        return false;
    }
    if (hJob) AssignProcessToJobObject(hJob, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    const HANDLE hProcess = process.hProcess;
    auto pExitCode = std::make_shared<DWORD>(1);
    const auto log = base / std::filesystem::path(scriptName).stem() / L"build.log";
    const std::wstring sLabel = label;
    StartBackgroundTask(
        std::wstring(L"Building ") + label,
        [hProcess, pExitCode] {
            WaitForSingleObject(hProcess, INFINITE);
            GetExitCodeProcess(hProcess, pExitCode.get());
        },
        [this, hProcess, pExitCode, log, sLabel] {
            CloseHandle(hProcess);
            if (pTask && pTask->bCanceled) {
                // The killed script could not unmap its temporary drive letter (see build-android.cmd).
                const auto driveFile = log.parent_path() / L"temp-drive.txt";
                std::wifstream in(driveFile);
                std::wstring sDrive;
                if (in >> sDrive && sDrive.size() == 2 && sDrive[1] == L':') DefineDosDeviceW(DDD_REMOVE_DEFINITION, sDrive.c_str(), nullptr);
                in.close();
                std::error_code ec;
                std::filesystem::remove(driveFile, ec);
                SetStatus((sLabel + L" build canceled.").c_str());
                Debug::LogWarning(Utf8(sLabel) + " build canceled.");
                return;
            }
            if (*pExitCode != 0) {
                const std::string message = "The build failed.\n\n" + SummarizeBuildLog(log) + "\nFull log: " + Utf8(log.wstring());
                Debug::LogError(message);
                SetStatus(L"Build failed.");
                Error(window, message);
                return;
            }
            const auto completed = sLabel + L" build completed.";
            SetStatus(completed.c_str());
            Debug::Log(Utf8(completed));
            MessageBoxW(window, completed.c_str(), L"Build", MB_OK | MB_ICONINFORMATION);
        },
        hJob);
    if (pTask) pTask->progress = [log, nExpectedTasks] { return BuildLogProgress(log, nExpectedTasks); };
    return true;
}

// The useful part of a failed build log: Gradle's "What went wrong" block and
// ERROR lines; otherwise the last lines.
std::string SummarizeBuildLog(const std::filesystem::path& log) {
    std::ifstream file(log, std::ios::binary);
    std::vector<std::string> vLines;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 220) line = line.substr(0, 217) + "...";
        vLines.push_back(std::move(line));
    }
    std::vector<std::string> vSummary;
    for (size_t nIndex = 0; nIndex < vLines.size(); ++nIndex) {
        if (vLines[nIndex].find("What went wrong") == std::string::npos) continue;
        for (size_t nNext = nIndex + 1; nNext < vLines.size() && nNext <= nIndex + 8; ++nNext) {
            if (vLines[nNext].rfind("* Try", 0) == 0) break;
            if (!vLines[nNext].empty()) vSummary.push_back(vLines[nNext]);
        }
        break; // the first failure is the cause
    }
    for (const auto& line : vLines)
        if (line.rfind("ERROR:", 0) == 0 && std::find(vSummary.begin(), vSummary.end(), line) == vSummary.end()) vSummary.push_back(line);
    if (vSummary.empty()) {
        for (const auto& line : vLines)
            if (!line.empty()) vSummary.push_back(line);
        if (vSummary.size() > 12) vSummary.erase(vSummary.begin(), vSummary.end() - 12);
    }
    std::string sText;
    for (const auto& line : vSummary) sText += line + "\n";
    return sText;
}

bool Editor::StartBackgroundTask(std::wstring sLabel, std::function<void()> work, std::function<void()> onDone, HANDLE hJob) {
    if (IsTaskRunning()) {
        if (hJob) CloseHandle(hJob);
        return false;
    }
    pTask = std::make_unique<BackgroundTask>();
    pTask->sLabel = std::move(sLabel);
    pTask->nStartTick = GetTickCount64();
    pTask->hJob = hJob;
    pTask->onDone = std::move(onDone);
    const HWND hWindow = window;
    pTask->worker = std::thread([work = std::move(work), hWindow] {
        work();
        PostMessageW(hWindow, WM_DXS_TASK_DONE, 0, 0);
    });
    SetTimer(window, TASK_STATUS_TIMER, 100, nullptr); // smooth gauge
    Layout();                                          // make room for the gauge
    UpdateTaskStatus();
    Debug::Log(Utf8(pTask->sLabel) + " started in the background.");
    return true;
}

void Editor::FinishBackgroundTask() {
    if (!pTask) return;
    if (pTask->worker.joinable()) pTask->worker.join();
    KillTimer(window, TASK_STATUS_TIMER);
    if (pTask->onDone) pTask->onDone(); // may read pTask (e.g. bCanceled)
    if (pTask && pTask->hJob) CloseHandle(pTask->hJob);
    pTask.reset();
    Layout(); // hide the gauge
}

void Editor::CancelBackgroundTask(bool bWaitForWorker) {
    if (!pTask) return;
    if (pTask->hJob) {
        pTask->bCanceled = true;
        TerminateJobObject(pTask->hJob, 1);
    }
    if (!bWaitForWorker) {
        SetStatus(pTask->hJob ? L"Canceling the build..." : L"An export cannot be canceled; it finishes shortly.");
        return;
    }
    // Shutting down: wait for the worker, drop the completion callback.
    if (pTask->worker.joinable()) pTask->worker.join();
    if (pTask->hJob) CloseHandle(pTask->hJob);
    pTask.reset();
}

void Editor::UpdateTaskStatus() {
    if (!pTask) return;
    if (pTask->progress) pTask->fProgress = pTask->progress();
    const auto nSeconds = (GetTickCount64() - pTask->nStartTick) / 1000;
    std::wstring text = pTask->sLabel + L"...  ";
    if (pTask->fProgress >= 0.0f) text += std::to_wstring(static_cast<int>(pTask->fProgress * 100.0f)) + L"%   ";
    text += std::to_wstring(nSeconds) + L" s" + (pTask->hJob ? L"   (Build > Cancel build to stop)" : L"");
    if (Text(status) != text) SetStatus(text.c_str());
    InvalidateRect(progressGauge, nullptr, FALSE);
}

// Status-bar gauge: filled share + "42%", or a sliding bar when unknown.
void Editor::DrawProgressGauge(const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    const HDC hDc = item.hDC;
    FillRect(hDc, &rect, theme::Brush(theme::COLOR_TOOLBAR));
    RECT track = rect;
    theme::RoundFill(hDc, track, 3, theme::COLOR_INPUT, theme::COLOR_INPUT_BORDER);
    InflateRect(&track, -2, -2);
    const float fProgress = pTask ? pTask->fProgress : 0.0f;
    const int nWidth = static_cast<int>(track.right - track.left);
    RECT fill = track;
    if (fProgress >= 0.0f) {
        fill.right = track.left + static_cast<int>(static_cast<float>(nWidth) * std::clamp(fProgress, 0.0f, 1.0f));
    } else {
        // Unknown length (export): a 30% block moving back and forth.
        const int nBlock = std::max(8, nWidth * 3 / 10);
        const double dPhase = static_cast<double>(GetTickCount64() % 1600) / 1600.0;
        const double dPosition = dPhase < 0.5 ? dPhase * 2.0 : 2.0 - dPhase * 2.0;
        fill.left = track.left + static_cast<int>(dPosition * (nWidth - nBlock));
        fill.right = fill.left + nBlock;
    }
    if (fill.right > fill.left) theme::RoundFill(hDc, fill, 2, theme::COLOR_FOCUS, theme::COLOR_FOCUS);
    if (fProgress >= 0.0f) {
        const std::wstring sPercent = std::to_wstring(static_cast<int>(fProgress * 100.0f)) + L"%";
        theme::DrawLabel(hDc, sPercent, rect, theme::COLOR_TEXT_BRIGHT, theme::GetFonts().hSmall ? theme::GetFonts().hSmall : theme::GetFonts().hRegular,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

namespace {
// Reads a log that the build is still writing (shared access).
std::vector<std::string> ReadSharedLines(const std::filesystem::path& log) {
    std::vector<std::string> vLines;
    HANDLE hFile = CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return vLines;
    std::string sText;
    char aBuffer[16384];
    DWORD dwRead = 0;
    while (ReadFile(hFile, aBuffer, sizeof(aBuffer), &dwRead, nullptr) && dwRead > 0 && sText.size() < 8u * 1024u * 1024u)
        sText.append(aBuffer, dwRead);
    CloseHandle(hFile);
    size_t nStart = 0;
    while (nStart <= sText.size()) {
        const size_t nEnd = sText.find('\n', nStart);
        std::string line = sText.substr(nStart, nEnd == std::string::npos ? std::string::npos : nEnd - nStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vLines.push_back(std::move(line));
        if (nEnd == std::string::npos) break;
        nStart = nEnd + 1;
    }
    return vLines;
}
} // namespace

int CountBuildLogTasks(const std::filesystem::path& log) {
    int nTasks = 0;
    bool bSucceeded = false;
    for (const auto& line : ReadSharedLines(log)) {
        if (line.rfind("Offline build failed", 0) == 0) nTasks = 0; // the online retry starts over
        if (line.rfind("> Task ", 0) == 0) ++nTasks;
        if (line.find("BUILD SUCCESSFUL") != std::string::npos) bSucceeded = true;
    }
    return bSucceeded ? nTasks : 0;
}

float BuildLogProgress(const std::filesystem::path& log, int nExpectedTasks) {
    // Script checks (SDK, NDK, Java, Gradle) ~5%, Gradle tasks up to 98%;
    // 100% only when the task has finished.
    int nTasks = 0;
    bool bGradleStarted = false;
    for (const auto& line : ReadSharedLines(log)) {
        if (line.rfind("Gradle:", 0) == 0) bGradleStarted = true;
        if (line.rfind("Offline build failed", 0) == 0) nTasks = 0;
        if (line.rfind("> Task ", 0) == 0) ++nTasks;
    }
    if (!bGradleStarted) return 0.02f;
    const float fTasks = static_cast<float>(nTasks) / static_cast<float>(std::max(nExpectedTasks, nTasks + 1));
    return std::min(0.05f + 0.93f * fTasks, 0.98f);
}

bool Editor::InitRenderer() {
    renderer.Shutdown();
    bRendererReady = false;
    RECT r{};
    GetClientRect(viewport, &r);
    std::string error, report;
    if (!renderer.InitializeWithFallback(viewport, std::max<LONG>(1, r.right), std::max<LONG>(1, r.bottom),
                                         (projectRoot / L"Default.hlsl").wstring(), projectSettings.graphicsApi, error,
                                         &report)) {
        UpdateGraphicsBadge();
        Error(window, "Renderer initialization failed: " + error);
        SetStatus(L"Renderer unavailable. Fix the shader and select Reload Shader.");
        return false;
    }
    if (renderer.Api() != projectSettings.graphicsApi)
        Debug::LogWarning(std::string(GraphicsApiDisplayName(projectSettings.graphicsApi)) + " is unavailable; using " +
                          GraphicsApiDisplayName(renderer.Api()) + ".\n" + report);
    renderer.SetCamera(camera);
    bRendererReady = true;
    SetStatus(L"RMB + WASD/QE: fly | Alt + RMB: orbit | MMB: pan | Shift + gizmo: snap");
    UpdateGraphicsBadge();
    EngineEvents::Get().onGraphicsApiChanged.Broadcast(renderer.Api());
    return true;
}

void Editor::Draw() {
    TickCamera();
    nLastRenderTick = GetTickCount64();
    if (!bRendererReady || IsIconic(window) || !IsWindowVisible(viewport) || IsIconic(panels[1].window)) return;
    if (bPlaying) {
        const ULONGLONG now = GetTickCount64();
        float dt = std::clamp(static_cast<float>(now - nPlayTick) / 1000.0f, 0.0f, 0.1f);
        nPlayTick = now;
        // Paused: the scene is frozen; Step advances exactly one 1/60 s frame.
        if (!bPaused || bStepRequested) {
            if (bStepRequested) dt = 1.0f / 60.0f;
            bStepRequested = false;
            UpdateUserComponents(scene, dt);
            RefreshComponentFields();
        }
    }
    renderer.SetCamera(bPlaying || bGameView ? ActiveGameCamera(scene) : camera);
    renderer.SetDrawMode(drawMode);
    auto items = ResolveWorldDraws(scene);
    std::string error;
    renderer.SetSelection(bGameView ? std::vector<int>{} : vSelectedIndices);
    renderer.SetDebugLines(bGameView ? std::vector<DirectX::XMFLOAT3>{} : ColliderOutlineSegments(scene), {0.55f, 0.95f, 0.45f, 1.0f});
    const bool rendered = bGameView ? renderer.Render(items, error) : renderer.Render(items, error, nSelected, true, gizmoMode, true);
    if (!rendered) {
        bRendererReady = false;
        Error(window, "Rendering failed: " + error);
        SetStatus(L"Rendering stopped. Select Reload Shader to retry.");
    }
    UpdateFrameStats();
}

void Editor::UpdateFrameStats() {
    ++nFpsFrames;
    const ULONGLONG now = GetTickCount64();
    if (!nFpsTick) nFpsTick = now;
    if (now - nFpsTick < 500) return;
    const double fps = nFpsFrames * 1000.0 / static_cast<double>(now - nFpsTick);
    nFpsFrames = 0;
    nFpsTick = now;
    wchar_t text[128];
    swprintf_s(text, L"%hs   |   %.0f FPS   |   %ls", GraphicsApiDisplayName(renderer.Api()), fps,
               bPlaying ? (bPaused ? L"Paused" : L"Playing") : L"Edit Mode");
    SetWindowTextW(statusRight, text);
}

void Editor::Command(int id, int notification) {
    if (id == ID_BUILD_DEBUG) {
        TogglePlay();
        return;
    }
    if (bPlaying && (id == ID_SAVE || id == ID_LOAD || id == ID_OPEN_PROJECT || id == ID_SAVE_PROJECT_AS || id == ID_EXPORT ||
                    id == ID_BUILD_ANDROID || id == ID_RELOAD_SHADER)) {
        Error(window, "Stop Play Mode before saving, loading, switching, exporting, building, or reloading the shader.");
        return;
    }
    if (id == ID_PERSPECTIVE || id == ID_ORTHOGRAPHIC || id == ID_FRONT_VIEW) {
        camera.bOrthographic = id != ID_PERSPECTIVE;
        if (id == ID_FRONT_VIEW) {
            camera.fYaw = 0;
            camera.fPitch = 0;
        }
        renderer.SetCamera(camera);
        InvalidateRect(window, nullptr, TRUE);
        for (auto button : projectionButtons)
            InvalidateRect(button, nullptr, TRUE);
        SetStatus(camera.bOrthographic ? L"Orthographic camera. Save or export to keep this camera view."
                                      : L"Perspective camera. Quads and cubes share the same 3D world.");
        return;
    }
    if (id == ID_FRAME_SELECTION) {
        FrameSelection();
        return;
    }
    if (id == ID_NEW_PROJECT) {
        NewProject();
        return;
    }
    if (id == ID_CHOOSE_SCRIPT_EDITOR) {
        ChooseProjectScriptEditor(projectPanel);
        return;
    }
    if (id == ID_AUTOMATIC_SCRIPT_EDITOR) {
        UseAutomaticProjectScriptEditor(projectPanel);
        return;
    }
    if (id == ID_OPEN_PROJECT) {
        std::filesystem::path descriptor;
        std::string error;
        if (ChooseProjectDescriptor(window, false, projectFile.empty() ? projectRoot : projectFile, descriptor, error))
            SwitchProject(descriptor);
        else if (!error.empty())
            Error(window, error);
        return;
    }
    if (id == ID_SAVE_PROJECT_AS) {
        std::filesystem::path descriptor;
        std::string error;
        if (ChooseProjectDescriptor(window, true, projectFile.empty() ? projectRoot : projectFile, descriptor, error)) {
            if (!SaveProjectDescriptor(descriptor, projectRoot, error))
                Error(window, error);
            else {
                projectFile = descriptor;
                SetStatus(L"Project file saved.");
            }
        } else if (!error.empty())
            Error(window, error);
        return;
    }
    if (id == ID_CANCEL_BUILD) {
        if (IsTaskRunning()) CancelBackgroundTask(false);
        else SetStatus(L"No build is running.");
        return;
    }
    if (id == ID_BUILD_ANDROID) {
        RunBuildScript(L"build-android.cmd", L"Android Debug APK");
        return;
    }
    if (id == ID_PLATFORM_INFO) {
        MessageBoxW(window,
                    L"Windows x64: DirectX 12 export and in-editor Play Mode are available.\n\n"
                    L"Android: OpenGL ES Debug APK build is available when Android SDK/NDK are installed.\n\n"
                    L"Use the Build menu or the toolbar buttons.",
                    L"Build platforms", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == ID_HIERARCHY && notification == LBN_SELCHANGE) {
        const int count = static_cast<int>(SendMessageW(hierarchy, LB_GETSELCOUNT, 0, 0));
        std::vector<int> rows(count > 0 ? count : 0), next;
        if (count > 0) SendMessageW(hierarchy, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(rows.data()));
        for (const int row : rows)
            if (row >= 0 && static_cast<size_t>(row) < vHierarchyRows.size()) next.push_back(vHierarchyRows[row]);
        const int caretRow = static_cast<int>(SendMessageW(hierarchy, LB_GETCARETINDEX, 0, 0));
        const int caret =
            caretRow >= 0 && static_cast<size_t>(caretRow) < vHierarchyRows.size() ? vHierarchyRows[caretRow] : -1;
        // Apply only typed-but-unapplied values: Apply() rebuilds the list,
        // which would break a drag that starts with this click.
        if (InspectorHasPendingEdits() && !Apply()) {
            SyncHierarchy();
            return;
        }
        SetSelection(std::move(next), caret);
        return;
    }
    if (id == ID_HIERARCHY_SEARCH && notification == EN_CHANGE) {
        Populate();
        return;
    }
    if (id == ID_INSPECTOR_ENABLED && notification == BN_CLICKED) {
        if (nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size() && vSelectedIndices.size() == 1) {
            bInspectorEnabledChecked = !bInspectorEnabledChecked;
            InvalidateRect(inspectorEnabled, nullptr, FALSE);
            scene.vEntities[nSelected].draw.bVisible = bInspectorEnabledChecked;
            SetStatus(scene.vEntities[nSelected].draw.bVisible ? L"GameObject enabled."
                                                            : L"GameObject disabled. It remains in the hierarchy.");
            InvalidateRect(viewport, nullptr, FALSE);
        }
        return;
    }
    if (id == ID_COMPONENT_LIST && notification == LBN_SELCHANGE) {
        RefreshComponentFields();
        return;
    }
    if (id == ID_COMPONENT_FIELDS && notification == LBN_SELCHANGE) {
        const int index = static_cast<int>(SendMessageW(componentFields, LB_GETCURSEL, 0, 0));
        if (index >= 0 && static_cast<size_t>(index) < vInspectedComponentFields.size())
            SetWindowTextW(componentValue, Wide(vInspectedComponentFields[index].second).c_str());
        return;
    }
    if (notification != BN_CLICKED) return;
    if (id == ID_CREATE_MENU) {
        RECT r{};
        GetWindowRect(controls[0], &r);
        ShowObjectMenu({r.left, r.bottom});
        return;
    }
    if (id == ID_APPLY_COMPONENT_VALUE) {
        if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
        auto& entity = scene.vEntities[nSelected];
        const int listIndex = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
        const int fieldIndex = static_cast<int>(SendMessageW(componentFields, LB_GETCURSEL, 0, 0));
        const int componentIndex = listIndex - (entity.renderer == Entity::RendererKind::None ? 0 : 1);
        if (componentIndex < 0 || fieldIndex < 0 || static_cast<size_t>(fieldIndex) >= vInspectedComponentFields.size()) {
            Error(window, "Select a component field first.");
            return;
        }
        std::string error;
        if (!SetComponentEditorValue(entity, static_cast<size_t>(componentIndex), vInspectedComponentFields[fieldIndex].first,
                                     Utf8(Text(componentValue)), error))
            Error(window, error);
        else {
            RefreshComponentFields();
            SetStatus(L"Component field applied. Values remain live during Play Mode.");
        }
        return;
    }
    if (id == ID_ADD_COMPONENT && nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size()) {
        if (!Apply()) return;
        int choice = static_cast<int>(SendMessageW(componentChoice, CB_GETCURSEL, 0, 0));
        if (choice == CB_ERR) {
            Error(window, "Choose a registered component first.");
            return;
        }
        int length = static_cast<int>(SendMessageW(componentChoice, CB_GETLBTEXTLEN, choice, 0));
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(componentChoice, CB_GETLBTEXT, choice, reinterpret_cast<LPARAM>(value.data()));
        value.resize(length);
        auto name = Utf8(value);
        if (name == "MeshRenderer" || name == "PolygonRenderer" || name == "ModelRenderer" || name == "Image") {
            auto& entity = scene.vEntities[nSelected];
            if (name == "MeshRenderer") {
                entity.renderer = Entity::RendererKind::Mesh;
                if (entity.sModelAsset.empty()) entity.sModelAsset = PrimitiveAssetPath(PrimitiveShape::CUBE);
                Populate();
                SetStatus(L"MeshRenderer attached. Pick a mesh with the Mesh field's button.");
                return;
            }
            entity.renderer = name == "PolygonRenderer" ? Entity::RendererKind::Polygon
                              : name == "ModelRenderer" ? Entity::RendererKind::Model
                                                        : Entity::RendererKind::Image;
            if (entity.renderer == Entity::RendererKind::Model && entity.sModelAsset.empty()) {
                Error(window, "Place an X/FBX model first, or assign a model asset through Place X / FBX model.");
                entity.renderer = Entity::RendererKind::None;
                return;
            }
            Inspect();
            SetStatus(L"IRender component attached at compile-time-known renderer type.");
            return;
        }
        auto& components = scene.vEntities[nSelected].vComponents;
        std::string attachError;
        if (!AttachComponent(scene.vEntities[nSelected], name, attachError)) {
            Error(window, attachError);
            return;
        }
        Inspect();
        SendMessageW(componentList, LB_SETCURSEL,
                     static_cast<WPARAM>(components.size() - 1 +
                                         (scene.vEntities[nSelected].renderer == Entity::RendererKind::None ? 0 : 1)),
                     0);
        RefreshComponentFields();
        SetStatus(L"Component attached. Save or export to keep it.");
    } else if (id == ID_REMOVE_COMPONENT && nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size()) {
        int index = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
        if (index == LB_ERR) {
            Error(window, "Select an attached component to remove.");
            return;
        }
        if (!Apply()) return;
        auto& components = scene.vEntities[nSelected].vComponents;
        auto& entity = scene.vEntities[nSelected];
        const int rendererOffset = entity.renderer == Entity::RendererKind::None ? 0 : 1;
        if (index == 0 && rendererOffset) {
            entity.renderer = Entity::RendererKind::None;
        } else {
            index -= rendererOffset;
            if (index >= 0 && static_cast<size_t>(index) < components.size()) {
                DetachComponent(entity, static_cast<size_t>(index));
            }
        }
        Inspect();
        SetStatus(L"Component detached. Its serialized Inspector values were removed.");
    } else if ((id == ID_MOVE_COMPONENT_UP || id == ID_MOVE_COMPONENT_DOWN) && nSelected >= 0 &&
               static_cast<size_t>(nSelected) < scene.vEntities.size()) {
        auto& entity = scene.vEntities[nSelected];
        const int offset = entity.renderer == Entity::RendererKind::None ? 0 : 1;
        const int row = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
        int index = row - offset;
        const int next = index + (id == ID_MOVE_COMPONENT_UP ? -1 : 1);
        if (index >= 0 && next >= 0 && static_cast<size_t>(index) < entity.vComponents.size() &&
            static_cast<size_t>(next) < entity.vComponents.size()) {
            std::swap(entity.vComponents[index], entity.vComponents[next]);
            entity.vRuntimeComponentNames.clear();
            entity.vRuntimeComponents.clear();
            entity.vRuntimeComponentStarted.clear();
            Inspect();
            SendMessageW(componentList, LB_SETCURSEL, next + offset, 0);
            RefreshComponentFields();
            SetStatus(L"Component order changed. Components run in Inspector order.");
        }
    } else if (id >= ID_MOVE_MODE && id <= ID_SCALE_MODE) {
        gizmoMode = id == ID_MOVE_MODE ? GizmoMode::Translate : id == ID_ROTATE_MODE ? GizmoMode::Rotate : GizmoMode::Scale;
        for (auto button : gizmoButtons)
            InvalidateRect(button, nullptr, TRUE);
        SetStatus(L"Drag a colored axis to transform. Esc cancels a drag.");
    } else if (id >= ID_ADD_PRIMITIVE_FIRST && id < ID_ADD_PRIMITIVE_FIRST + static_cast<int>(PRIMITIVES.size())) {
        AddPrimitive(PRIMITIVES[static_cast<size_t>(id - ID_ADD_PRIMITIVE_FIRST)].shape);
    } else if (id == ID_ADD_CUBE) {
        AddPrimitive(PrimitiveShape::CUBE);
    } else if (id == ID_MESH_PICK) {
        ShowMeshPicker();
    } else if (id == ID_ADD_SPRITE || id == ID_ADD_EMPTY) {
        if (!Apply()) return;
        Entity entity;
        entity.sName = id == ID_ADD_CUBE ? "Cube" : id == ID_ADD_SPRITE ? "Sprite" : "Empty";
        entity.draw.bIs2D = id == ID_ADD_SPRITE;
        entity.renderer = id == ID_ADD_EMPTY ? Entity::RendererKind::None : Entity::RendererKind::Polygon;
        entity.draw.color =
            id == ID_ADD_CUBE ? DirectX::XMFLOAT4{0.35f, 0.68f, 1.f, 1.f} : DirectX::XMFLOAT4{1.f, 0.62f, 0.28f, 1.f};
        scene.vEntities.push_back(entity);
        nSelected = static_cast<int>(scene.vEntities.size()) - 1;
        vSelectedIndices = {nSelected};
        Populate();
        SetStatus(L"Object added. Save the scene to keep it.");
    } else if (id == ID_ADD_MODEL) {
        std::filesystem::path source;
        if (!ChooseModelFile(window, source)) return;
        const auto folder = projectRoot / L"Assets" / L"Models";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        std::wstring importError;
        auto destination = folder / source.filename();
        if (std::filesystem::weakly_canonical(source, ec) != std::filesystem::weakly_canonical(destination, ec) &&
            !ImportProjectFile(projectRoot, folder, source, importError)) {
            Error(window, Utf8(importError));
            return;
        }
        Entity entity;
        entity.sName = Utf8(source.stem().wstring());
        entity.renderer = Entity::RendererKind::Mesh;
        const auto relative = std::filesystem::relative(destination, projectRoot, ec).generic_u8string();
        entity.sModelAsset.assign(reinterpret_cast<const char*>(relative.data()), relative.size());
        scene.vEntities.push_back(std::move(entity));
        nSelected = static_cast<int>(scene.vEntities.size()) - 1;
        vSelectedIndices = {nSelected};
        Populate();
        SetStatus(L"Model placed from project/Assets/Models. ASCII FBX and text X are supported.");
    } else if (id == ID_DUPLICATE && !vSelectedIndices.empty()) {
        if (!Apply()) return;
        std::vector<int> duplicates;
        duplicates.reserve(vSelectedIndices.size());
        for (int source : vSelectedIndices)
            if (source >= 0 && static_cast<size_t>(source) < scene.vEntities.size()) {
                Entity copy = scene.vEntities[static_cast<size_t>(source)];
                copy.sName += " (Clone)";
                scene.vEntities.push_back(std::move(copy));
                duplicates.push_back(static_cast<int>(scene.vEntities.size()) - 1);
            }
        if (!duplicates.empty()) {
            vSelectedIndices = duplicates;
            nSelected = duplicates.back();
            Populate();
            SetStatus(L"Selection duplicated. Ctrl+D repeats this action.");
        }
    } else if (id == ID_DELETE && nSelected >= 0 && nSelected < static_cast<int>(scene.vEntities.size())) {
        if (vSelectedIndices.size() > 1 && MessageBoxW(window, L"Remove all selected objects from the scene?",
                                                      L"Delete objects", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
        for (auto it = vSelectedIndices.rbegin(); it != vSelectedIndices.rend(); ++it) {
            const int removed = *it;
            scene.vEntities.erase(scene.vEntities.begin() + removed);
            for (auto& entity : scene.vEntities) {
                if (entity.nParent == removed)
                    entity.nParent = -1;
                else if (entity.nParent > removed)
                    --entity.nParent;
            }
        }
        vSelectedIndices.clear();
        nSelected = -1;
        Populate();
        SetStatus(L"Selected objects removed. Save to keep changes.");
    } else if (id == ID_APPLY) {
        Apply();
    } else if (id == ID_SAVE) {
        if (bSceneProtected) {
            Error(window, "The existing scene could not be loaded. Fix it and use Load before saving.");
            return;
        }
        if (!Apply()) return;
        std::string error;
        if (!SaveScene(projectRoot / L"scene.dxs", scene, error))
            Error(window, error);
        else
            SetStatus(L"Scene saved to the active project.");
    } else if (id == ID_LOAD) {
        Scene loaded;
        std::string error;
        if (!LoadScene(projectRoot / L"scene.dxs", loaded, error))
            Error(window, error);
        else {
            scene = std::move(loaded);
            renderer.SetCamera(camera);
            for (auto button : projectionButtons)
                InvalidateRect(button, nullptr, TRUE);
            bSceneProtected = false;
            nSelected = scene.vEntities.empty() ? -1 : 0;
            vSelectedIndices = nSelected < 0 ? std::vector<int>{} : std::vector<int>{nSelected};
            Populate();
            SetStatus(L"Scene loaded.");
        }
    } else if (id == ID_EXPORT) {
        if (bSceneProtected) {
            Error(window, "The existing scene could not be loaded. Fix it and use Load before exporting.");
            return;
        }
        if (!Apply()) return;
        std::string error;
        std::filesystem::path destination;
        if (!ChooseExportFolder(window, base, destination, error)) {
            if (!error.empty()) Error(window, error);
            return;
        }
        std::error_code ec;
        bool existing = false;
        for (const auto& file : {L"Game.exe", L"scene.dxs", L"Default.vs.cso", L"Default.ps.cso"}) {
            if (std::filesystem::exists(destination / file, ec)) existing = true;
            if (ec) {
                Error(window, "Could not check the export folder: " + ec.message());
                return;
            }
        }
        if (existing &&
            MessageBoxW(window, L"The selected folder already contains one or more exported game files. Replace them?",
                        L"Export", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
        if (IsTaskRunning()) {
            Error(window, "Wait for the running build to finish (or use Build > Cancel build).");
            return;
        }
        // Export on a worker thread from a copy of the scene, so editing and
        // Play stay responsive while shaders compile and files are copied.
        auto pError = std::make_shared<std::string>();
        auto pOk = std::make_shared<bool>(false);
        Scene snapshot = scene;
        const auto baseDir = base, projectDir = projectRoot;
        StartBackgroundTask(
            L"Exporting game",
            [=]() mutable { *pOk = ExportGame(baseDir, projectDir, destination, snapshot, *pError); },
            [this, pError, pOk, destination] {
                if (!*pOk) {
                    SetStatus(L"Export failed.");
                    Error(window, *pError);
                    return;
                }
                SetStatus(L"Export complete. Run Game.exe in the selected folder.");
                MessageBoxW(window, (L"Exported to:\n" + destination.wstring()).c_str(), L"Export complete", MB_OK);
            });
    } else if (id == ID_RELOAD_SHADER) {
        InitRenderer();
    }
}

} // namespace dxs::editor
