#include <type_traits>

#include "vod_library.h"
#include "../platform/clock.h"
#include "../i18n/i18n.h"
#include "../platform/log.h"

using namespace iptv;

VodLibrary::VodLibrary(XtreamService &x, std::string dataDir) : xtream(x), dir(std::move(dataDir)) {}

void VodLibrary::reset() {
    (*epoch)++;
    movieCatalog = std::make_shared<MovieCatalog>();
    seriesCatalog = std::make_shared<SeriesCatalog>();
    movieState = SectionStatus();
    seriesState = SectionStatus();
    movieInfos.clear();
    seriesInfos.clear();
    inFlight.clear();
    errors.clear();
    gen++;
}

void VodLibrary::setOffline(bool offline) {
    offlineMode = offline;
    gen++;
}

void VodLibrary::openMovies(const Profile &profile) {
    open<MovieCatalog>(profile, true);
}

void VodLibrary::openSeries(const Profile &profile) {
    open<SeriesCatalog>(profile, false);
}

void VodLibrary::refreshMovies(const Profile &profile) {
    refresh(profile, true);
}

void VodLibrary::refreshSeries(const Profile &profile) {
    refresh(profile, false);
}

template<typename Catalog>
void VodLibrary::open(const Profile &profile, bool movies) {
    if (profile.isPlaylist()) {
        return;   // playlist sources have no Movies / Series (never guessed from channel names)
    }
    SectionStatus &st = movies ? movieState : seriesState;
    if (st.status != CatalogStatus::NotLoaded && st.status != CatalogStatus::Failed) {
        return;   // loaded or loading: nothing to do
    }
    st.status = CatalogStatus::Loading;
    st.message.clear();
    gen++;
    int myEpoch = *epoch;
    std::weak_ptr<int> alive = epoch;
    Profile p = profile;
    auto done = [this, alive, myEpoch, movies, p](XtreamService::CatalogOutcome<Catalog> &o) {
        auto e = alive.lock();
        if (!e || *e != myEpoch) {
            return;   // signed in again meanwhile
        }
        SectionStatus &s = movies ? movieState : seriesState;
        if (o.ok) {
            store(o.catalog);
            s.status = CatalogStatus::Ready;
            s.fromCache = true;
            s.savedAt = o.savedAt;
            gen++;
            if (clockx::unixNow() - o.savedAt > REFRESH_AFTER && !offlineMode) {
                refresh(p, movies);   // show the saved list now, newer one in the background
            }
        } else if (offlineMode) {
            s.status = CatalogStatus::Failed;
            s.message = i18n::tr("catalog.offline_none");
            gen++;
        } else {
            refresh(p, movies);       // no saved copy: the provider is the only source
        }
    };
    if constexpr (std::is_same<Catalog, MovieCatalog>::value) {
        xtream.loadMovies(profile, dir, XtreamService::Source::Cache, done);
    } else {
        xtream.loadSeriesList(profile, dir, XtreamService::Source::Cache, done);
    }
}

void VodLibrary::refresh(const Profile &profile, bool movies) {
    if (profile.isPlaylist()) {
        return;
    }
    SectionStatus &st = movies ? movieState : seriesState;
    if (st.refreshing) {
        return;
    }
    if (offlineMode) {
        bool hasList = movies ? !movieCatalog->empty() : !seriesCatalog->empty();
        st.refreshes++;
        st.lastRefreshOk = false;
        st.lastRefreshError = i18n::tr("connect.offline_mode");
        st.status = hasList ? CatalogStatus::Ready : CatalogStatus::Failed;
        st.message = hasList ? "" : i18n::tr("catalog.offline_none");
        gen++;
        return;
    }
    bool hasData = movies ? !movieCatalog->empty() : !seriesCatalog->empty();
    st.refreshing = hasData;
    if (!hasData) {
        st.status = CatalogStatus::Loading;
    }
    gen++;
    int myEpoch = *epoch;
    std::weak_ptr<int> alive = epoch;
    auto apply = [this, alive, myEpoch, movies](bool ok, const std::string &message, int64_t savedAt) {
        auto e = alive.lock();
        if (!e || *e != myEpoch) {
            return false;
        }
        SectionStatus &s = movies ? movieState : seriesState;
        s.refreshing = false;
        s.refreshes++;
        s.lastRefreshOk = ok;
        s.lastRefreshError = ok ? "" : message;
        bool hasList = movies ? !movieCatalog->empty() : !seriesCatalog->empty();
        if (ok) {
            s.status = CatalogStatus::Ready;
            s.fromCache = false;
            s.savedAt = savedAt;
            s.message.clear();
        } else if (hasList) {
            s.status = CatalogStatus::Ready;
            s.message = i18n::tr("catalog.refresh_failed_saved", {message});
        } else {
            s.status = CatalogStatus::Failed;
            s.message = message;
        }
        gen++;
        return ok;
    };
    if (movies) {
        xtream.loadMovies(profile, dir, XtreamService::Source::Network,
                          [this, apply](XtreamService::MoviesOutcome &o) {
                              if (o.ok && apply(true, "", o.savedAt)) {
                                  movieCatalog = o.catalog;
                              } else if (!o.ok) {
                                  apply(false, o.message, 0);
                              }
                          });
    } else {
        xtream.loadSeriesList(profile, dir, XtreamService::Source::Network,
                              [this, apply](XtreamService::SeriesListOutcome &o) {
                                  if (o.ok && apply(true, "", o.savedAt)) {
                                      seriesCatalog = o.catalog;
                                  } else if (!o.ok) {
                                      apply(false, o.message, 0);
                                  }
                              });
    }
}

