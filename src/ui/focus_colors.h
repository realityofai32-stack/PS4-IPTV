// ui/focus_style.h states -> the existing theme tokens (no new colours).

#ifndef PS4IPTV_UI_FOCUS_COLORS_H
#define PS4IPTV_UI_FOCUS_COLORS_H

#include "focus_style.h"
#include "theme.h"

namespace ui {
    namespace focus {

        inline c2d::Color color(Fill f) {
            switch (f) {
                case Fill::Surface:
                    return theme::surface();
                case Fill::SurfaceElevated:
                    return theme::surfaceElevated();
                case Fill::CardHover:
                    return theme::cardHover();
                case Fill::Accent:
                    return theme::accent();
                case Fill::AccentMuted:
                    return theme::accentMuted();
                case Fill::FocusTint:
                    return theme::focus();
                case Fill::DangerSurface:
                    return theme::dangerSurface();
                default:
                    return theme::none();
            }
        }

        inline c2d::Color color(Text t) {
            switch (t) {
                case Text::Secondary:
                    return theme::textSecondary();
                case Text::Strong:
                    return theme::textStrong();
                case Text::OnAccent:
                    return theme::onAccent();
                case Text::Muted:
                    return theme::textMuted();
                default:
                    return theme::textPrimary();
            }
        }

        inline c2d::Color color(Edge e) {
            return e == Edge::Accent ? theme::accent() : e == Edge::Muted ? theme::accentMuted() : theme::none();
        }
    }
}

#endif // PS4IPTV_UI_FOCUS_COLORS_H
