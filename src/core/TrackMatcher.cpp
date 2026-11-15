#include "core/TrackMatcher.h"

#include <cmath>
#include <functional>

namespace dss1
{

namespace
{
    /// Decodes one UTF-8 sequence at `i`, advancing it. Invalid bytes are returned as themselves.
    std::uint32_t nextCodePoint (const std::string& s, std::size_t& i)
    {
        const auto b0 = static_cast<unsigned char> (s[i]);
        const auto continuation = [&] (std::size_t k) {
            return i + k < s.size() && (static_cast<unsigned char> (s[i + k]) & 0xC0) == 0x80;
        };
        const auto bits = [&] (std::size_t k) { return std::uint32_t (static_cast<unsigned char> (s[i + k]) & 0x3F); };

        if (b0 < 0x80)
        {
            i += 1;
            return b0;
        }
        if ((b0 & 0xE0) == 0xC0 && continuation (1))
        {
            const auto cp = (std::uint32_t (b0 & 0x1F) << 6) | bits (1);
            i += 2;
            return cp;
        }
        if ((b0 & 0xF0) == 0xE0 && continuation (1) && continuation (2))
        {
            const auto cp = (std::uint32_t (b0 & 0x0F) << 12) | (bits (1) << 6) | bits (2);
            i += 3;
            return cp;
        }
        if ((b0 & 0xF8) == 0xF0 && continuation (1) && continuation (2) && continuation (3))
        {
            const auto cp = (std::uint32_t (b0 & 0x07) << 18) | (bits (1) << 12) | (bits (2) << 6) | bits (3);
            i += 4;
            return cp;
        }
        i += 1;
        return b0;
    }

    void appendUtf8 (std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char> (cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char> (0xC0 | (cp >> 6));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char> (0xE0 | (cp >> 12));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xF0 | (cp >> 18));
            out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
    }

    /// Base letters for accented Latin letters (Latin-1 Supplement and Latin Extended-A), lower case.
    /// nullptr = not a letter we fold.
    const char* foldLatin (std::uint32_t cp)
    {
        static const char* const latin1[64] = {
            "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // C0–CF
            "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss", // D0–DF
            "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0–EF
            "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y"   // F0–FF
        };
        if (cp >= 0xC0 && cp <= 0xFF)
            return latin1[cp - 0xC0];

        struct Range { std::uint32_t first, last; const char* base; };
        static const Range extendedA[] = {
            { 0x100, 0x105, "a" }, { 0x106, 0x10D, "c" }, { 0x10E, 0x111, "d" }, { 0x112, 0x11B, "e" },
            { 0x11C, 0x123, "g" }, { 0x124, 0x127, "h" }, { 0x128, 0x131, "i" }, { 0x132, 0x133, "ij" },
            { 0x134, 0x135, "j" }, { 0x136, 0x138, "k" }, { 0x139, 0x142, "l" }, { 0x143, 0x14B, "n" },
            { 0x14C, 0x151, "o" }, { 0x152, 0x153, "oe" }, { 0x154, 0x159, "r" }, { 0x15A, 0x161, "s" },
            { 0x162, 0x167, "t" }, { 0x168, 0x173, "u" }, { 0x174, 0x175, "w" }, { 0x176, 0x178, "y" },
            { 0x179, 0x17E, "z" }, { 0x17F, 0x17F, "s" }
        };
        for (const auto& r : extendedA)
            if (cp >= r.first && cp <= r.last)
                return r.base;
        return nullptr;
    }

    bool isCombiningMark (std::uint32_t cp)
    {
        return cp >= 0x300 && cp <= 0x36F;
    }

    std::string fileName (const std::string& key)
    {
        const auto slash = key.find_last_of ('/');
        return slash == std::string::npos ? key : key.substr (slash + 1);
    }

    std::string stem (const std::string& name)
    {
        const auto dot = name.find_last_of ('.');
        return (dot == std::string::npos || dot == 0) ? name : name.substr (0, dot);
    }

    bool lengthsAgree (const std::optional<double>& a, const std::optional<double>& b)
    {
        return ! a || ! b || std::abs (*a - *b) <= lengthToleranceSeconds;
    }
} // namespace

std::string looseKey (const std::string& utf8)
{
    std::string out;
    out.reserve (utf8.size());
    for (std::size_t i = 0; i < utf8.size();)
    {
        const auto cp = nextCodePoint (utf8, i);
        if (isCombiningMark (cp))
            continue;
        if (cp == '\\')
            out += '/';
        else if (cp >= 'A' && cp <= 'Z')
            out += static_cast<char> (cp - 'A' + 'a');
        else if (const auto* base = foldLatin (cp))
            out += base;
        else
            appendUtf8 (out, cp);
    }
    return out;
}

MatchResult matchTrack (const MatchQuery& query, const std::vector<MatchCandidate>& candidates)
{
    const auto queryKey = looseKey (query.path);
    const auto queryName = fileName (queryKey);
    const auto queryStem = stem (queryName);

    using Rule = std::function<bool (const MatchCandidate&)>;
    const std::pair<MatchKind, Rule> rules[] = {
        { MatchKind::exactPath,    [&] (const MatchCandidate& c) { return c.path == query.path; } },
        { MatchKind::samePath,     [&] (const MatchCandidate& c) { return looseKey (c.path) == queryKey; } },
        { MatchKind::sameFileName, [&] (const MatchCandidate& c) {
              return fileName (looseKey (c.path)) == queryName && lengthsAgree (c.lengthSeconds, query.lengthSeconds); } },
        { MatchKind::sameName,     [&] (const MatchCandidate& c) {
              return stem (fileName (looseKey (c.path))) == queryStem && lengthsAgree (c.lengthSeconds, query.lengthSeconds); } },
    };

    for (const auto& [kind, rule] : rules)
    {
        MatchResult result;
        for (const auto& c : candidates)
            if (rule (c))
                result.ids.push_back (c.id);

        if (result.ids.size() == 1)
        {
            result.kind = kind;
            return result;
        }
        if (result.ids.size() > 1)
        {
            result.kind = MatchKind::ambiguous;
            return result;
        }
    }
    return {};
}

} // namespace dss1
