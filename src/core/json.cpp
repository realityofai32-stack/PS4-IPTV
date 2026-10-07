#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "json.h"
#include "utf8.h"

namespace json {

    namespace {
        const Value NULL_VALUE;
        const int MAX_DEPTH = 64;
    }

    Value Value::makeString(std::string s) {
        Value v;
        v.t = Type::String;
        v.str = std::move(s);
        return v;
    }

    Value Value::makeNumber(double d) {
        Value v;
        v.t = Type::Number;
        char buf[64];
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9e15) {
            snprintf(buf, sizeof(buf), "%.0f", d);
        } else if (std::isfinite(d)) {
            snprintf(buf, sizeof(buf), "%.17g", d);
        } else {
            snprintf(buf, sizeof(buf), "0");
        }
        v.str = buf;
        return v;
    }

    Value Value::makeInt(int64_t i) {
        Value v;
        v.t = Type::Number;
        v.str = std::to_string(i);
        return v;
    }

    Value Value::makeBool(bool bv) {
        Value v;
        v.t = Type::Bool;
        v.b = bv;
        return v;
    }

    Value Value::makeArray() {
        Value v;
        v.t = Type::Array;
        return v;
    }

    Value Value::makeObject() {
        Value v;
        v.t = Type::Object;
        return v;
    }

    const Value &Value::operator[](const char *key) const {
        if (t == Type::Object) {
            for (const auto &m: obj) {
                if (m.first == key) {
                    return m.second;
                }
            }
        }
        return NULL_VALUE;
    }

    bool Value::has(const char *key) const {
        if (t != Type::Object) {
            return false;
        }
        for (const auto &m: obj) {
            if (m.first == key) {
                return true;
            }
        }
        return false;
    }

    size_t Value::size() const {
        return t == Type::Array ? arr.size() : t == Type::Object ? obj.size() : 0;
    }

    const Value &Value::at(size_t index) const {
        return t == Type::Array && index < arr.size() ? arr[index] : NULL_VALUE;
    }

    std::string Value::asString(const std::string &def) const {
        switch (t) {
            case Type::String:
            case Type::Number:
                return str;
            case Type::Bool:
                return b ? "true" : "false";
            default:
                return def;
        }
    }

    int64_t Value::asInt(int64_t def) const {
        if (t == Type::Bool) {
            return b ? 1 : 0;
        }
        if (t != Type::Number && t != Type::String) {
            return def;
        }
        const char *s = str.c_str();
        while (*s == ' ') {
            s++;
        }
        if (*s == '\0') {
            return def;
        }
        char *end = nullptr;
        errno = 0;
        long long v = strtoll(s, &end, 10);
        if (end == s || errno == ERANGE) {
            // "12.0" or "1e3": go through double
            char *dend = nullptr;
            double d = strtod(s, &dend);
            return dend == s ? def : (int64_t) d;
        }
        if (*end == '.' || *end == 'e' || *end == 'E') {
            return (int64_t) strtod(s, nullptr);
        }
        return (int64_t) v;
    }

    double Value::asDouble(double def) const {
        if (t == Type::Bool) {
            return b ? 1 : 0;
        }
        if (t != Type::Number && t != Type::String) {
            return def;
        }
        char *end = nullptr;
        double d = strtod(str.c_str(), &end);
        return end == str.c_str() ? def : d;
    }

    bool Value::asBool(bool def) const {
        switch (t) {
            case Type::Bool:
                return b;
            case Type::Number:
                return asDouble() != 0;
            case Type::String:
                if (str == "1" || str == "true" || str == "True" || str == "TRUE" || str == "yes") {
                    return true;
                }
                if (str == "0" || str == "false" || str == "False" || str == "FALSE" || str == "no" || str.empty()) {
                    return false;
                }
                return def;
            default:
                return def;
        }
    }

    Value &Value::set(const std::string &key, Value v) {
        if (t != Type::Object) {
            *this = makeObject();
        }
        for (auto &m: obj) {
            if (m.first == key) {
                m.second = std::move(v);
                return m.second;
            }
        }
        obj.emplace_back(key, std::move(v));
        return obj.back().second;
    }

    Value &Value::push(Value v) {
        if (t != Type::Array) {
            *this = makeArray();
        }
        arr.push_back(std::move(v));
        return arr.back();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    class Parser {

    public:

        Parser(const char *data, size_t len) : p(data), begin(data), end(data + len) {}

        bool run(Value &out) {
            if (end - p >= 3 && (unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB
                && (unsigned char) p[2] == 0xBF) {
                p += 3;
            }
            skipWs();
            if (!parseValue(out, 0)) {
                return false;
            }
            skipWs();
            if (p != end) {
                return fail("trailing characters");
            }
            return true;
        }

        std::string error;

        bool streamObjects(const std::function<bool(const FlatObject &)> &fn) {
            if (end - p >= 3 && (unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB
                && (unsigned char) p[2] == 0xBF) {
                p += 3;
            }
            skipWs();
            if (p >= end || *p != '[') {
                return fail("expected a JSON array");
            }
            p++;
            skipWs();
            if (p < end && *p == ']') {
                p++;
                return true;
            }
            FlatObject obj;
            while (true) {
                skipWs();
                if (p >= end) {
                    return fail("unterminated array");
                }
                if (*p == '{') {
                    obj.fields.clear();
                    if (!parseFlatObject(obj)) {
                        return false;
                    }
                    if (!fn(obj)) {
                        return true;
                    }
                } else {
                    Value skipped;
                    if (!parseValue(skipped, 1)) {
                        return false;
                    }
                }
                skipWs();
                if (p >= end) {
                    return fail("unterminated array");
                }
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == ']') {
                    p++;
                    return true;
                }
                return fail("expected , or ]");
            }
        }

    private:

        bool parseFlatObject(FlatObject &obj) {
            p++;  // {
            skipWs();
            if (p < end && *p == '}') {
                p++;
                return true;
            }
            while (true) {
                skipWs();
                if (p >= end || *p != '"') {
                    return fail("expected key");
                }
                FlatObject::Field f;
                if (!parseString(f.key)) {
                    return false;
                }
                skipWs();
                if (p >= end || *p != ':') {
                    return fail("expected :");
                }
                p++;
                skipWs();
                if (p >= end) {
                    return fail("unexpected end");
                }
                if (*p == '{' || *p == '[') {
                    Value nested;  // rare in Xtream lists (category_ids): parsed and dropped
                    if (!parseValue(nested, 2)) {
                        return false;
                    }
                    f.type = nested.type();
                } else {
                    Value scalar;
                    if (!parseValue(scalar, 2)) {
                        return false;
                    }
                    f.type = scalar.type();
                    if (f.type == Type::Bool) {
                        f.value = scalar.asBool() ? "true" : "false";
                    } else if (f.type != Type::Null) {
                        f.value = std::move(scalar.str);
                    }
                }
                obj.fields.push_back(std::move(f));
                skipWs();
                if (p >= end) {
                    return fail("unterminated object");
                }
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == '}') {
                    p++;
                    return true;
                }
                return fail("expected , or }");
            }
        }

        const char *p;
        const char *begin;
        const char *end;

        bool fail(const char *what) {
            if (error.empty()) {
                error = std::string(what) + " at offset " + std::to_string(p - begin);
            }
            return false;
        }

        void skipWs() {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                p++;
            }
        }

        bool literal(const char *word) {
            size_t n = strlen(word);
            if ((size_t) (end - p) < n || memcmp(p, word, n) != 0) {
                return fail("invalid literal");
            }
            p += n;
            return true;
        }

        bool parseValue(Value &v, int depth) {
            if (depth > MAX_DEPTH) {
                return fail("nesting too deep");
            }
            if (p >= end) {
                return fail("unexpected end");
            }
            switch (*p) {
                case '{':
                    return parseObject(v, depth);
                case '[':
                    return parseArray(v, depth);
                case '"':
                    v.t = Type::String;
                    return parseString(v.str);
                case 't':
                    v.t = Type::Bool;
                    v.b = true;
                    return literal("true");
                case 'f':
                    v.t = Type::Bool;
                    v.b = false;
                    return literal("false");
                case 'n':
                    v.t = Type::Null;
                    return literal("null");
                default:
                    return parseNumber(v);
            }
        }

        bool parseNumber(Value &v) {
            const char *start = p;
            if (p < end && *p == '-') {
                p++;
            }
            if (p >= end || !(*p >= '0' && *p <= '9')) {
                return fail("invalid value");
            }
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+'
                               || *p == '-')) {
                p++;
            }
            v.t = Type::Number;
            v.str.assign(start, (size_t) (p - start));
            return true;
        }

        static int hex(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool readHex4(unsigned &out) {
            if (end - p < 4) {
                return fail("truncated \\u escape");
            }
            out = 0;
            for (int i = 0; i < 4; i++) {
                int h = hex(p[i]);
                if (h < 0) {
                    return fail("invalid \\u escape");
                }
                out = (out << 4) | (unsigned) h;
            }
            p += 4;
            return true;
        }

        bool parseString(std::string &out) {
            p++;  // opening quote
            out.clear();
            const char *run = p;
            while (p < end) {
                char c = *p;
                if (c == '"') {
                    out.append(run, (size_t) (p - run));
                    p++;
                    return true;
                }
                if (c == '\\') {
                    out.append(run, (size_t) (p - run));
                    p++;
                    if (p >= end) {
                        return fail("truncated escape");
                    }
                    char e = *p++;
                    switch (e) {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            unsigned cp;
                            if (!readHex4(cp)) {
                                return false;
                            }
                            if (cp >= 0xD800 && cp <= 0xDBFF) {
                                unsigned lo = 0;
                                if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                                    p += 2;
                                    if (!readHex4(lo)) {
                                        return false;
                                    }
                                }
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    cp = 0xFFFD;
                                }
                            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                                cp = 0xFFFD;
                            }
                            out += utf8::encode(cp);
                            break;
                        }
                        default:
                            return fail("invalid escape");
                    }
                    run = p;
                    continue;
                }
                if ((unsigned char) c < 0x20) {
                    // tolerate raw control characters some panels emit inside strings
                    out.append(run, (size_t) (p - run));
                    out += ' ';
                    p++;
                    run = p;
                    continue;
                }
                p++;
            }
            return fail("unterminated string");
        }

        bool parseArray(Value &v, int depth) {
            p++;
            v.t = Type::Array;
            skipWs();
            if (p < end && *p == ']') {
                p++;
                return true;
            }
            while (true) {
                v.arr.emplace_back();
                skipWs();
                if (!parseValue(v.arr.back(), depth + 1)) {
                    return false;
                }
                skipWs();
                if (p >= end) {
                    return fail("unterminated array");
                }
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == ']') {
                    p++;
                    return true;
                }
                return fail("expected , or ]");
            }
        }

        bool parseObject(Value &v, int depth) {
            p++;
            v.t = Type::Object;
            skipWs();
            if (p < end && *p == '}') {
                p++;
                return true;
            }
            while (true) {
                skipWs();
                if (p >= end || *p != '"') {
                    return fail("expected key");
                }
                std::string key;
                if (!parseString(key)) {
                    return false;
                }
                skipWs();
                if (p >= end || *p != ':') {
                    return fail("expected :");
                }
                p++;
                skipWs();
                v.obj.emplace_back(std::move(key), Value());
                if (!parseValue(v.obj.back().second, depth + 1)) {
                    return false;
                }
                skipWs();
                if (p >= end) {
                    return fail("unterminated object");
                }
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == '}') {
                    p++;
                    return true;
                }
                return fail("expected , or }");
            }
        }
    };

    bool parse(const std::string &text, Value &out, std::string *error) {
        Parser parser(text.data(), text.size());
        out = Value();
        bool ok = parser.run(out);
        if (!ok) {
            out = Value();
            if (error) {
                *error = parser.error;
            }
        }
        return ok;
    }

    bool forEachObject(const std::string &text, const std::function<bool(const FlatObject &)> &fn,
                       std::string *error) {
        Parser parser(text.data(), text.size());
        bool ok = parser.streamObjects(fn);
        if (!ok && error) {
            *error = parser.error;
        }
        return ok;
    }

    namespace {
        const std::string EMPTY;

        const FlatObject::Field *findField(const FlatObject &o, const char *key) {
            for (const auto &f: o.fields) {
                if (f.key == key) {
                    return &f;
                }
            }
            return nullptr;
        }
    }

    const std::string &FlatObject::get(const char *key) const {
        const Field *f = findField(*this, key);
        return f && (f->type == Type::String || f->type == Type::Number || f->type == Type::Bool) ? f->value : EMPTY;
    }

    bool FlatObject::has(const char *key) const {
        return findField(*this, key) != nullptr;
    }

    int64_t FlatObject::getInt(const char *key, int64_t def) const {
        const Field *f = findField(*this, key);
        if (f == nullptr || f->type == Type::Null || f->value.empty()) {
            return def;
        }
        Value v = f->type == Type::Bool ? Value::makeBool(f->value == "true") : Value::makeString(f->value);
        return v.asInt(def);
    }

    double FlatObject::getDouble(const char *key, double def) const {
        const Field *f = findField(*this, key);
        if (f == nullptr || f->type == Type::Null || f->value.empty()) {
            return def;
        }
        return Value::makeString(f->value).asDouble(def);
    }

    bool FlatObject::getBool(const char *key, bool def) const {
        const Field *f = findField(*this, key);
        if (f == nullptr || f->type == Type::Null) {
            return def;
        }
        return Value::makeString(f->value).asBool(def);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    namespace {
        void writeString(std::string &out, const std::string &s) {
            out += '"';
            for (unsigned char c: s) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        } else {
                            out += (char) c;
                        }
                }
            }
            out += '"';
        }

        void writeValue(std::string &out, const Value &v, bool pretty, int indent) {
            auto newline = [&](int level) {
                if (pretty) {
                    out += '\n';
                    out.append((size_t) level * 2, ' ');
                }
            };
            switch (v.type()) {
                case Type::Null:
                    out += "null";
                    break;
                case Type::Bool:
                    out += v.asBool() ? "true" : "false";
                    break;
                case Type::Number:
                    out += v.asString();
                    break;
                case Type::String:
                    writeString(out, v.asString());
                    break;
                case Type::Array: {
                    if (v.items().empty()) {
                        out += "[]";
                        break;
                    }
                    out += '[';
                    bool first = true;
                    for (const auto &item: v.items()) {
                        if (!first) {
                            out += ',';
                        }
                        first = false;
                        newline(indent + 1);
                        writeValue(out, item, pretty, indent + 1);
                    }
                    newline(indent);
                    out += ']';
                    break;
                }
                case Type::Object: {
                    if (v.members().empty()) {
                        out += "{}";
                        break;
                    }
                    out += '{';
                    bool first = true;
                    for (const auto &m: v.members()) {
                        if (!first) {
                            out += ',';
                        }
                        first = false;
                        newline(indent + 1);
                        writeString(out, m.first);
                        out += pretty ? ": " : ":";
                        writeValue(out, m.second, pretty, indent + 1);
                    }
                    newline(indent);
                    out += '}';
                    break;
                }
            }
        }
    }

    std::string write(const Value &value, bool pretty) {
        std::string out;
        writeValue(out, value, pretty, 0);
        if (pretty) {
            out += '\n';
        }
        return out;
    }
}
