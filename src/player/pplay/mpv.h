//
// Created by cpasjuste on 02/04/19.
//
// PS4 IPTV: copy (via the hardware-validated playback test, tests/playback-test/src/pplay) of of pPlay src/player/mpv.h (Cpasjuste/pplay @ 0399546, GPL-3.0).
// Changes: diagnostics only (init error reporting). See scripts/diff-pplay-playback.ps1.
//

#ifndef PPLAY_MPV_H
#define PPLAY_MPV_H

#include <cstdio>
#include <string>

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include "media_info.h"

class Mpv {

public:

    enum class LoadType {
        Replace,
        Append,
        AppendPlay
    };

    explicit Mpv(const std::string &configPath, bool initRender);

    ~Mpv();

    int load(const std::string &file, LoadType loadType, const std::string &options);

    int save();

    int pause();

    int resume();

    int stop();

    int seek(double position);

    int setSpeed(double speed);

    double getSpeed();

    int setVid(int id);

    int getVid();

    int setAid(int id);

    int getAid();

    int setSid(int id);

    int getSid();

    int getVideoBitrate();

    int getAudioBitrate();

    long getDuration();

    long getPosition();

    bool isStopped();

    bool isPaused();

    bool isAvailable();

    mpv_event *getEvent();

    mpv_handle *getHandle();

    mpv_render_context *getContext();

    MediaInfo getMediaInfo(const c2d::Io::File &file);

    // diagnostics: which init step failed and the mpv error code it returned (0 = success)
    const std::string &getInitErrorStep() const { return initErrorStep; }

    int getInitError() const { return initError; }

private:

    mpv_handle *handle = nullptr;
    mpv_render_context *context = nullptr;
    std::string initErrorStep;
    int initError = 0;
};

#endif //PPLAY_MPV_H
