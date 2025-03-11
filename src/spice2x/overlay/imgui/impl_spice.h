#pragma once

#include <windows.h>
#include "external/imgui/imgui.h"

IMGUI_IMPL_API bool ImGui_ImplSpice_Init(HWND hWnd);
IMGUI_IMPL_API void ImGui_ImplSpice_Shutdown();
IMGUI_IMPL_API void ImGui_ImplSpice_UpdateDisplaySize();
IMGUI_IMPL_API bool ImGui_ImplSpice_UpdateMouseCursor();
IMGUI_IMPL_API void ImGui_ImplSpice_NewFrame();
