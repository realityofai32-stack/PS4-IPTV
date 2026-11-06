//
// Created by cpasjuste on 09/12/18.
//
// PS4 IPTV: adapted copy (via the hardware-validated playback test) of pPlay src/player/video_texture.cpp (Cpasjuste/pplay @ 0399546,
// GPL-3.0). The mpv render path in onDraw() is unchanged: same FBO (GLTextureBuffer, RGBA8, screen
// size), same mpv_render_context_update/mpv_render_context_render calls and parameters.
// Differences:
//   - constructed from the Mpv instance instead of pPlay's Main
//   - no fade texture (pPlay UI effect)
//   - the texture quad is only drawn once a frame of the current file has been rendered, so a
//     previous test's last frame is never shown; mpv rendering itself runs exactly as in pPlay
//   - the time spent in mpv_render_context_render is measured (diagnostics)
//

#include "video_texture.h"
#include "../../platform/clock.h"

using namespace c2d;

VideoTexture::VideoTexture(Mpv *m, const c2d::Vector2f &size) : GLTextureBuffer(size, Format::RGBA8) {
    mpv = m;
}

void VideoTexture::resetFrameStats() {
    framesRendered = 0;
    timing = RenderTiming();
}

void VideoTexture::onDraw(c2d::Transform &transform, bool draw) {
    if (mpv->getContext() == nullptr) {  // mpv init failed: nothing to render
        GLTextureBuffer::onDraw(transform, false);
        return;
    }
    bool update = mpv_render_context_update(mpv->getContext()) & MPV_RENDER_UPDATE_FRAME;
    if (draw && update) {
        int zero{0};
        mpv_opengl_fbo mpv_fbo{
                .fbo = (int) fbo,
                .w = (int) getSize().x, .h = (int) getSize().y,
                .internal_format = GL_RGBA8};
        mpv_render_param r_params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
                {MPV_RENDER_PARAM_FLIP_Y,     &zero},
                {MPV_RENDER_PARAM_INVALID,    nullptr}
        };

        double t0 = clockx::monotonic();
        mpv_render_context_render(mpv->getContext(), r_params);
        timing.add((clockx::monotonic() - t0) * 1000.0);
        framesRendered++;
    }

    GLTextureBuffer::onDraw(transform, draw && framesRendered > 0);
}
