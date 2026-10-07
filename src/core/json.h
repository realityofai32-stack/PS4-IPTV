// Small JSON DOM parser/writer for configuration files and small Xtream responses.
// Numbers keep their source text (Xtream IDs can exceed what callers expect, and panels mix
// "123" and 123), and the typed accessors coerce between strings/numbers/booleans.

#ifndef PS4IPTV_CORE_JSON_H
#define PS4IPTV_CORE_JSON_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace json {

    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    class Value {

    public:

        Value() = default;

        static Value makeString(std::string s);

        static Value makeNumber(double d);

        static Value makeInt(int64_t i);

        static Value makeBool(bool b);

        static Value makeArray();

        static Value makeObject();

        Type type() const { return t; }

        bool isNull() const { return t == Type::Null; }

        bool isObject() const { return t == Type::Object; }

        bool isArray() const { return t == Type::Array; }

        bool isString() const { return t == Type::String; }

        bool isNumber() const { return t == Type::Number; }

        bool isBool() const { return t == Type::Bool; }

        // object access: a shared null value when missing or not an object
        const Value &operator[](const char *key) const;

        const Value &operator[](const std::string &key) const { return (*this)[key.c_str()]; }

        bool has(const char *key) const;

        // array access
        size_t size() const;

        const Value &at(size_t index) const;

        const std::vector<Value> &items() const { return arr; }

        const std::vector<std::pair<std::string, Value>> &members() const { return obj; }

        // coercing accessors
        std::string asString(const std::string &def = "") const;

        int64_t asInt(int64_t def = 0) const;

        double asDouble(double def = 0) const;

        bool asBool(bool def = false) const;

        // building
        Value &set(const std::string &key, Value v);

        Value &push(Value v);

    private:

        Type t = Type::Null;
        bool b = false;
        std::string str;  // string value, or number source text
        std::vector<Value> arr;
        std::vector<std::pair<std::string, Value>> obj;

        friend class Parser;

        friend std::string write(const Value &, bool);
    };

    // Parses UTF-8 JSON (optional BOM). On failure returns false with a message containing the offset.
    bool parse(const std::string &text, Value &out, std::string *error = nullptr);

    std::string write(const Value &value, bool pretty = true);
}

#endif // PS4IPTV_CORE_JSON_H
