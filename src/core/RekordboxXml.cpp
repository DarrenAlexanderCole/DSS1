#include "core/RekordboxXml.h"

#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

#include <pugixml.hpp>

namespace dss1::rekordbox
{

namespace
{
    // Numbers are always written and read with '.' as the decimal point, whatever the user's locale.

    std::optional<double> toNumber (const char* text)
    {
        if (text == nullptr || *text == '\0')
            return std::nullopt;
        std::istringstream in (text);
        in.imbue (std::locale::classic());
        double value = 0;
        in >> value;
        if (in.fail())
            return std::nullopt;
        return value;
    }

    int toInt (const char* text, int fallback)
    {
        const auto n = toNumber (text);
        return n ? static_cast<int> (*n) : fallback;
    }

    std::string fixed (double value, int decimals)
    {
        std::ostringstream out;
        out.imbue (std::locale::classic());
        out << std::fixed << std::setprecision (decimals) << value;
        return out.str();
    }

    /// Whole numbers without a decimal point ("44100"), others with up to 3 decimals.
    std::string compact (double value)
    {
        if (value == static_cast<double> (static_cast<long long> (value)))
            return std::to_string (static_cast<long long> (value));
        auto text = fixed (value, 3);
        while (text.back() == '0')
            text.pop_back();
        if (text.back() == '.')
            text.pop_back();
        return text;
    }

    const char* const namedTrackAttributes[] = {
        "TrackID", "Name", "Artist", "Album", "Genre", "Kind", "TotalTime",
        "AverageBpm", "SampleRate", "Tonality", "Location"
    };

    bool isNamed (const char* attribute)
    {
        for (const auto* name : namedTrackAttributes)
            if (std::string_view (name) == attribute)
                return true;
        return false;
    }

    Track readTrack (const pugi::xml_node& node)
    {
        Track t;
        t.trackId    = node.attribute ("TrackID").as_string();
        t.name       = node.attribute ("Name").as_string();
        t.artist     = node.attribute ("Artist").as_string();
        t.album      = node.attribute ("Album").as_string();
        t.genre      = node.attribute ("Genre").as_string();
        t.kind       = node.attribute ("Kind").as_string();
        t.totalTime  = toNumber (node.attribute ("TotalTime").as_string());
        t.averageBpm = toNumber (node.attribute ("AverageBpm").as_string());
        t.sampleRate = toNumber (node.attribute ("SampleRate").as_string());
        t.tonality   = node.attribute ("Tonality").as_string();
        t.location   = node.attribute ("Location").as_string();

        for (const auto& a : node.attributes())
            if (! isNamed (a.name()))
                t.otherAttributes.emplace_back (a.name(), a.value());

        for (const auto& child : node.children ("TEMPO"))
        {
            Tempo tempo;
            tempo.inizio  = toNumber (child.attribute ("Inizio").as_string()).value_or (0);
            tempo.bpm     = toNumber (child.attribute ("Bpm").as_string()).value_or (0);
            tempo.metro   = child.attribute ("Metro").as_string ("4/4");
            tempo.battito = toInt (child.attribute ("Battito").as_string(), 1);
            t.tempos.push_back (tempo);
        }

        for (const auto& child : node.children ("POSITION_MARK"))
        {
            PositionMark mark;
            mark.name  = child.attribute ("Name").as_string();
            const auto type = toInt (child.attribute ("Type").as_string(), 0);
            mark.type  = (type >= 0 && type <= 4) ? static_cast<MarkType> (type) : MarkType::cue;
            mark.start = toNumber (child.attribute ("Start").as_string()).value_or (0);
            mark.end   = toNumber (child.attribute ("End").as_string());
            mark.num   = toInt (child.attribute ("Num").as_string(), -1);
            if (child.attribute ("Red") || child.attribute ("Green") || child.attribute ("Blue"))
                mark.colour = Rgb { child.attribute ("Red").as_int(),
                                    child.attribute ("Green").as_int(),
                                    child.attribute ("Blue").as_int() };
            t.marks.push_back (mark);
        }
        return t;
    }

