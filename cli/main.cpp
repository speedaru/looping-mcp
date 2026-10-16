// looping-cli: edit Looping (.loo) schema files from the command line. Prints JSON.
//
//   looping-cli new FILE
//   looping-cli show FILE
//   looping-cli describe FILE      (MCD + derived MLD as text, read-only)
//   looping-cli types
//   looping-cli add-entity|add-association FILE NAME [--x N] [--y N] [--comment TEXT]
//   looping-cli set-comment FILE (--entity E | --association A) [--comment TEXT]   (no text clears it)
//   looping-cli rename-entity|rename-association FILE NAME NEWNAME
//   looping-cli move-entity|move-association FILE NAME X Y
//   looping-cli layout FILE [--only NAME]...        (arrange everything, or just the named objects)
//   looping-cli remove-entity|remove-association FILE NAME
//   looping-cli add-attribute FILE (--entity E | --association A) --name N [--type T]
//                             [--identifier] [--not-null] [--unique] [--time-zone] [--logical-name L] [--comment C]
//   looping-cli update-attribute FILE (--entity E | --association A) --attribute N
//                             [--name N] [--type T] [--identifier true|false] [--not-null true|false]
//                             [--unique true|false] [--time-zone true|false] [--logical-name L] [--comment C]
//   looping-cli remove-attribute FILE (--entity E | --association A) --attribute N
//   looping-cli add-link FILE ENTITY ASSOCIATION CARDINALITY [--role TEXT]
//   looping-cli set-cardinality FILE ENTITY ASSOCIATION CARDINALITY [--link NAME]
//   looping-cli set-link-role FILE ENTITY ASSOCIATION [ROLE] [--link NAME]   (no ROLE removes it)
//   looping-cli remove-link FILE ENTITY ASSOCIATION [--link NAME]
//   (--link NAME, e.g. "Lien 2", is needed only when an entity is linked twice to the same association)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "loo/editor.hpp"
#include "loo/mld.hpp"
#include "loo/types.hpp"

namespace fs = std::filesystem;
using loo::OwnerKind;

namespace {

// ---- arguments (UTF-8 on every platform) -------------------------------------------------------

std::vector<std::string> utf8Args(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    LocalFree(w);
    return out;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // --key value, or --flag (value "true")
    std::map<std::string, std::vector<std::string>> repeated;  // every value given for a --key, in order
    std::set<std::string> used;

    static Args parse(const std::vector<std::string>& v, std::size_t from) {
        static const std::set<std::string> flags = {"identifier", "not-null", "unique", "time-zone"};
        Args a;
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].rfind("--", 0) == 0) {
                const std::string key = v[i].substr(2);
                if (flags.count(key) && (i + 1 >= v.size() || v[i + 1].rfind("--", 0) == 0)) {
                    a.options[key] = "true";
                } else if (i + 1 < v.size()) {
                    a.options[key] = v[++i];
                    a.repeated[key].push_back(v[i]);
                } else {
                    throw loo::Error("option --" + key + " needs a value");
                }
            } else {
                a.positional.push_back(v[i]);
            }
        }
        return a;
    }

    std::optional<std::string> opt(const std::string& k) {
        auto it = options.find(k);
        if (it == options.end()) return std::nullopt;
        used.insert(k);
        return it->second;
    }

    std::vector<std::string> all(const std::string& k) {
        used.insert(k);
        auto it = repeated.find(k);
        return it == repeated.end() ? std::vector<std::string>{} : it->second;
    }

    std::string pos(std::size_t i, const char* what) const {
        if (i >= positional.size()) throw loo::Error(std::string("missing argument: ") + what);
        return positional[i];
    }

    std::optional<bool> boolOpt(const std::string& k) {
        auto v = opt(k);
        if (!v) return std::nullopt;
        if (*v == "true" || *v == "1" || *v == "yes") return true;
        if (*v == "false" || *v == "0" || *v == "no") return false;
        throw loo::Error("--" + k + " expects true or false");
    }

    void rejectUnknown() const {
        for (const auto& [k, v] : options) {
            if (!used.count(k)) throw loo::Error("unknown option --" + k);
        }
    }
};

int toInt(const std::string& s, const char* what) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used != s.size()) throw std::invalid_argument("");
        return v;
    } catch (...) {
        throw loo::Error(std::string(what) + " must be an integer");
    }
}

// ---- files ---------------------------------------------------------------------------------------

fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::vector<std::uint8_t> readFile(const std::string& name) {
    std::ifstream f(pathOf(name), std::ios::binary);
    if (!f) throw loo::Error("cannot open '" + name + "'");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Writes next to the target and renames, so an error never leaves a half-written file.
void writeFile(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const fs::path target = pathOf(name);
    fs::path tmp = target;
    tmp += ".tmp";
    if (target.has_parent_path()) {
        std::error_code mk;
        fs::create_directories(target.parent_path(), mk);  // a model may go in a folder that does not exist yet
        if (mk) throw loo::Error("cannot create the folder for '" + name + "': " + mk.message());
    }
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw loo::Error("cannot write '" + name + "'");
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) throw loo::Error("write failed for '" + name + "'");
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) throw loo::Error("cannot replace '" + name + "': " + ec.message());
    }
}

// ---- JSON ----------------------------------------------------------------------------------------

std::string esc(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o + "\"";
}

std::string b(bool v) { return v ? "true" : "false"; }

std::string json(const loo::OwnerInfo& o) {
    std::string s = "{\"name\":" + esc(o.name) + ",\"x\":" + std::to_string(o.position.x) +
                    ",\"y\":" + std::to_string(o.position.y) + ",\"comment\":" + esc(o.comment) + ",\"attributes\":[";
    for (std::size_t i = 0; i < o.attributes.size(); ++i) {
        const auto& a = o.attributes[i];
        if (i) s += ",";
        s += "{\"name\":" + esc(a.name) + ",\"logicalName\":" + esc(a.logicalName) + ",\"type\":" + esc(a.type) +
             ",\"identifier\":" + b(a.identifier) + ",\"notNull\":" + b(a.notNull) + ",\"unique\":" + b(a.unique) +
             ",\"timeZone\":" + b(a.timeZone) +
             ",\"comment\":" + esc(a.comment) + "}";
    }
    return s + "]}";
}

std::string json(const loo::Schema& sc) {
    std::string s = "{\"entities\":[";
    for (std::size_t i = 0; i < sc.entities.size(); ++i) s += (i ? "," : "") + json(sc.entities[i]);
    s += "],\"associations\":[";
    for (std::size_t i = 0; i < sc.associations.size(); ++i) s += (i ? "," : "") + json(sc.associations[i]);
    s += "],\"links\":[";
    for (std::size_t i = 0; i < sc.links.size(); ++i) {
        const auto& l = sc.links[i];
        s += std::string(i ? "," : "") + "{\"name\":" + esc(l.name) + ",\"entity\":" + esc(l.entity) +
             ",\"association\":" + esc(l.association) + ",\"cardinality\":" + esc(loo::toString(l.cardinality)) +
             ",\"role\":" + esc(l.role) + "}";
    }
    return s + "]}";
}

// ---- commands ------------------------------------------------------------------------------------

OwnerKind kindFrom(Args& a, std::string& owner) {
    auto e = a.opt("entity");
    auto s = a.opt("association");
    if (e.has_value() == s.has_value()) throw loo::Error("give exactly one of --entity or --association");
    owner = e ? *e : *s;
    return e ? OwnerKind::Entity : OwnerKind::Association;
}

std::optional<loo::Point> pointFrom(Args& a) {
    auto x = a.opt("x");
    auto y = a.opt("y");
    if (x.has_value() != y.has_value()) throw loo::Error("give both --x and --y, or neither");
    if (!x) return std::nullopt;
    return loo::Point{toInt(*x, "--x"), toInt(*y, "--y")};
}

