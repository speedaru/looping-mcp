#include "loo/types.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

namespace loo {

namespace {

enum Type : std::uint32_t { Text = 0, Numeric = 1, DateTime = 2, Boolean = 3, Xml = 5, Geography = 6, Geometry = 7 };

struct TextFamily {
    const char* name;
    std::uint32_t kind;     // 0 fixed, 1 variable, 2 large object
    std::uint32_t charset;  // 0 ascii, 1 unicode, 2 binary
};

constexpr TextFamily kText[] = {
    {"VARCHAR", 1, 0}, {"NVARCHAR", 1, 1}, {"VARBINARY", 1, 2},  //
    {"CHAR", 0, 0},    {"NCHAR", 0, 1},    {"BINARY", 0, 2},     //
    {"TEXT", 2, 0},    {"NTEXT", 2, 1},    {"BLOB", 2, 2},
};

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string strip(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    }
    return out;
}

void resetType(Param& p) {
    p.withTimeZone = 0;
    p.type = Numeric;
    p.textKind = 1;
    p.textCharset = 0;
    p.numericKind = 0;
    p.intSize = 2;
    p.realPrecision = 1;
    p.dateKind = 0;
}

// Text lengths: one stored length per charset x kind (verified with longueur.loo).
//   ASCII fixed d100, ASCII variable d112, Unicode fixed d104, Unicode variable d116,
//   binary fixed d108, binary variable d120.   (d104_128[0] is d104, [1] d108, [2] d112, [3] d116, [4] d120)
std::uint32_t* lengthSlot(Param& p, std::uint32_t kind, std::uint32_t charset) {
    if (kind > 1 || charset > 2) return nullptr;  // large objects have no length
    if (kind == 0 && charset == 0) return &p.d100;
    // fixed: unicode d104, binary d108; variable: ascii d112, unicode d116, binary d120
    static constexpr int fixedIdx[3] = {-1, 0, 1};
    static constexpr int varIdx[3] = {2, 3, 4};
    return &p.d104_128[static_cast<std::size_t>(kind == 0 ? fixedIdx[charset] : varIdx[charset])];
}

constexpr int kMaxTextLength = 65535;
constexpr int kMaxDecimalDigits = 65;

}  // namespace

const char* supportedTypeNames() {
    return "VARCHAR(n), NVARCHAR(n), VARBINARY(n), CHAR(n), NCHAR(n), BINARY(n), TEXT, NTEXT, BLOB, TINYINT, "
           "SMALLINT, INT, BIGINT, DECIMAL(digits,decimals), FLOAT, DOUBLE, MONEY, COUNTER, DATE, TIME, DATETIME, "
           "BOOLEAN, XML, GEOGRAPHY, GEOMETRY";
}

