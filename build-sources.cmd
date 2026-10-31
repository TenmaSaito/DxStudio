@echo off
rem Shared source and library lists for build-direct.cmd, build-debug.cmd and test.cmd.
set "DXS_ENGINE_SOURCES=src\Engine.cpp src\Primitives.cpp src\Core\Log.cpp src\Core\Coroutine.cpp src\Core\EngineCallbacks.cpp third_party\ufbx\ufbx.c"
set "DXS_RENDER_SOURCES=src\Renderer.cpp src\Graphics\ShaderLoader.cpp src\Graphics\DeviceD3D12.cpp src\Graphics\DeviceD3D11.cpp src\Graphics\DeviceD3D9.cpp"
set "DXS_EDITOR_SOURCES=src\EditorMain.cpp src\Editor.cpp src\EditorPanels.cpp src\EditorChrome.cpp src\EditorLayout.cpp src\EditorActions.cpp src\EditorInspector.cpp src\EditorTheme.cpp src\EditorUtility.cpp src\ConsolePanel.cpp src\ProjectPanel.cpp"
set "DXS_RENDER_LIBS=d3d12.lib d3d11.lib d3d9.lib dxgi.lib d3dcompiler.lib"
set "DXS_WINDOW_LIBS=user32.lib gdi32.lib shell32.lib"
set "DXS_EDITOR_LIBS=comctl32.lib comdlg32.lib ole32.lib uuid.lib dwmapi.lib uxtheme.lib"
set "DXS_CL_FLAGS=/nologo /std:c++20 /EHsc /MT /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING /Isrc"