std::string run(const std::vector<std::string>& v) {
    if (v.size() < 2) throw loo::Error("usage: looping-cli <command> ... (see cli/main.cpp)");
    const std::string cmd = v[1];

    if (cmd == "types") return std::string("{\"ok\":true,\"types\":") + esc(loo::supportedTypeNames()) + "}";

    Args a = Args::parse(v, 2);
    const std::string file = a.pos(0, "FILE");

    if (cmd == "new") {
        if (fs::exists(pathOf(file))) throw loo::Error("'" + file + "' already exists");
        writeFile(file, loo::Editor::create().save());
        return "{\"ok\":true}";
    }

    auto ed = loo::Editor::open(readFile(file));
    bool changed = true;
    std::string result = "{\"ok\":true}";

    if (cmd == "show") {
        changed = false;
        result = "{\"ok\":true,\"schema\":" + json(ed.schema()) + "}";
    } else if (cmd == "describe") {
        changed = false;
        const auto schema = ed.schema();
        result = "{\"ok\":true,\"text\":" + esc(loo::describe(schema, loo::deriveMld(schema))) + "}";
    } else if (cmd == "add-entity" || cmd == "add-association") {
        const auto kind = cmd == "add-entity" ? OwnerKind::Entity : OwnerKind::Association;
        const std::string name = a.pos(1, "NAME");
        const auto where = pointFrom(a);
        const auto comment = a.opt("comment");
        a.rejectUnknown();
        ed.addOwner(kind, name, where);
        if (comment) ed.setOwnerComment(kind, name, *comment);
    } else if (cmd == "set-comment") {
        std::string owner;
        const auto kind = kindFrom(a, owner);
        const auto comment = a.opt("comment");
        a.rejectUnknown();
        ed.setOwnerComment(kind, owner, comment.value_or(""));
    } else if (cmd == "rename-entity" || cmd == "rename-association") {
        const auto kind = cmd == "rename-entity" ? OwnerKind::Entity : OwnerKind::Association;
        ed.renameOwner(kind, a.pos(1, "NAME"), a.pos(2, "NEWNAME"));
    } else if (cmd == "move-entity" || cmd == "move-association") {
        const auto kind = cmd == "move-entity" ? OwnerKind::Entity : OwnerKind::Association;
        ed.moveOwner(kind, a.pos(1, "NAME"),
                     loo::Point{toInt(a.pos(2, "X"), "X"), toInt(a.pos(3, "Y"), "Y")});
    } else if (cmd == "layout") {
        const auto only = a.all("only");
        a.rejectUnknown();
        const std::size_t moved = ed.autoLayout(only);
        changed = moved > 0;
        result = "{\"ok\":true,\"moved\":" + std::to_string(moved) + "}";
    } else if (cmd == "remove-entity" || cmd == "remove-association") {
        const auto kind = cmd == "remove-entity" ? OwnerKind::Entity : OwnerKind::Association;
        ed.removeOwner(kind, a.pos(1, "NAME"));
    } else if (cmd == "add-attribute") {
        std::string owner;
        const auto kind = kindFrom(a, owner);
        loo::AttributeSpec spec;
        auto name = a.opt("name");
        if (!name) throw loo::Error("--name is required");
        spec.name = *name;
        if (auto t = a.opt("type")) spec.type = *t;
        spec.identifier = a.boolOpt("identifier").value_or(false);
        spec.notNull = a.boolOpt("not-null").value_or(false);
        spec.unique = a.boolOpt("unique").value_or(false);
        spec.timeZone = a.boolOpt("time-zone").value_or(false);
        if (auto l = a.opt("logical-name")) spec.logicalName = *l;
        if (auto c = a.opt("comment")) spec.comment = *c;
        a.rejectUnknown();
        ed.addAttribute(kind, owner, spec);
    } else if (cmd == "update-attribute") {
        std::string owner;
        const auto kind = kindFrom(a, owner);
        auto attr = a.opt("attribute");
        if (!attr) throw loo::Error("--attribute is required");
        loo::AttributeUpdate u;
        u.name = a.opt("name");
        u.type = a.opt("type");
        u.logicalName = a.opt("logical-name");
        u.comment = a.opt("comment");
        u.identifier = a.boolOpt("identifier");
        u.notNull = a.boolOpt("not-null");
        u.unique = a.boolOpt("unique");
        u.timeZone = a.boolOpt("time-zone");
        a.rejectUnknown();
        ed.updateAttribute(kind, owner, *attr, u);
    } else if (cmd == "remove-attribute") {
        std::string owner;
        const auto kind = kindFrom(a, owner);
        auto attr = a.opt("attribute");
        if (!attr) throw loo::Error("--attribute is required");
        a.rejectUnknown();
        ed.removeAttribute(kind, owner, *attr);
    } else if (cmd == "add-link") {
        const std::string role = a.opt("role").value_or("");
        ed.addLink(a.pos(1, "ENTITY"), a.pos(2, "ASSOCIATION"), loo::parseCardinality(a.pos(3, "CARDINALITY")), role);
    } else if (cmd == "set-link-role") {
        const std::string link = a.opt("link").value_or("");
        ed.setLinkRole(a.pos(1, "ENTITY"), a.pos(2, "ASSOCIATION"), a.positional.size() > 3 ? a.positional[3] : "", link);
    } else if (cmd == "set-cardinality") {
        const std::string link = a.opt("link").value_or("");
        ed.setCardinality(a.pos(1, "ENTITY"), a.pos(2, "ASSOCIATION"), loo::parseCardinality(a.pos(3, "CARDINALITY")), link);
    } else if (cmd == "remove-link") {
        ed.removeLink(a.pos(1, "ENTITY"), a.pos(2, "ASSOCIATION"), a.opt("link").value_or(""));
    } else {
        throw loo::Error("unknown command '" + cmd + "'");
    }

    a.rejectUnknown();
    if (changed) writeFile(file, ed.save());
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string out = run(utf8Args(argc, argv));
        std::cout << out << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"ok\":false,\"error\":" << esc(e.what()) << "}\n";
        return 1;
    }
}
