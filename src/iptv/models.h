// IPTV data model.

#ifndef PS4IPTV_IPTV_MODELS_H
#define PS4IPTV_IPTV_MODELS_H

#include <cstdint>
#include <string>
#include <vector>

namespace iptv {

    struct Profile {
        std::string id;
        std::string name;
        std::string server;     // normalized base URL, e.g. http://host:8080
        std::string username;
        std::string password;
        int64_t createdAt = 0;
        int64_t lastUsedAt = 0;
        std::string lastStatus; // short user-facing status of the last connection attempt
    };

    enum class ContentType {
        Live,
        Movie,
        Series
    };

    struct Category {
        std::string id;
        std::string name;
        std::string parentId;
    };

    enum class AuthStatus {
        Ok,
        InvalidCredentials,
        Expired,
        Banned,
        Disabled,
        Refused,          // HTTP 403 from the API
        ServerError,      // HTTP 5xx / unexpected HTTP status
        Malformed,        // not an Xtream response
        Network           // could not reach the server (set by the caller)
    };

    struct AccountInfo {
        std::string status;          // as reported: Active / Expired / Banned / Disabled / ...
        std::string message;
        int64_t expiresAt = 0;       // unix time, 0 = never / unknown
        bool trial = false;
        int maxConnections = 0;
        int activeConnections = 0;
        std::vector<std::string> outputFormats;
        // server_info
        std::string serverUrl;
        std::string serverPort;
        std::string serverHttpsPort;
        std::string serverProtocol;
        std::string timezone;
        int64_t serverTime = 0;
    };

    struct AuthResult {
        AuthStatus status = AuthStatus::Malformed;
        AccountInfo account;
        std::string detail;          // diagnostic detail for the log (never contains credentials)
    };

    const char *contentTypeName(ContentType type);
}

#endif // PS4IPTV_IPTV_MODELS_H