    Node readNode (const pugi::xml_node& node)
    {
        Node n;
        n.kind = toInt (node.attribute ("Type").as_string(), 0) == 1 ? Node::Kind::playlist : Node::Kind::folder;
        n.name = node.attribute ("Name").as_string();
        n.keyType = toInt (node.attribute ("KeyType").as_string(), 0);
        if (n.kind == Node::Kind::folder)
        {
            for (const auto& child : node.children ("NODE"))
                n.children.push_back (readNode (child));
        }
        else
        {
            for (const auto& child : node.children ("TRACK"))
                n.trackKeys.emplace_back (child.attribute ("Key").as_string());
        }
        return n;
    }

    void setIfPresent (pugi::xml_node& node, const char* name, const std::string& value)
    {
        if (! value.empty())
            node.append_attribute (name) = value.c_str();
    }

    void writeTrack (pugi::xml_node& parent, const Track& t)
    {
        auto node = parent.append_child ("TRACK");
        setIfPresent (node, "TrackID", t.trackId);
        setIfPresent (node, "Name", t.name);
        setIfPresent (node, "Artist", t.artist);
        setIfPresent (node, "Album", t.album);
        setIfPresent (node, "Genre", t.genre);
        setIfPresent (node, "Kind", t.kind);
        if (t.totalTime)  node.append_attribute ("TotalTime")  = compact (*t.totalTime).c_str();
        if (t.averageBpm) node.append_attribute ("AverageBpm") = fixed (*t.averageBpm, 2).c_str();
        if (t.sampleRate) node.append_attribute ("SampleRate") = compact (*t.sampleRate).c_str();
        setIfPresent (node, "Tonality", t.tonality);
        node.append_attribute ("Location") = t.location.c_str();
        for (const auto& [name, value] : t.otherAttributes)
            node.append_attribute (name.c_str()) = value.c_str();

        for (const auto& tempo : t.tempos)
        {
            auto e = node.append_child ("TEMPO");
            e.append_attribute ("Inizio")  = fixed (tempo.inizio, 3).c_str();
            e.append_attribute ("Bpm")     = fixed (tempo.bpm, 2).c_str();
            e.append_attribute ("Metro")   = tempo.metro.c_str();
            e.append_attribute ("Battito") = tempo.battito;
        }

        for (const auto& mark : t.marks)
        {
            auto e = node.append_child ("POSITION_MARK");
            e.append_attribute ("Name")  = mark.name.c_str();
            e.append_attribute ("Type")  = static_cast<int> (mark.type);
            e.append_attribute ("Start") = fixed (mark.start, 3).c_str();
            if (mark.end)
                e.append_attribute ("End") = fixed (*mark.end, 3).c_str();
            e.append_attribute ("Num")   = mark.num;
            if (mark.colour)
            {
                e.append_attribute ("Red")   = mark.colour->red;
                e.append_attribute ("Green") = mark.colour->green;
                e.append_attribute ("Blue")  = mark.colour->blue;
            }
        }
    }

    void writeNode (pugi::xml_node& parent, const Node& n)
    {
        auto node = parent.append_child ("NODE");
        node.append_attribute ("Type") = static_cast<int> (n.kind);
        node.append_attribute ("Name") = n.name.c_str();
        if (n.kind == Node::Kind::folder)
        {
            node.append_attribute ("Count") = static_cast<unsigned long long> (n.children.size());
            for (const auto& child : n.children)
                writeNode (node, child);
        }
        else
        {
            node.append_attribute ("KeyType") = n.keyType;
            node.append_attribute ("Entries") = static_cast<unsigned long long> (n.trackKeys.size());
            for (const auto& key : n.trackKeys)
                node.append_child ("TRACK").append_attribute ("Key") = key.c_str();
        }
    }

