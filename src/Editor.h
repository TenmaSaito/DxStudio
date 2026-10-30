#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>

namespace dxs {
int RunEditor(HINSTANCE instance, const std::filesystem::path& baseDir,
              const std::filesystem::path& initialProjectFile = {});
}
