#pragma once

#include <windows.h>

namespace ziliu::tsf {

void AddModuleReference() noexcept;
void ReleaseModuleReference() noexcept;
[[nodiscard]] long ModuleReferenceCount() noexcept;
[[nodiscard]] HINSTANCE ModuleInstance() noexcept;

}  // namespace ziliu::tsf