    bool keepsAsIs (unsigned char c)
    {
        return std::isalnum (c) != 0 || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':';
    }

    int hexValue (char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
} // namespace

Collection parse (const std::string& xmlText)
{
    pugi::xml_document doc;
    const auto result = doc.load_buffer (xmlText.data(), xmlText.size(), pugi::parse_default, pugi::encoding_utf8);
    if (! result)
        throw std::runtime_error (std::string ("Not a readable XML file: ") + result.description()
                                  + " (at byte " + std::to_string (result.offset) + ")");

    const auto root = doc.child ("DJ_PLAYLISTS");
    if (! root)
        throw std::runtime_error ("Not a Rekordbox collection: the file has no DJ_PLAYLISTS element");

    Collection c;
    const auto product = root.child ("PRODUCT");
    c.productName    = product.attribute ("Name").as_string();
    c.productVersion = product.attribute ("Version").as_string();
    c.productCompany = product.attribute ("Company").as_string();

    for (const auto& track : root.child ("COLLECTION").children ("TRACK"))
        c.tracks.push_back (readTrack (track));

    if (const auto top = root.child ("PLAYLISTS").child ("NODE"))
        c.playlists = readNode (top);

    return c;
}

std::string write (const Collection& c)
{
    pugi::xml_document doc;
    auto declaration = doc.append_child (pugi::node_declaration);
    declaration.append_attribute ("version") = "1.0";
    declaration.append_attribute ("encoding") = "UTF-8";

    auto root = doc.append_child ("DJ_PLAYLISTS");
    root.append_attribute ("Version") = "1.0.0";

    auto product = root.append_child ("PRODUCT");
    product.append_attribute ("Name") = c.productName.c_str();
    product.append_attribute ("Version") = c.productVersion.c_str();
    product.append_attribute ("Company") = c.productCompany.c_str();

    auto collection = root.append_child ("COLLECTION");
    collection.append_attribute ("Entries") = static_cast<unsigned long long> (c.tracks.size());
    for (const auto& track : c.tracks)
        writeTrack (collection, track);

    auto playlists = root.append_child ("PLAYLISTS");
    writeNode (playlists, c.playlists);

    std::ostringstream out;
    doc.save (out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

std::string locationFromPath (const std::string& path)
{
    std::string p = path;
    for (auto& ch : p)
        if (ch == '\\')
            ch = '/';
    const bool windowsDrive = p.size() >= 2 && std::isalpha (static_cast<unsigned char> (p[0])) != 0 && p[1] == ':';
    if (windowsDrive)
        p.insert (p.begin(), '/');

    std::string url = "file://localhost";
    static const char* digits = "0123456789ABCDEF";
    for (const auto ch : p)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (keepsAsIs (c))
        {
            url += static_cast<char> (c);
        }
        else
        {
            url += '%';
            url += digits[c >> 4];
            url += digits[c & 0x0F];
        }
    }
    return url;
}

std::string pathFromLocation (const std::string& location)
{
    std::string rest;
    if (location.rfind ("file://localhost/", 0) == 0)
        rest = location.substr (16);
    else if (location.rfind ("file:///", 0) == 0)
        rest = location.substr (7);
    else
        return {};

    std::string path;
    for (std::size_t i = 0; i < rest.size(); ++i)
    {
        if (rest[i] == '%' && i + 2 < rest.size())
        {
            const auto hi = hexValue (rest[i + 1]), lo = hexValue (rest[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                path += static_cast<char> (hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        path += rest[i];
    }

    // "/C:/Music/a.mp3" is a Windows path.
    if (path.size() >= 3 && path[0] == '/' && std::isalpha (static_cast<unsigned char> (path[1])) != 0 && path[2] == ':')
        path.erase (0, 1);
    return path;
}

} // namespace dss1::rekordbox
