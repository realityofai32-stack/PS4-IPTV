// V7 IPTV design-system components, built on libcross2d shapes and ui::Label like ui/widgets.h:
//   BrandMark / Brand   the V7 symbol (vector, any size) and the horizontal "symbol + name" lockup
//   Chip                tabs, filters and season chips (rest / selected / focused)
//   ProgressBar         thin rounded track + accent fill
//   IconButton          round navigation button with a vector icon (search, favorites, downloads, settings)
//   TopNav              Home's navigation bar: brand, primary sections, icon actions, clock
//   EmptyState          short title + one-line explanation (+ optional action hint), centred or left-aligned
//   Backdrop            ambient artwork: an enlarged, darkened poster with eased fades into the background
//   fade()              an eased (smoothstep) gradient: no visible band where it starts or ends
//   RowFrame            a list row on the background: divider at rest, surface + accent edge when focused
//
// Texts are passed in already localized (the ui layer never looks up strings). Colours come from theme::.

#ifndef PS4IPTV_UI_COMPONENTS_H
#define PS4IPTV_UI_COMPONENTS_H

#include <memory>
#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "text.h"
#include "theme.h"

namespace ui {

    // The V7 symbol: a V whose right arm is the stem of a 7, closed by the 7's top bar. Three convex polygons on a
    // 100 x 80 grid (see docs/v7-ui-refresh.md); `height` is the drawn height, the width is height * 1.25.
    // Two tones of the active accent (the light arm in the accent's text tone), or fixed colours for the icon.
    class BrandMark : public c2d::RectangleShape {
    public:
        static constexpr float ASPECT = 1.25f;   // width / height

        explicit BrandMark(float height);

        void setColors(const c2d::Color &arm, const c2d::Color &stem);
    private:
        c2d::ConvexShape *left;
        c2d::ConvexShape *right;
        c2d::ConvexShape *bar;
    };

    // [symbol] V7 IPTV: the symbol and the product name on one baseline
    class Brand : public c2d::RectangleShape {
    public:
        Brand(const std::string &name, float markHeight, unsigned textSize);
    };

    class Chip : public c2d::RectangleShape {
    public:
        // width 0 = fit the text (+ padding)
        Chip(const std::string &text, float x, float y, float width = 0, float height = theme::CHIP_H);

        void setText(const std::string &text);

        // selected: the current tab / season / filter; focused: the controller is on it
        void setState(bool selected, bool focused);

        float width() const { return getSize().x; }

    private:
        void layout();

        Label *caption;
        float fixedWidth;
        bool selected = false;
        bool focused = false;
    };

    class ProgressBar : public c2d::RectangleShape {
    public:
        // onImage: a darker track that stays visible over artwork
        ProgressBar(const c2d::FloatRect &rect, bool onImage = false);

        // 0..1; values above 0 always show a sliver (the item has been started)
        void setFraction(float fraction);

    private:
        c2d::RectangleShape *fill;
    };

    enum class Icon {
        Search,
        Favorites,
        Downloads,
        Settings
    };

    class IconButton : public c2d::RectangleShape {
    public:
        IconButton(Icon icon, float size);

        void setFocused(bool focused);

        // small accent badge with a number (0 hides it), e.g. downloads in progress
        void setBadge(int count);

    private:
        void addPart(c2d::Shape *shape, bool outline);

        std::vector<std::pair<c2d::Shape *, bool>> parts;   // shape, drawn as an outline
        c2d::CircleShape *badge;
        Label *badgeText;
        bool focused = false;
    };

    // Home's navigation: [brand]  Home  Live TV  Movies  Series ............ (search)(favorites)(downloads)(settings)  22:14
    // Sections are text tabs (the current one strong with an accent underline, the focused one an accent pill);
    // the secondary actions are small transparent round buttons. Nothing is boxed at rest.
    class NavTab;

