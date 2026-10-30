cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(DxStudio LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "DxStudio requires Windows and the Windows SDK")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(DxStudioCore STATIC src/Renderer.cpp src/Engine.cpp third_party/ufbx/ufbx.c)
target_include_directories(DxStudioCore PUBLIC src)
target_compile_definitions(DxStudioCore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(DxStudioCore PUBLIC d3d12 dxgi d3dcompiler)

# Compile project scripts into both the editor's picker and the player.
file(GLOB PROJECT_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/project/Scripts/*.cpp")
add_executable(DxStudio WIN32 src/EditorMain.cpp src/Editor.cpp src/ProjectPanel.cpp ${PROJECT_SCRIPTS})
target_link_libraries(DxStudio PRIVATE DxStudioCore comctl32 comdlg32 ole32 shell32 uuid)

add_executable(Game WIN32 src/GameMain.cpp ${PROJECT_SCRIPTS})
target_link_libraries(Game PRIVATE DxStudioCore)

# Keep the editable project beside the executables.
foreach(target DxStudio Game)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
  endforeach()
endforeach()
