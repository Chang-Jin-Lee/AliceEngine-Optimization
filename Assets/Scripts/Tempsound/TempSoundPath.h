#pragma once

#include <string>
#include <string_view>

namespace Alice
{
    namespace TempSound
    {
        inline constexpr char kPlayerFolder[] = "\xED\x94\x8C\xEB\xA0\x88\xEC\x9D\xB4\xEC\x96\xB4";
        inline constexpr char kBossFolder[] = "\xEB\xB3\xB4\xEC\x8A\xA4";
        inline constexpr char kBgmFolder[] = "BGM";
        inline constexpr std::string_view kPlayerFolderView = kPlayerFolder;
        inline constexpr std::string_view kBossFolderView = kBossFolder;
        inline constexpr std::string_view kBgmFolderView = kBgmFolder;
        inline constexpr std::string_view kSfxRoot = "Resource/Sound/SFX/";

        inline char ToLowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        inline bool EqualsNoCaseAscii(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;

            for (size_t i = 0; i < a.size(); ++i)
            {
                if (ToLowerAscii(a[i]) != ToLowerAscii(b[i]))
                    return false;
            }

            return true;
        }

        inline bool StartsWithNoCaseAscii(std::string_view s, std::string_view prefix)
        {
            if (s.size() < prefix.size())
                return false;

            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (ToLowerAscii(s[i]) != ToLowerAscii(prefix[i]))
                    return false;
            }

            return true;
        }

        inline bool IsLogicalRoot(std::string_view s)
        {
            return StartsWithNoCaseAscii(s, "Assets/") ||
                   StartsWithNoCaseAscii(s, "Resource/") ||
                   StartsWithNoCaseAscii(s, "Cooked/");
        }

        inline std::string NormalizeSlashes(std::string s)
        {
            for (char& c : s)
            {
                if (c == '\\')
                    c = '/';
            }
            return s;
        }

        inline std::string_view MapCategorySegment(std::string_view segment)
        {
            if (segment == kPlayerFolderView)
                return kPlayerFolderView;
            if (segment == kBossFolderView)
                return kBossFolderView;
            if (segment == kBgmFolderView)
                return kBgmFolderView;

            if (EqualsNoCaseAscii(segment, "Player"))
                return kPlayerFolderView;
            if (EqualsNoCaseAscii(segment, "Boss"))
                return kBossFolderView;
            if (EqualsNoCaseAscii(segment, "BGM"))
                return kBgmFolderView;

            return {};
        }

        inline std::string ResolveTempSoundPath(const std::string& raw, std::string_view defaultCategory)
        {
            std::string path = NormalizeSlashes(raw);
            if (path.empty())
                return path;

            std::string_view view(path);

            if (StartsWithNoCaseAscii(view, kSfxRoot))
            {
                std::string_view tail = view.substr(kSfxRoot.size());
                while (!tail.empty() && tail.front() == '/')
                    tail.remove_prefix(1);

                std::string_view segment;
                std::string_view rest;
                const size_t slash = tail.find('/');
                if (slash == std::string_view::npos)
                {
                    segment = tail;
                    rest = {};
                }
                else
                {
                    segment = tail.substr(0, slash);
                    rest = tail.substr(slash + 1);
                    while (!rest.empty() && rest.front() == '/')
                        rest.remove_prefix(1);
                }

                std::string_view mapped = MapCategorySegment(segment);
                if (mapped.empty())
                    return path;

                std::string out(kSfxRoot.data(), kSfxRoot.size());
                out.append(mapped.data(), mapped.size());
                if (!rest.empty())
                {
                    out.push_back('/');
                    out.append(rest.data(), rest.size());
                }
                return out;
            }

            if (IsLogicalRoot(view))
                return path;

            if (StartsWithNoCaseAscii(view, "SFX/"))
                view.remove_prefix(4);

            while (!view.empty() && view.front() == '/')
                view.remove_prefix(1);

            std::string_view segment;
            std::string_view rest;
            const size_t slash = view.find('/');
            if (slash == std::string_view::npos)
            {
                segment = view;
                rest = {};
            }
            else
            {
                segment = view.substr(0, slash);
                rest = view.substr(slash + 1);
                while (!rest.empty() && rest.front() == '/')
                    rest.remove_prefix(1);
            }

            const std::string_view explicitMapped = MapCategorySegment(segment);
            const bool hasExplicitCategory = !explicitMapped.empty();
            const std::string_view mapped = hasExplicitCategory ? explicitMapped : MapCategorySegment(defaultCategory);
            if (mapped.empty())
                return path;

            const std::string_view remainder = hasExplicitCategory ? rest : view;
            std::string out(kSfxRoot.data(), kSfxRoot.size());
            out.append(mapped.data(), mapped.size());
            if (!remainder.empty())
            {
                out.push_back('/');
                out.append(remainder.data(), remainder.size());
            }

            return out;
        }
    }
}