void VodLibrary::requestMovieInfo(const Profile &profile, const std::string &streamId) {
    std::string key = "m" + streamId;
    if (streamId.empty() || profile.isPlaylist() || movieInfos.contains(streamId) || inFlight.count(key)) {
        return;
    }
    if (offlineMode) {
        const std::string &why = i18n::tr("connect.offline_mode");   // only what was cached before is shown
        if (errors[key] != why) {
            errors[key] = why;
            gen++;
        }
        return;
    }
    inFlight.insert(key);
    errors.erase(key);
    int myEpoch = *epoch;
    std::weak_ptr<int> alive = epoch;
    xtream.loadMovieInfo(profile, streamId, [this, alive, myEpoch, key, streamId](
            XtreamService::InfoOutcome<MovieInfo> &o) {
        auto e = alive.lock();
        if (!e || *e != myEpoch) {
            return;
        }
        inFlight.erase(key);
        if (o.ok) {
            auto info = std::make_shared<MovieInfo>(std::move(o.info));
            int64_t bytes = (int64_t) (sizeof(MovieInfo) + info->plot.size() + info->cast.size());
            movieInfos.put(streamId, info, bytes);
        } else {
            errors[key] = o.message;
        }
        gen++;
    });
}

std::shared_ptr<const MovieInfo> VodLibrary::movieInfo(const std::string &streamId) {
    auto *v = movieInfos.get(streamId);
    return v ? *v : nullptr;
}

void VodLibrary::requestSeriesInfo(const Profile &profile, const std::string &seriesId, bool force) {
    std::string key = "s" + seriesId;
    if (seriesId.empty() || profile.isPlaylist() || inFlight.count(key) || (!force && seriesInfos.contains(seriesId))) {
        return;
    }
    if (offlineMode) {
        const std::string &why = i18n::tr("connect.offline_mode");   // only what was cached before is shown
        if (errors[key] != why) {
            errors[key] = why;
            gen++;
        }
        return;
    }
    inFlight.insert(key);
    errors.erase(key);
    int myEpoch = *epoch;
    std::weak_ptr<int> alive = epoch;
    xtream.loadSeriesInfo(profile, seriesId, [this, alive, myEpoch, key, seriesId](
            XtreamService::InfoOutcome<SeriesInfo> &o) {
        auto e = alive.lock();
        if (!e || *e != myEpoch) {
            return;
        }
        inFlight.erase(key);
        if (o.ok) {
            auto info = std::make_shared<SeriesInfo>(std::move(o.info));
            int64_t bytes = sizeof(SeriesInfo);
            for (const auto &s: info->seasons) {
                bytes += (int64_t) (s.episodes.size() * (sizeof(Episode) + 160));
            }
            seriesInfos.put(seriesId, info, bytes);
        } else {
            errors[key] = o.message;
        }
        gen++;
    });
}

std::shared_ptr<const SeriesInfo> VodLibrary::seriesInfo(const std::string &seriesId) {
    auto *v = seriesInfos.get(seriesId);
    return v ? *v : nullptr;
}

std::string VodLibrary::detailError(const std::string &key) const {
    auto it = errors.find(key);
    return it == errors.end() ? "" : it->second;
}
