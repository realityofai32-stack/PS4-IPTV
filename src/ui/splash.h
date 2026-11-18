// The loading screen (V7 Player): ui/brand_art.h's artwork - dark page, the V7 monogram, flowing light, loading
// dots - drawn with the accent of Settings > Appearance (V7 Blue until one is chosen). No ring around the monogram,
// no text, no images.
//
// App shows it from the moment the settings are read (App::App) until its first screen exists, then fades it out
// over that screen (App::run). It never makes startup wait: there is no minimum time, a fast start shows it for a
// moment only. While startup runs, App draws a frame between startup stages (at most 30 per second); every motion
// is a function of the time since the screen appeared, so frames drawn at uneven intervals stay coherent.
// Its first frame is the same picture as the package's start image (sce_sys/pic1.png, V7 Blue): only the accent
// colour changes when the app takes over from the system.

#ifndef PS4IPTV_UI_SPLASH_H
#define PS4IPTV_UI_SPLASH_H

#include "cross2d/c2d.h"
#include "brand_art.h"

namespace ui {

    // coloured triangles (brandart::Mesh) drawn as one vertex array, no texture
    class MeshShape : public c2d::Transformable {
    public:
        MeshShape();

        // replaces the triangles; `opacity` scales every vertex's alpha
        void set(const brandart::Mesh &mesh, float opacity = 1.0f);

    protected:
        void onDraw(c2d::Transform &transform, bool draw) override;

    private:
        c2d::VertexArray array;
    };

    class Splash : public c2d::Transformable {
    public:
        explicit Splash(const palette::Accent &accent);

        // the picture at `now` (seconds, App's clock); the first call is the animation's t = 0
        void tick(double now);

        // fades out from `now` over `seconds`; finished() once it is gone
        void fadeOut(double now, double seconds);

        bool fading() const { return fadeFrom >= 0; }

        bool finished() const { return gone; }

    private:
        brandart::Colors colors;
        brandart::Mesh background, logo;   // built once
        MeshShape *backgroundShape, *wavesShape, *logoShape, *dotsShape;
        double start = -1;
        double fadeFrom = -1;
        double fadeLength = 0;
        float stillOpacity = -1;           // opacity the static meshes were last uploaded with
        bool gone = false;
    };
}

#endif // PS4IPTV_UI_SPLASH_H
