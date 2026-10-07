//
// Created by cpasjuste on 09/12/18.
//
// PS4 IPTV: adapted copy (via the hardware-validated playback test) of pPlay src/player/video_texture.h (Cpasjuste/pplay @ 0399546,
// GPL-3.0). Changes: takes the Mpv instance directly instead of pPlay's Main, drops the pPlay fade
// skin texture, and counts rendered frames for diagnostics. See video_texture.cpp.
//

#ifndef PPLAY_VIDEO_TEXTURE_H
#define PPLAY_VIDEO_TEXTURE_H

#include "cross2d/c2d.h"
#include "mpv.h"

class VideoTexture : public c2d::GLTextureBuffer {

public:

    explicit VideoTexture(Mpv *mpv, const c2d::Vector2f &size);

    // diagnostics
    void resetFrameStats();

    unsigned long getFramesRendered() const { return framesRendered; }

private:

    void onDraw(c2d::Transform &transform, bool draw = true) override;

    Mpv *mpv;
    unsigned long framesRendered = 0;
};

#endif //PPLAY_VIDEO_TEXTURE_H
