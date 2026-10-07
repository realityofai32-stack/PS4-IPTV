#include "check.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/redact.h"

using namespace iptv;

static Profile profile() {
    Profile p;
    p.server = "http://example.com:8080";
    p.username = "user name";
    p.password = "p@ss/word";
    return p;
}

TEST(xtream_urls) {
    Profile p = profile();
    CHECK_EQ(xtream::apiUrl(p),
             std::string("http://example.com:8080/player_api.php?username=user%20name&password=p%40ss%2Fword"));
    CHECK_EQ(xtream::apiUrl(p, "get_live_categories"),
             std::string("http://example.com:8080/player_api.php?username=user%20name&password=p%40ss%2Fword"
                         "&action=get_live_categories"));
    CHECK_EQ(xtream::liveUrl(p, "123", "ts"), std::string("http://example.com:8080/live/user%20name/p%40ss%2Fword/123.ts"));
    CHECK_EQ(xtream::liveUrl(p, "123", "m3u8"),
             std::string("http://example.com:8080/live/user%20name/p%40ss%2Fword/123.m3u8"));
    CHECK_EQ(xtream::movieUrl(p, "77", "mkv"), std::string("http://example.com:8080/movie/user%20name/p%40ss%2Fword/77.mkv"));
    CHECK_EQ(xtream::movieUrl(p, "77", ".mp4"), std::string("http://example.com:8080/movie/user%20name/p%40ss%2Fword/77.mp4"));
    CHECK_EQ(xtream::seriesUrl(p, "9", "avi"), std::string("http://example.com:8080/series/user%20name/p%40ss%2Fword/9.avi"));
}

TEST(xtream_auth_ok_with_string_types) {
    const char *body = R"({"user_info":{"username":"u","password":"p","message":"Welcome","auth":1,"status":"Active",
        "exp_date":"1893456000","is_trial":"0","active_cons":"0","created_at":"1600000000","max_connections":"1",
        "allowed_output_formats":["m3u8","ts","rtmp"]},
        "server_info":{"url":"example.com","port":"8080","https_port":"8443","server_protocol":"http",
        "timezone":"Europe/Istanbul","timestamp_now":1790000000,"time_now":"2026-09-21 10:00:00"}})";
    AuthResult r = xtream::parseAuth(200, body, 1790000000);
    CHECK(r.status == AuthStatus::Ok);
    CHECK_EQ(r.account.status, std::string("Active"));
    CHECK_EQ(r.account.expiresAt, (int64_t) 1893456000);
    CHECK_EQ(r.account.maxConnections, 1);
    CHECK_EQ(r.account.activeConnections, 0);
    CHECK_EQ(r.account.outputFormats.size(), (size_t) 3);
    CHECK_EQ(r.account.serverProtocol, std::string("http"));
    CHECK_EQ(r.account.timezone, std::string("Europe/Istanbul"));
    CHECK(!r.account.trial);
}

TEST(xtream_auth_failures) {
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":0}})", 0).status == AuthStatus::InvalidCredentials);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":"0"}})", 0).status == AuthStatus::InvalidCredentials);
    CHECK(xtream::parseAuth(200, "[]", 0).status == AuthStatus::InvalidCredentials);
    CHECK(xtream::parseAuth(401, "", 0).status == AuthStatus::InvalidCredentials);
    CHECK(xtream::parseAuth(403, "", 0).status == AuthStatus::Refused);
    CHECK(xtream::parseAuth(502, "", 0).status == AuthStatus::ServerError);
    CHECK(xtream::parseAuth(404, "", 0).status == AuthStatus::ServerError);
    CHECK(xtream::parseAuth(200, "<html>nginx</html>", 0).status == AuthStatus::Malformed);
    CHECK(xtream::parseAuth(200, "", 0).status == AuthStatus::Malformed);
    CHECK(xtream::parseAuth(200, R"({"server_info":{}})", 0).status == AuthStatus::Malformed);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":1,"status":"Expired"}})", 0).status == AuthStatus::Expired);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":1,"status":"Active","exp_date":"1000"}})", 2000).status
          == AuthStatus::Expired);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":1,"status":"Active","exp_date":null}})", 2000).status
          == AuthStatus::Ok);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":1,"status":"Banned"}})", 0).status == AuthStatus::Banned);
    CHECK(xtream::parseAuth(200, R"({"user_info":{"auth":1,"status":"Disabled"}})", 0).status == AuthStatus::Disabled);
    for (int s = 0; s <= (int) AuthStatus::Network; s++) {
        CHECK(!xtream::authStatusText((AuthStatus) s).empty());
    }
}

TEST(xtream_categories) {
    std::vector<Category> cats;
    std::string err;
    CHECK(xtream::parseCategories(R"([{"category_id":"1","category_name":"News","parent_id":0},
        {"category_id":2,"category_name":"Spor ş"},{"category_name":"no id"},"junk",{"category_id":"3"}])",
                                  cats, err));
    CHECK_EQ(cats.size(), (size_t) 3);
    CHECK_EQ(cats[0].name, std::string("News"));
    CHECK_EQ(cats[1].id, std::string("2"));
    CHECK(cats[1].name == "Spor \xC5\x9F");
    CHECK_EQ(cats[2].name, std::string("Category 3"));
    CHECK(xtream::parseCategories("[]", cats, err) && cats.empty());
    CHECK(!xtream::parseCategories(R"({"user_info":{"auth":0}})", cats, err));
    CHECK(!err.empty());
    CHECK(!xtream::parseCategories("garbage", cats, err));
}

TEST(redaction_of_profile_credentials) {
    Profile p = profile();
    redact::addSecret(p.password);
    redact::addSecret("ab");  // too short to mask literally, must not mangle text
    std::string line = redact::apply("GET " + xtream::liveUrl(p, "5", "ts") + " password p@ss/word");
    CHECK(line.find("p@ss/word") == std::string::npos);
    CHECK(line.find("p%40ss%2Fword") == std::string::npos);  // encoded form is masked structurally
    CHECK(line.find("/live/[REDACTED]/[REDACTED]/5.ts") != std::string::npos);
    std::string api = redact::apply(xtream::apiUrl(p, "get_live_streams"));
    CHECK(api.find("user%20name") == std::string::npos);
    CHECK(api.find("password=[REDACTED]") != std::string::npos);
    CHECK(redact::apply("about tab") == "about tab");
}
