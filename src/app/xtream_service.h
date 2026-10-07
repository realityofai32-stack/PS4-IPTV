// Asynchronous Xtream API operations on the job system. Callbacks run on the main thread.

#ifndef PS4IPTV_APP_XTREAM_SERVICE_H
#define PS4IPTV_APP_XTREAM_SERVICE_H

#include <functional>
#include <string>
#include <vector>

#include "../iptv/models.h"
#include "../network/jobs.h"

class XtreamService {

public:

    explicit XtreamService(JobSystem &jobs) : jobs(jobs) {}

    struct AuthOutcome {
        iptv::AuthResult result;
        std::string message;      // user-facing
        bool httpsWarning = false; // API reachable but streams would need HTTPS
    };

    using AuthCallback = std::function<void(const AuthOutcome &)>;

    CancelToken authenticate(const iptv::Profile &profile, AuthCallback callback);

    struct CategoriesOutcome {
        bool ok = false;
        std::vector<iptv::Category> categories;
        std::string message;      // user-facing error
    };

    using CategoriesCallback = std::function<void(const CategoriesOutcome &)>;

    CancelToken loadCategories(const iptv::Profile &profile, iptv::ContentType type, CategoriesCallback callback);

private:

    JobSystem &jobs;
};

#endif // PS4IPTV_APP_XTREAM_SERVICE_H
