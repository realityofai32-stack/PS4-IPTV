// Shared screen building blocks.

#ifndef PS4IPTV_SCREENS_COMMON_H
#define PS4IPTV_SCREENS_COMMON_H

#include <string>
#include <utility>
#include <vector>

#include "../app/app.h"
#include "../app/screens.h"
#include "../i18n/i18n.h"
#include "../ui/components.h"
#include "../ui/text.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

// every screen text comes from the localization tables (src/i18n): tr("key"), tr("key", {args})
using i18n::tr;

// hints and shortcuts name actions: {A::Confirm, tr("common.open")} (platform/input_actions.h)
using A = actions::Action;

namespace screens {

    // control hints name actions (platform/input_actions.h), drawn for the active device (DS4 glyphs / key caps)
    using Hints = std::vector<ui::Hint>;

    // page title (+ optional subtitle under it) at the top-left of a full screen, after the V7 symbol; returns
    // the title (headerContext places text after it)
    ui::Label *header(c2d::C2DObject *parent, const std::string &title, const std::string &subtitle = "");

    // the contextual part of a sub-page header, on the title's line and baseline: "All movies · 19,826" or
    // "0 B downloaded · 347 GB free". Call place() after changing its text.
    class HeaderContext {
    public:
        HeaderContext(c2d::C2DObject *parent, ui::Label *title);

        // lead: stronger part (the open category), rest: quieter part after it (counts, order)
        void set(const std::string &lead, const std::string &rest = "");

    private:
        ui::Label *title;
        ui::Label *lead;
        ui::Label *rest;
    };

    // a status on the header line, right-aligned at the safe edge ("Refreshing...", "Saved list")
    ui::Label *headerStatus(c2d::C2DObject *parent);

    // detail pages: a small V7 symbol and the section they belong to ("Movies"), above the artwork
    void breadcrumb(c2d::C2DObject *parent, const std::string &section);

    // scrolling pages: content slides under an eased fade and a solid band behind the controller hints
    void bottomFade(c2d::C2DObject *parent);

    // ---- series episode playback: the one setup every entry point uses (Series Detail, Continue Watching / the
    // Home hero, Downloads), so the player always has the same episode context (app/auto_next.h episodeQueue)

    // the series' catalog in memory (VodLibrary::seriesInfo), when it may describe this episode: it belongs to the
    // signed-in profile (a download of another profile has its own provider). nullptr otherwise.
    std::shared_ptr<const iptv::SeriesInfo> episodeCatalog(App &app, const VodItem &episode);

    // the player's queue for a series episode: the catalog order when the catalog is in memory (downloaded copies
    // play from the disk), else the series' downloads, else the episode alone. position: the episode's index.
    std::vector<VodItem> seriesEpisodeQueue(App &app, const VodItem &episode, int &position);

    // plays a series episode (resume: from its saved position) with that queue
    Screen *playSeriesEpisode(App &app, const VodItem &episode, bool resume,
                              std::function<void(const std::string &)> onExit = nullptr);

    // ---- detail pages' artwork (v7-ux-13): the clean dark page, the sharp poster, and only a small halo of the
    // poster's colour hugging `posterRect` (`tint`: a full-screen Backdrop without fades; images/hero_art.h
    // ambientAround, Home's showcase idea), never reaching the text column at `textX`. No page-wide layer: the
    // provider backdrop is not drawn (`backdrop` stays hidden), never a dimmed / stretched poster, nothing generated.
    struct DetailArt {
        ui::Backdrop *backdrop = nullptr;   // its box decides whether a backdrop can be drawn sharp (BACKDROP_MAX_SCALE)
        ui::Backdrop *tint = nullptr;
        c2d::FloatRect posterRect;
        float textX = 0;
    };

    void bindDetailArt(App &app, const DetailArt &art, const std::string &posterUrl, const std::string &backdropUrl);

    // a framed poster like Home's showcase: posterShadow() before adding the poster, posterFrame() after it
    void posterShadow(c2d::C2DObject *parent, const c2d::FloatRect &rect);

    void posterFrame(c2d::C2DObject *parent, const c2d::FloatRect &rect);

    ui::HintBar *hintBar(c2d::C2DObject *parent, const Hints &hints);

    // the key / button that performs an action on the active device, for sentences ("Press {0} to try again"):
    // "X", "Square", "OPTIONS" on the controller; "Enter", "F", "Del" on the keyboard
    std::string keyName(actions::Action action);

    // playback HUD shade over the video, added to `parent` (a HUD band of `height`, at its top or bottom edge):
    // `solid` px of the overlay at the screen edge (behind the text), then an eased fade into the picture. The
    // smoothstep fade is flat at both ends, so neither the join nor the end shows a line over bright video.
    void hudShade(c2d::C2DObject *parent, float height, float solid, bool top);


    std::string hostOf(const std::string &server);

    // "Xtream Codes" / "M3U / M3U8"
    std::string sourceTypeName(iptv::SourceType type);

    // where a source is, safe to show: the Xtream host, or the playlist's host and file name (never the
    // credentials / tokens a playlist URL may contain)
    std::string sourceLocation(const iptv::Profile &profile);

    std::string mask(const std::string &secret);

    // "Active - expires 21 Sep 2027 - 0/1 connections"
    std::string accountSummary(const iptv::AccountInfo &account);

    // video geometry values as shown to the user (Playback Options, Settings, technical info)
    std::string aspectName(display::Aspect aspect);

    std::string cropName(display::Crop crop);

    std::string zoomName(int percent);

    // "Centered", "Left 50%", "Down 25%"
    std::string positionName(int step, bool horizontal);

    // "Auto / Source  ·  None  ·  100%"
    std::string geometrySummary(const display::Geometry &g);

    // a row with a caption on the left and a value on the right, used by forms and settings
    class FieldRow : public c2d::RectangleShape {
    public:
        FieldRow(const c2d::FloatRect &rect, const std::string &caption);

        void setValue(const std::string &value, bool placeholder = false);

        void setCaption(const std::string &caption);

        void setFocused(bool focused);

    private:
        ui::Label *captionLabel;
        ui::Label *valueLabel;
    };
}

#endif // PS4IPTV_SCREENS_COMMON_H
