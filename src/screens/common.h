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

namespace screens {

    using Hints = std::vector<std::pair<ui::Glyph, std::string>>;

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

    // poster-derived backdrop: the large poster when it is ready, else the grid poster, else nothing (a glow)
    void bindBackdrop(App &app, ui::Backdrop *backdrop, const std::string &posterUrl);

    ui::HintBar *hintBar(c2d::C2DObject *parent, const Hints &hints);

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
