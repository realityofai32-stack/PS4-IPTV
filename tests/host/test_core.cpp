#include "check.h"
#include "../../src/core/json.h"
#include "../../src/core/url.h"
#include "../../src/core/utf8.h"

TEST(utf8_decode_encode) {
    std::string s = "A\xC5\x9F\xC4\x9F\xC4\xB0\xE2\x82\xAC\xF0\x9F\x93\xBA";  // A ş ğ İ € 📺
    std::u32string d = utf8::decode(s);
    CHECK_EQ(d.size(), (size_t) 6);
    CHECK(d[1] == 0x15F && d[2] == 0x11F && d[3] == 0x130 && d[4] == 0x20AC && d[5] == 0x1F4FA);
    std::string back;
    for (char32_t c: d) {
        back += utf8::encode(c);
    }
    CHECK(back == s);
    CHECK_EQ(utf8::length(s), (size_t) 6);
}

TEST(utf8_invalid_input_never_breaks) {
    std::u32string d = utf8::decode(std::string("ok\xFF\xC5", 4));
    CHECK_EQ(d.size(), (size_t) 4);
    CHECK(d[2] == 0xFFFD && d[3] == 0xFFFD);
    d = utf8::decode(std::string("\xE2\x82", 2));  // truncated
    CHECK(!d.empty() && d[0] == 0xFFFD);
    d = utf8::decode(std::string("\xC0\x80", 2));  // overlong
    CHECK(d[0] == 0xFFFD);
}

TEST(utf8_pop_back_and_fold) {
    std::string s = "a\xC5\x9F";
    utf8::popBack(s);
    CHECK(s == "a");
    std::u32string f = utf8::foldForSearch("TRT \xC5\x9E" "EN \xC4\xB0");  // "TRT ŞEN İ"
    std::u32string g = utf8::foldForSearch("trt \xC5\x9F" "en i");
    CHECK(f == g);
}

TEST(json_parse_xtream_types) {
    json::Value v;
    std::string err;
    CHECK(json::parse("\xEF\xBB\xBF{\"a\":\"12\",\"b\":12,\"c\":true,\"d\":null,\"e\":\"1\",\"f\":[1,2,{\"g\":\"x\"}],"
                      "\"h\":\"\\u015f\\ud83d\\udcfa\\n\"}", v, &err));
    CHECK_EQ(v["a"].asInt(), (int64_t) 12);
    CHECK_EQ(v["b"].asString(), std::string("12"));
    CHECK(v["c"].asBool());
    CHECK(v["d"].isNull());
    CHECK(v["e"].asBool());
    CHECK_EQ(v["f"].size(), (size_t) 3);
    CHECK_EQ(v["f"].at(2)["g"].asString(), std::string("x"));
    CHECK(v["h"].asString() == "\xC5\x9F\xF0\x9F\x93\xBA\n");
    CHECK(v["missing"]["deeper"].isNull());
    CHECK_EQ(v["missing"].asInt(7), (int64_t) 7);
}

TEST(json_numbers_and_big_ids) {
    json::Value v;
    CHECK(json::parse("{\"id\":9007199254740993,\"f\":\"4.5\",\"neg\":-3,\"exp\":1e3,\"empty\":\"\"}", v));
    CHECK_EQ(v["id"].asString(), std::string("9007199254740993"));  // source text kept exactly
    CHECK_EQ(v["f"].asDouble(), 4.5);
    CHECK_EQ(v["neg"].asInt(), (int64_t) -3);
    CHECK_EQ(v["exp"].asInt(), (int64_t) 1000);
    CHECK_EQ(v["empty"].asInt(42), (int64_t) 42);
}

TEST(json_malformed_inputs_fail_cleanly) {
    json::Value v;
    std::string err;
    for (const char *bad: {"", "{", "{\"a\":}", "[1,2", "{\"a\" 1}", "nul", "\"abc", "{\"a\":1}x", "[1,]"}) {
        err.clear();
        CHECK(!json::parse(bad, v, &err));
        CHECK(!err.empty());
        CHECK(v.isNull());
    }
    std::string deep(200, '[');
    CHECK(!json::parse(deep, v, &err));
}

TEST(json_write_roundtrip) {
    json::Value o = json::Value::makeObject();
    o.set("name", json::Value::makeString("Ma\xC3\xA7 \"1\"\n\\"));
    o.set("n", json::Value::makeInt(-5));
    o.set("b", json::Value::makeBool(false));
    json::Value a = json::Value::makeArray();
    a.push(json::Value::makeInt(1));
    a.push(json::Value());
    o.set("a", a);
    for (bool pretty: {true, false}) {
        json::Value back;
        CHECK(json::parse(json::write(o, pretty), back));
        CHECK(back["name"].asString() == o["name"].asString());
        CHECK_EQ(back["n"].asInt(), (int64_t) -5);
        CHECK(!back["b"].asBool(true));
        CHECK(back["a"].at(1).isNull());
    }
}

TEST(url_encode) {
    CHECK_EQ(url::encode("a b/c?d=e&f@g:h#%+!~._-"), std::string("a%20b%2Fc%3Fd%3De%26f%40g%3Ah%23%25%2B%21~._-"));
    CHECK_EQ(url::encode("\xC5\x9F"), std::string("%C5%9F"));
}

TEST(url_normalize_server) {
    auto s = url::normalizeServer("  example.com:8080  ");
    CHECK(s.ok);
    CHECK_EQ(s.base, std::string("http://example.com:8080"));
    s = url::normalizeServer("HTTP://Example.COM:8080/");
    CHECK(s.ok && s.base == "http://example.com:8080");
    s = url::normalizeServer("http://example.com:8080/player_api.php?username=u&password=p");
    CHECK(s.ok && s.base == "http://example.com:8080");
    s = url::normalizeServer("http://example.com/get.php?username=u&password=p&type=m3u_plus");
    CHECK(s.ok && s.base == "http://example.com");
    s = url::normalizeServer("http://user:pass@example.com:80");
    CHECK(s.ok && s.base == "http://example.com" && s.displayHost == "example.com");
    s = url::normalizeServer("https://secure.example.com");
    CHECK(s.ok && s.scheme == "https");
    s = url::normalizeServer("http://example.com/panel/");
    CHECK(s.ok && s.base == "http://example.com/panel");
    s = url::normalizeServer("10.0.0.5:25461");
    CHECK(s.ok && s.base == "http://10.0.0.5:25461");
    for (const char *bad: {"", "   ", "ftp://x.com", "http://", "x.com:99999", "x.com:abc", "bad host.com"}) {
        CHECK(!url::normalizeServer(bad).ok);
        CHECK(!url::normalizeServer(bad).error.empty());
    }
}
