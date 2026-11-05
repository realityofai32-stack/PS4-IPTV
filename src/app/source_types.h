// Results of the source operations (sign-in, Live TV, Movies / Series catalogs, details) shared by every source
// type: Xtream (app/xtream_service) and Stalker / MAG (app/stalker_service) produce the same outcomes, so the
// screens and the VOD library work with either (app/source_service routes by the profile's source type).

#ifndef PS4IPTV_APP_SOURCE_TYPES_H
#define PS4IPTV_APP_SOURCE_TYPES_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../iptv/catalog.h"
#include "../iptv/models.h"

namespace sources {

    struct AuthOutcome {
        iptv::AuthResult result;
        std::string message;      // user-facing
        bool httpsWarning = false; // API reachable but streams need HTTPS, which this backend cannot play
    };

    using AuthCallback = std::function<void(const AuthOutcome &)>;

    struct CategoriesOutcome {
        bool ok = false;
        std::vector<iptv::Category> categories;
        std::string message;      // user-facing error
    };

    using CategoriesCallback = std::function<void(const CategoriesOutcome &)>;

    struct LiveOutcome {
        bool ok = false;
        bool fromCache = false;
        int64_t savedAt = 0;
        std::vector<iptv::Category> categories;
        std::vector<iptv::LiveChannel> channels;
        std::string message;       // user-facing error (or cache notice)
    };

    using LiveCallback = std::function<void(LiveOutcome &)>;

    enum class Source {
        Cache,      // the saved copy only (fast, offline)
        Network     // the provider; a good response replaces the saved copy
    };

    template<typename Catalog>
    struct CatalogOutcome {
        bool ok = false;
        int64_t savedAt = 0;
        std::shared_ptr<Catalog> catalog;   // parsed and indexed on the worker
        std::string message;               // user-facing error
    };

    using MoviesOutcome = CatalogOutcome<iptv::MovieCatalog>;
    using SeriesListOutcome = CatalogOutcome<iptv::SeriesCatalog>;

    template<typename Info>
    struct InfoOutcome {
        bool ok = false;
        Info info;
        std::string message;
    };

    // a playable address resolved at play time (Stalker: create_link)
    struct LinkOutcome {
        bool ok = false;
        std::string url;
        std::string userAgent;     // for the stream request ("" = the player default)
        std::string message;       // user-facing error
    };

    using LinkCallback = std::function<void(const LinkOutcome &)>;
}

#endif // PS4IPTV_APP_SOURCE_TYPES_H
