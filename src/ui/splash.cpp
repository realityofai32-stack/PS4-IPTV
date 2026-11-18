#include <algorithm>
#include <cmath>

#include "splash.h"
#include "theme.h"

using namespace c2d;

namespace ui {

    MeshShape::MeshShape() : array(Triangles) {
    }

    void MeshShape::set(const brandart::Mesh &mesh, float opacity) {
        array.resize(mesh.size());
        std::vector<Vertex> &v = *array.getVertices();
        bool translucent = false;
        for (size_t i = 0; i < mesh.size(); i++) {
            const brandart::Vertex &m = mesh[i];
            auto a = (uint8_t) std::lround(std::max(0.0f, std::min(1.0f, opacity)) * m.c.a);
            translucent |= a < 255;
            v[i] = Vertex({m.x, m.y}, Color(m.c.r, m.c.g, m.c.b, a), {0, 0});
        }
        // libcross2d's GL path turns blending on for a whole array only when its FIRST vertex is translucent
        // (gl_renderer.cpp): one unit less on that vertex keeps every soft edge and glow blended
        if (translucent && !v.empty() && v[0].color.a == 255) {
            v[0].color.a = 254;
        }
        array.update();
    }

    void MeshShape::onDraw(Transform &transform, bool draw) {
        if (draw && array.getVertexCount() > 0) {
            Transform combined = transform * getTransform();
            c2d_renderer->draw(&array, combined, nullptr);
        }
        C2DObject::onDraw(transform, draw);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Splash::Splash(const palette::Accent &accent) : colors(brandart::colorsOf(accent)) {
        brandart::splash::background(background, colors);
        brandart::splash::logo(logo, colors);
        // drawing order of brandart::splash::frame: page, light, monogram, dots
        backgroundShape = new MeshShape();
        wavesShape = new MeshShape();
        logoShape = new MeshShape();
        dotsShape = new MeshShape();
        add(backgroundShape);
        add(wavesShape);
        add(logoShape);
        add(dotsShape);
    }

    void Splash::tick(double now) {
        if (gone) {
            return;
        }
        if (start < 0) {
            start = now;
        }
        float opacity = 1.0f;
        if (fadeFrom >= 0) {
            double u = fadeLength > 0 ? (now - fadeFrom) / fadeLength : 1.0;
            if (u >= 1.0) {
                gone = true;
                setVisibility(Visibility::Hidden);
                return;
            }
            u = std::max(0.0, u);
            opacity = (float) (1.0 - u * u * (3.0 - 2.0 * u));   // smoothstep
        }
        if (opacity != stillOpacity) {
            backgroundShape->set(background, opacity);
            logoShape->set(logo, opacity);
            stillOpacity = opacity;
        }
        const double t = now - start;
        brandart::Mesh m;
        brandart::splash::waves(m, colors, t);
        wavesShape->set(m, opacity);
        m.clear();
        brandart::splash::dots(m, colors, t);
        dotsShape->set(m, opacity);
    }

    void Splash::fadeOut(double now, double seconds) {
        if (fadeFrom < 0) {
            fadeFrom = now;
            fadeLength = seconds;
        }
    }
}
