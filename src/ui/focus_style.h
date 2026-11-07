// Focus states of every focusable control (host-testable, no libcross2d): exactly three looks.
//
//   focused now        the strongest treatment: the accent fill and / or the accent ring / edge. Only the control that
//                      has the focus gets it, so one thing on screen looks focused.
//   primary, at rest   the screen's main action while something else has the focus: still important, but restrained -
//                      the accent's dark tint (accentMuted) with strong text, no ring. Never the accent fill.
//   secondary, at rest neutral: an elevated surface (or nothing), secondary text.
// "Selected" (the open tab / category / season while the focus is elsewhere) is the same restrained level as primary
// at rest. Theme colours are unchanged; this only decides which existing token each state uses (ui/widgets.cpp
// Button, ui/components.cpp Chip / NavTab / IconButton / RowFrame).

#ifndef PS4IPTV_UI_FOCUS_STYLE_H
#define PS4IPTV_UI_FOCUS_STYLE_H

namespace ui {
    namespace focus {

        enum class Role {
            Primary,     // pill button: the screen's main action (Play / Resume / Save)
            Secondary,   // pill button: any other action
            Danger,      // pill button: a destructive action
            Chip,        // tabs, filters, seasons (rest: an elevated chip)
            NavTab,      // Home's navigation sections (rest: text only)
            Icon,        // round icon buttons (rest: the icon only)
            Row          // list rows (ui::RowFrame)
        };

        enum class Fill {
            None,
            Surface,
            SurfaceElevated,
            CardHover,
            Accent,          // the strongest: focused only
            AccentMuted,     // restrained: primary at rest, selected
            FocusTint,       // a focused row's accent-tinted surface (theme::focus)
            DangerSurface
        };

        enum class Text {
            Primary,
            Secondary,
            Strong,
            OnAccent,
            Muted
        };

        enum class Edge {
            None,
            Accent,          // focused: the ring / row edge
            Muted            // selected row: a quiet edge in the accent's dark tint
        };

        struct Look {
            Fill fill = Fill::None;
            Text text = Text::Primary;
            Edge edge = Edge::None;      // the focus ring (buttons) / the row's left edge
            bool strong = false;         // the focus treatment: true only for the focused control
        };

        inline Look look(Role role, bool focused, bool selected = false, bool enabled = true) {
            Look l;
            if (!enabled) {
                l.fill = role == Role::NavTab || role == Role::Icon || role == Role::Row ? Fill::None : Fill::Surface;
                l.text = Text::Muted;
                return l;
            }
            l.strong = focused;
            switch (role) {
                case Role::Primary:
                    l.fill = focused ? Fill::Accent : Fill::AccentMuted;
                    l.text = focused ? Text::OnAccent : Text::Strong;
                    l.edge = focused ? Edge::Accent : Edge::None;
                    break;
                case Role::Secondary:
                    l.fill = focused ? Fill::CardHover : Fill::SurfaceElevated;
                    l.text = focused ? Text::Strong : Text::Primary;
                    l.edge = focused ? Edge::Accent : Edge::None;
                    break;
                case Role::Danger:
                    l.fill = Fill::DangerSurface;
                    l.text = focused ? Text::Strong : Text::Primary;
                    l.edge = focused ? Edge::Accent : Edge::None;
                    break;
                case Role::Chip:
                    l.fill = focused ? Fill::Accent : selected ? Fill::AccentMuted : Fill::SurfaceElevated;
                    l.text = focused ? Text::OnAccent : selected ? Text::Strong : Text::Secondary;
                    break;
                case Role::NavTab:
                    l.fill = focused ? Fill::Accent : Fill::None;
                    l.text = focused ? Text::OnAccent : selected ? Text::Strong : Text::Secondary;
                    break;
                case Role::Icon:
                    l.fill = focused ? Fill::Accent : Fill::None;
                    l.text = focused ? Text::OnAccent : Text::Secondary;
                    break;
                case Role::Row:
                    l.fill = focused ? Fill::FocusTint : Fill::None;
                    l.text = focused ? Text::Strong : Text::Primary;
                    l.edge = focused ? Edge::Accent : selected ? Edge::Muted : Edge::None;
                    break;
            }
            return l;
        }
    }
}

#endif // PS4IPTV_UI_FOCUS_STYLE_H
