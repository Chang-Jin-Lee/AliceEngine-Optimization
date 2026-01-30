#pragma once

// ImGui용 간단 래퍼
// - 라벨을 wide literal(TCHAR / wchar_t*)로 받아서 UTF-8로 변환 후 ImGui에 넘깁니다.
// - 예)
//   Alice::ImGuiCheckbox(TEXT("Fill Light (보조광)"), &flag);
//   Alice::ImGuiSliderFloat(TEXT("Key Intensity (주광)"), &value, 0.0f, 3.0f);
//   Alice::ImGuiSliderFloat3(TEXT("Key Direction (주광)"), &vec.x, -1.0f, 1.0f);

#include "imgui.h"
#include "Core/StringUtils.h"

namespace Alice
{
    inline void ImGuiText(const wchar_t* text)
    {
        std::string utf8 = Utf8(text);
        ImGui::TextUnformatted(utf8.c_str());
    }

    inline void ImGuiText(const char* text)
    {
        ImGui::TextUnformatted(text);
    }

    inline bool ImGuiCheckbox(const wchar_t* label, bool* v)
    {
        std::string utf8 = Utf8(label);
        return ImGui::Checkbox(utf8.c_str(), v);
    }

    inline bool ImGuiSliderFloat(const wchar_t* label,
                                 float* v,
                                 float v_min,
                                 float v_max,
                                 const char* format = "%.3f",
                                 ImGuiSliderFlags flags = 0)
    {
        std::string utf8 = Utf8(label);
        return ImGui::SliderFloat(utf8.c_str(), v, v_min, v_max, format, flags);
    }

    inline bool ImGuiSliderFloat3(const wchar_t* label,
                                  float v[3],
                                  float v_min,
                                  float v_max,
                                  const char* format = "%.3f",
                                  ImGuiSliderFlags flags = 0)
    {
        std::string utf8 = Utf8(label);
        return ImGui::SliderFloat3(utf8.c_str(), v, v_min, v_max, format, flags);
    }
}