void applyType(Param& p, const std::string& sqlType) {
    std::string t = upper(strip(sqlType));
    std::vector<int> args;  // numbers inside the brackets: (50) or (10,2)
    if (auto open = t.find('('); open != std::string::npos) {
        auto close = t.find(')', open);
        if (close == std::string::npos || close != t.size() - 1) throw Error("malformed type '" + sqlType + "'");
        const std::string inside = t.substr(open + 1, close - open - 1);
        std::size_t start = 0;
        while (true) {
            const auto comma = inside.find(',', start);
            const std::string part = inside.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (part.empty() || part.size() > 6 ||
                !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); })) {
                throw Error("malformed type arguments in '" + sqlType + "'");
            }
            args.push_back(std::stoi(part));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        t = t.substr(0, open);
    }
    std::optional<int> length;
    if (args.size() == 1) length = args[0];

    for (const auto& f : kText) {
        if (t == f.name) {
            if (args.size() > 1) throw Error(t + " takes a single length");
            if (f.kind == 2 && !args.empty()) throw Error(t + " does not take a length");
            if (length && (*length < 1 || *length > kMaxTextLength)) {
                throw Error("text length must be between 1 and " + std::to_string(kMaxTextLength));
            }
            resetType(p);
            p.type = Text;
            p.textKind = f.kind;
            p.textCharset = f.charset;
            if (auto* slot = lengthSlot(p, f.kind, f.charset)) {
                if (length) {
                    *slot = static_cast<std::uint32_t>(*length);
                } else if (*slot == 0) {
                    *slot = 50;  // Looping's default length
                }
            }
            return;
        }
    }

    if (!args.empty() && t != "DECIMAL" && t != "NUMERIC") throw Error(t + " does not take arguments");
    resetType(p);

    struct Numeric_ { const char* name; std::uint32_t kind, intSize, real; };
    constexpr Numeric_ kNum[] = {
        {"TINYINT", 0, 0, 1}, {"SMALLINT", 0, 1, 1}, {"INT", 0, 2, 1}, {"INTEGER", 0, 2, 1}, {"BIGINT", 0, 3, 1},
        {"DECIMAL", 1, 2, 1}, {"NUMERIC", 1, 2, 1}, {"FLOAT", 2, 2, 0},     {"REAL", 2, 2, 0}, {"DOUBLE", 2, 2, 1}, {"MONEY", 3, 2, 1},
        {"COUNTER", 4, 2, 1},
    };
    for (const auto& n : kNum) {
        if (t == n.name) {
            p.type = Numeric;
            p.numericKind = n.kind;
            p.intSize = n.intSize;
            p.realPrecision = n.real;
            if (n.kind == 1) {
                // DECIMAL(p,s): d124 = digits in total, d128 = digits after the point (Looping default 15,2).
                std::uint32_t& precision = p.d104_128[5];
                std::uint32_t& scale = p.d104_128[6];
                if (precision == 0 && scale == 0) {
                    precision = 15;
                    scale = 2;
                }
                if (!args.empty()) {
                    if (args.size() > 2) throw Error(t + " takes (digits) or (digits,decimals)");
                    if (args[0] < 1 || args[0] > kMaxDecimalDigits) {
                        throw Error("decimal digits must be between 1 and " + std::to_string(kMaxDecimalDigits));
                    }
                    const int decimals = args.size() == 2 ? args[1] : 0;
                    if (decimals < 0 || decimals > args[0]) throw Error("decimals cannot exceed the total number of digits");
                    precision = static_cast<std::uint32_t>(args[0]);
                    scale = static_cast<std::uint32_t>(decimals);
                }
            }
            return;
        }
    }

    if (t == "DATE") { p.type = DateTime; p.dateKind = 0; return; }
    if (t == "TIME") { p.type = DateTime; p.dateKind = 1; return; }
    if (t == "DATETIME") { p.type = DateTime; p.dateKind = 2; return; }
    if (t == "BOOLEAN" || t == "BOOL") { p.type = Boolean; return; }
    if (t == "XML") { p.type = Xml; return; }
    if (t == "GEOGRAPHY") { p.type = Geography; return; }
    if (t == "GEOMETRY") { p.type = Geometry; return; }

    throw Error("unknown type '" + sqlType + "'. Supported: " + supportedTypeNames());
}

std::string formatType(const Param& p) {
    switch (p.type) {
        case Text:
            for (const auto& f : kText) {
                if (f.kind == p.textKind && f.charset == p.textCharset) {
                    std::string name = f.name;
                    Param copy = p;
                    if (auto* slot = lengthSlot(copy, f.kind, f.charset)) name += "(" + std::to_string(*slot) + ")";
                    return name;
                }
            }
            return "TEXT?";
        case Numeric:
            switch (p.numericKind) {
                case 0: {
                    constexpr const char* names[] = {"TINYINT", "SMALLINT", "INT", "BIGINT"};
                    return p.intSize < 4 ? names[p.intSize] : "INT?";
                }
                case 1:
                    return "DECIMAL(" + std::to_string(p.d104_128[5]) + "," + std::to_string(p.d104_128[6]) + ")";
                case 2: return p.realPrecision == 0 ? "FLOAT" : "DOUBLE";
                case 3: return "MONEY";
                case 4: return "COUNTER";
                default: return "NUMERIC?";
            }
        case DateTime: {
            constexpr const char* names[] = {"DATE", "TIME", "DATETIME"};
            return p.dateKind < 3 ? names[p.dateKind] : "DATE?";
        }
        case Boolean: return "BOOLEAN";
        case Xml: return "XML";
        case Geography: return "GEOGRAPHY";
        case Geometry: return "GEOMETRY";
        default: return "UNKNOWN(" + std::to_string(p.type) + ")";
    }
}

}  // namespace loo
