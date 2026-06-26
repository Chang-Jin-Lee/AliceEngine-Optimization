#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"

#include "ThirdParty/json/json.hpp"

#include "imgui.h"

#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace Alice
{
	namespace
	{
		std::string ToLowerAscii(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		std::string TrimAscii(const std::string& s)
		{
			size_t start = 0;
			while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
				++start;
			size_t end = s.size();
			while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
				--end;
			return s.substr(start, end - start);
		}

		std::string NormalizeLogicalPath(const std::string& s)
		{
			std::string temp = s;
			std::replace(temp.begin(), temp.end(), '\\', '/');
			if (temp.rfind("./", 0) == 0)
				temp = temp.substr(2);
			std::filesystem::path p(temp);
			std::string normalized = p.lexically_normal().generic_string();
			if (normalized.rfind("./", 0) == 0)
				normalized = normalized.substr(2);
			return normalized;
		}

		bool HasParentTraversal(const std::string& s)
		{
			std::filesystem::path p(s);
			for (const auto& part : p)
			{
				if (part == "..")
					return true;
			}
			return false;
		}

		bool IsAllowedLogicalPath(const std::string& s)
		{
			const std::string lower = ToLowerAscii(s);
			return lower.rfind("assets/", 0) == 0 ||
				lower.rfind("resource/", 0) == 0 ||
				lower.rfind("cooked/", 0) == 0;
		}

		std::filesystem::path GetProjectRoot()
		{
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			std::filesystem::path exeDir = std::filesystem::path(exePathW).parent_path();
			return exeDir.parent_path().parent_path().parent_path();
		}

		bool TryMakeLogicalPath(const std::filesystem::path& input, std::string& outLogical)
		{
			outLogical.clear();
			if (input.empty())
				return false;

			if (input.is_absolute())
			{
				// Resource/... 우선 변환
				std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(input);
				if (!logical.empty() && !logical.is_absolute())
				{
					std::string normalized = NormalizeLogicalPath(logical.generic_string());
					if (!HasParentTraversal(normalized) && IsAllowedLogicalPath(normalized))
					{
						outLogical = normalized;
						return true;
					}
				}

				// 프로젝트 루트 기준 상대 경로
				std::error_code ec;
				const std::filesystem::path projectRoot = GetProjectRoot();
				const std::filesystem::path rel = std::filesystem::relative(input, projectRoot, ec);
				if (!ec && !rel.empty())
				{
					std::string normalized = NormalizeLogicalPath(rel.generic_string());
					if (!HasParentTraversal(normalized) && IsAllowedLogicalPath(normalized))
					{
						outLogical = normalized;
						return true;
					}
				}

				return false;
			}

			{
				std::string normalized = NormalizeLogicalPath(input.generic_string());
				if (!HasParentTraversal(normalized) && IsAllowedLogicalPath(normalized))
				{
					outLogical = normalized;
					return true;
				}
			}
			return false;
		}

		bool LoadPreloadJsonFile(const std::filesystem::path& path,
			std::vector<std::string>& outItems,
			std::string& outError)
		{
			outItems.clear();
			outError.clear();

			std::error_code ec;
			if (!std::filesystem::exists(path, ec))
				return true;

			std::ifstream ifs(path);
			if (!ifs.is_open())
			{
				outError = "Failed to open Preload.json.";
				return false;
			}

			nlohmann::json j;
			try
			{
				ifs >> j;
			}
			catch (...)
			{
				outError = "Preload.json parse failed.";
				return false;
			}

			auto AppendFromArray = [&](const nlohmann::json& arr)
			{
				for (const auto& v : arr)
				{
					if (!v.is_string())
						continue;
					std::string s = TrimAscii(v.get<std::string>());
					if (s.empty())
						continue;
					outItems.push_back(NormalizeLogicalPath(s));
				}
			};

			if (j.is_array())
			{
				AppendFromArray(j);
				return true;
			}

			if (j.contains("preload") && j["preload"].is_array())
			{
				AppendFromArray(j["preload"]);
				return true;
			}

			if (j.contains("startup") && j["startup"].is_array())
			{
				AppendFromArray(j["startup"]);
				return true;
			}

			// 인식 가능한 구조가 아니면 빈 목록으로 처리
			return true;
		}

		bool SavePreloadJsonFile(const std::filesystem::path& path,
			const std::vector<std::string>& items,
			std::string& outError)
		{
			outError.clear();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);

			nlohmann::json j;
			if (std::filesystem::exists(path, ec))
			{
				std::ifstream ifs(path);
				if (ifs.is_open())
				{
					try { ifs >> j; }
					catch (...) {}
				}
			}
			if (!j.is_object())
				j = nlohmann::json::object();
			j["preload"] = items;

			std::filesystem::path tmp = path;
			tmp += ".tmp";
			{
				std::ofstream ofs(tmp);
				if (!ofs.is_open())
				{
					outError = "Failed to write Preload.json (open failed).";
					return false;
				}
				ofs << j.dump(4);
			}

			ec.clear();
			std::filesystem::rename(tmp, path, ec);
			if (ec)
			{
				std::filesystem::remove(path, ec);
				ec.clear();
				std::filesystem::rename(tmp, path, ec);
			}
			if (ec)
			{
				outError = "Failed to save Preload.json.";
				return false;
			}

			return true;
		}

		bool ResolveLogicalExists(const std::string& logicalPath)
		{
			if (logicalPath.empty())
				return false;
			const std::filesystem::path resolved = ResourceManager::Get().Resolve(logicalPath);
			std::error_code ec;
			return std::filesystem::exists(resolved, ec);
		}

		bool AddPreloadItem(std::vector<std::string>& items,
			const std::string& logicalPath,
			std::string& outError)
		{
			outError.clear();
			std::string normalized = NormalizeLogicalPath(TrimAscii(logicalPath));
			if (normalized.empty())
			{
				outError = "Empty path.";
				return false;
			}
			if (HasParentTraversal(normalized))
			{
				outError = "Invalid path (.. not allowed).";
				return false;
			}
			if (!IsAllowedLogicalPath(normalized))
			{
				outError = "Only Assets/Resource/Cooked paths are allowed.";
				return false;
			}

			const std::string key = ToLowerAscii(normalized);
			for (const auto& item : items)
			{
				if (ToLowerAscii(item) == key)
				{
					outError = "Already exists in list.";
					return false;
				}
			}

			items.push_back(normalized);
			return true;
		}

		std::vector<std::filesystem::path> ParseOpenFileBuffer(const wchar_t* buffer)
		{
			std::vector<std::filesystem::path> out;
			if (!buffer || !buffer[0])
				return out;

			std::wstring first = buffer;
			const wchar_t* p = buffer + first.size() + 1;
			if (*p == L'\0')
			{
				out.emplace_back(first);
				return out;
			}

			std::filesystem::path dir(first);
			while (*p)
			{
				std::wstring name = p;
				out.emplace_back(dir / name);
				p += name.size() + 1;
			}
			return out;
		}
	}

	void EditorCore::DrawPreloadAssetEditorWindow()
	{
		if (!g_PreloadEditorOpen)
			return;

		static std::filesystem::path s_loadedPath;
		static std::filesystem::file_time_type s_loadedTime{};
		static std::string s_statusMsg;
		static bool s_statusError = false;

		if (g_PreloadEditorPath != s_loadedPath)
		{
			std::string err;
			if (!LoadPreloadJsonFile(g_PreloadEditorPath, g_PreloadEditorItems, err))
			{
				s_statusMsg = err;
				s_statusError = true;
			}
			else
			{
				s_statusMsg.clear();
				s_statusError = false;
			}

			g_PreloadEditorSelected = -1;
			s_loadedPath = g_PreloadEditorPath;

			std::error_code ec;
			if (std::filesystem::exists(g_PreloadEditorPath, ec))
				s_loadedTime = std::filesystem::last_write_time(g_PreloadEditorPath, ec);
			else
				s_loadedTime = {};
		}

		if (ImGui::Begin("Preload Asset Editor", &g_PreloadEditorOpen))
		{
			if (g_PreloadEditorPath.empty())
			{
				ImGui::TextDisabled("No Preload.json selected.");
				ImGui::End();
				return;
			}

			ImGui::Text("Asset: %s", g_PreloadEditorPath.string().c_str());

			// 외부 변경 감지
			{
				std::error_code ec;
				if (std::filesystem::exists(g_PreloadEditorPath, ec))
				{
					const auto currentTime = std::filesystem::last_write_time(g_PreloadEditorPath, ec);
					if (!ec && currentTime != s_loadedTime)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "File changed on disk.");
						ImGui::SameLine();
						if (ImGui::Button("Reload"))
						{
							std::string err;
							if (!LoadPreloadJsonFile(g_PreloadEditorPath, g_PreloadEditorItems, err))
							{
								s_statusMsg = err;
								s_statusError = true;
							}
							else
							{
								s_statusMsg.clear();
								s_statusError = false;
								s_loadedTime = currentTime;
								g_PreloadEditorSelected = -1;
							}
						}
					}
				}
			}

			if (!s_statusMsg.empty())
			{
				const ImVec4 col = s_statusError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
				ImGui::TextColored(col, "%s", s_statusMsg.c_str());
			}

			ImGui::Separator();

			bool changed = false;

			if (ImGui::Button("Add..."))
			{
				wchar_t fileBuffer[65536] = {};
				OPENFILENAMEW ofn{};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = m_hwnd;
				ofn.lpstrFilter = L"All Files\0*.*\0";
				ofn.lpstrFile = fileBuffer;
				ofn.nMaxFile = static_cast<DWORD>(sizeof(fileBuffer) / sizeof(fileBuffer[0]));
				ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
					OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;

				const std::filesystem::path assetsDir = GetProjectRoot() / "Assets";
				const std::wstring initDir = assetsDir.wstring();
				ofn.lpstrInitialDir = initDir.c_str();

				if (GetOpenFileNameW(&ofn))
				{
					const auto paths = ParseOpenFileBuffer(fileBuffer);
					for (const auto& p : paths)
					{
						std::string logical;
						std::string err;
						if (!TryMakeLogicalPath(p, logical))
						{
							s_statusMsg = "Selected file is outside Assets/Resource/Cooked.";
							s_statusError = true;
							continue;
						}
						if (AddPreloadItem(g_PreloadEditorItems, logical, err))
						{
							changed = true;
						}
						else if (!err.empty())
						{
							s_statusMsg = err;
							s_statusError = true;
						}
					}
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Remove Selected") &&
				g_PreloadEditorSelected >= 0 &&
				g_PreloadEditorSelected < static_cast<int>(g_PreloadEditorItems.size()))
			{
				g_PreloadEditorItems.erase(g_PreloadEditorItems.begin() + g_PreloadEditorSelected);
				g_PreloadEditorSelected = -1;
				changed = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear All"))
			{
				g_PreloadEditorItems.clear();
				g_PreloadEditorSelected = -1;
				changed = true;
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Drag assets here to add (Assets/Resource/Cooked only).");
			ImGui::BeginChild("PreloadDropArea", ImVec2(0, 60.0f), true);
			ImGui::TextUnformatted("Drop files from Project panel.");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);

					std::string logical;
					std::string err;
					if (TryMakeLogicalPath(droppedPath, logical))
					{
						if (AddPreloadItem(g_PreloadEditorItems, logical, err))
							changed = true;
						else if (!err.empty())
						{
							s_statusMsg = err;
							s_statusError = true;
						}
					}
					else
					{
						s_statusMsg = "Dropped file is outside Assets/Resource/Cooked.";
						s_statusError = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::EndChild();

			ImGui::Separator();

			if (g_PreloadEditorItems.empty())
			{
				ImGui::TextDisabled("No preload entries.");
			}
			else if (ImGui::BeginTable("PreloadList", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Path");
				ImGui::TableSetupColumn("Up", ImGuiTableColumnFlags_WidthFixed, 40.0f);
				ImGui::TableSetupColumn("Down", ImGuiTableColumnFlags_WidthFixed, 50.0f);
				ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 40.0f);
				ImGui::TableHeadersRow();

				for (int i = 0; i < static_cast<int>(g_PreloadEditorItems.size()); ++i)
				{
					const std::string& path = g_PreloadEditorItems[i];
					const std::string normalized = NormalizeLogicalPath(path);
					const bool allowed = IsAllowedLogicalPath(normalized) && !HasParentTraversal(normalized);
					const bool exists = allowed && ResolveLogicalExists(normalized);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					ImGui::PushID(i);
					if (!allowed)
						ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 90, 90, 255));
					else if (!exists)
						ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 90, 255));

					const std::string label = normalized + "##preload";
					if (ImGui::Selectable(label.c_str(), g_PreloadEditorSelected == i))
						g_PreloadEditorSelected = i;

					if (!allowed)
					{
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Invalid path (must be Assets/Resource/Cooked)");
					}
					else if (!exists)
					{
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Missing file on disk.");
					}

					ImGui::TableSetColumnIndex(1);
					const bool canUp = (i > 0);
					ImGui::BeginDisabled(!canUp);
					if (ImGui::ArrowButton("##Up", ImGuiDir_Up) && canUp)
					{
						std::swap(g_PreloadEditorItems[i], g_PreloadEditorItems[i - 1]);
						g_PreloadEditorSelected = i - 1;
						changed = true;
					}
					ImGui::EndDisabled();

					ImGui::TableSetColumnIndex(2);
					const bool canDown = (i + 1 < static_cast<int>(g_PreloadEditorItems.size()));
					ImGui::BeginDisabled(!canDown);
					if (ImGui::ArrowButton("##Down", ImGuiDir_Down) && canDown)
					{
						std::swap(g_PreloadEditorItems[i], g_PreloadEditorItems[i + 1]);
						g_PreloadEditorSelected = i + 1;
						changed = true;
					}
					ImGui::EndDisabled();

					ImGui::TableSetColumnIndex(3);
					if (ImGui::SmallButton("X"))
					{
						g_PreloadEditorItems.erase(g_PreloadEditorItems.begin() + i);
						if (g_PreloadEditorSelected == i)
							g_PreloadEditorSelected = -1;
						else if (g_PreloadEditorSelected > i)
							--g_PreloadEditorSelected;
						changed = true;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Tip: Put Preload.json under Assets/Startup for build-time chunking.");

			if (changed)
			{
				std::string err;
				if (!SavePreloadJsonFile(g_PreloadEditorPath, g_PreloadEditorItems, err))
				{
					s_statusMsg = err;
					s_statusError = true;
				}
				else
				{
					s_statusMsg = "Saved.";
					s_statusError = false;
					std::error_code ec;
					if (std::filesystem::exists(g_PreloadEditorPath, ec))
						s_loadedTime = std::filesystem::last_write_time(g_PreloadEditorPath, ec);
				}
			}
		}
		ImGui::End();
	}
}