    class TopNav : public c2d::RectangleShape {
    public:
        TopNav(const std::string &brandName, const std::vector<std::string> &sections, const std::vector<Icon> &icons);

        int itemCount() const { return (int) (pills.size() + buttons.size()); }

        int sectionCount() const { return (int) pills.size(); }

        // index < sectionCount(): a section, otherwise an icon; -1 = nothing focused
        void setFocus(int index);

        // the section the user is in (drawn as selected while the focus is elsewhere)
        void setCurrent(int section);

        void setClock(const std::string &text);

        void setBadge(int icon, int count);

        // the focused icon's name, shown under it ("Search")
        void setIconCaption(const std::string &text);

        const std::string &clockText() const;

    private:
        std::vector<NavTab *> pills;
        std::vector<IconButton *> buttons;
        Label *clock;
        Label *iconCaption;
        int focus = -1;
        int current = 0;
    };

    // The edge of a fade that carries the full colour; the alpha eases to 0 towards the opposite edge.
    enum class Edge {
        Top,
        Bottom,
        Left,
        Right
    };

    // Four gradient segments following smoothstep: the slope is flat at both ends, so the fade has no visible
    // start or end line over artwork or video (a single linear ramp shows a band at both ends on the TV).
    c2d::RectangleShape *fade(c2d::C2DObject *parent, const c2d::FloatRect &rect, const c2d::Color &color,
                              Edge solid);

    // A list row drawn on the background instead of a filled slab. Rest: a 1 px divider at the bottom (optional).
    // Selected (the open item of a list that does not have the focus): a 4 px accent edge. Focused: the
    // accent-tinted surface and the edge. Children (texts, tiles) are added by the owner.
    class RowFrame : public c2d::RectangleShape {
    public:
        // dividerInset: where the divider starts (it ends at the row's right edge); < 0 = no divider
        RowFrame(float width, float height, float dividerInset = 0);

        void setState(bool focused, bool selected = false);

        // rows that hold their own focusable cells (the accent palette) draw no divider
        void setDivider(bool shown);

    private:
        c2d::RectangleShape *edge;
        c2d::RectangleShape *line = nullptr;
        bool divider = true;
    };

    class EmptyState : public c2d::RectangleShape {
    public:
        // leftAligned: texts start at the box's left edge and the block sits at its top (search instructions)
        explicit EmptyState(const c2d::FloatRect &rect, bool leftAligned = false);

        // an empty title hides the whole state
        void set(const std::string &title, const std::string &message, const std::string &action = "");

    private:
        Label *title;
        Label *message;
        Label *action;
        bool left;
    };

    // Ambient artwork behind a hero or a detail page: one full-bleed layer, never a framed picture. The poster is
    // cover-cropped to the box (aspect kept, anchored above the centre where faces usually are), dimmed by texture
    // modulation, shaded, then faded into the background on every edge that is not a screen edge, with eased
    // gradients (no band where a fade starts or ends). Bilinear upscaling of the poster texture gives a soft,
    // blur-like wash without any blur pass. Without an image only a faint accent glow is drawn.
    class Backdrop : public c2d::RectangleShape {
    public:
        // fadeLeft / fadeBottom / fadeTop: width / height of the eased fades into the background colour
        Backdrop(const c2d::FloatRect &rect, float fadeLeft, float fadeBottom, float fadeTop = 0);

        void set(const std::shared_ptr<c2d::Texture> &texture, const c2d::Vector2i &imageSize);

        // which part of a taller image is kept: 0 = its top .. 1 = its bottom (default 0.3: above the centre)
        void setAnchor(float anchorY);

    private:
        c2d::RectangleShape *image;
        c2d::RectangleShape *shade;
        float anchor = 0.3f;
        bool recrop = false;
        c2d::GradientRectangle *glow;
        std::shared_ptr<c2d::Texture> shown;
    };
}

#endif // PS4IPTV_UI_COMPONENTS_H
