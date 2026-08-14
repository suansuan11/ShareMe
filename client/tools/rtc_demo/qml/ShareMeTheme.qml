import QtQuick

QtObject {
    readonly property color background: "#0B1016"
    readonly property color surface: "#121922"
    readonly property color surfaceRaised: "#1A2430"
    readonly property color surfaceHover: "#23303E"
    readonly property color surfacePressed: "#293847"
    readonly property color surfaceDisabled: "#18212B"
    readonly property color border: "#2A3542"
    readonly property color borderStrong: "#3A4A5B"
    readonly property color textPrimary: "#EDF2F7"
    readonly property color textSecondary: "#A8B4C0"
    readonly property color textMuted: "#71808F"
    readonly property color textDisabled: "#5D6A77"
    readonly property color accent: "#6EA8E8"
    readonly property color accentHover: "#83B8EF"
    readonly property color accentPressed: "#5B95D4"
    readonly property color accentSubtle: "#1D3045"
    readonly property color success: "#72D5B0"
    readonly property color successSurface: "#19352D"
    readonly property color warning: "#E6C477"
    readonly property color warningSurface: "#382F1D"
    readonly property color error: "#E07A86"
    readonly property color errorSurface: "#3A222A"
    readonly property color focus: "#A9D2FF"
    readonly property color scrim: "#B30B1016"

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacingXxl: 32
    readonly property int spacingHuge: 48

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14

    readonly property int fontDisplay: 28
    readonly property int fontPageTitle: 22
    readonly property int fontSectionTitle: 16
    readonly property int fontBody: 14
    readonly property int fontLabel: 13
    readonly property int fontMeta: 12
    readonly property int fontCaption: 11
    readonly property int fontButton: 14
    readonly property real lineHeightBody: 1.4

    readonly property int controlHeight: 44
    readonly property int drawerWidth: 320
    readonly property int motionFast: 160

    // Keep existing component bindings source-compatible while they migrate to
    // the semantic names above.
    readonly property color primary: accent
    readonly property color primaryHover: accentHover
    readonly property color cyan: accent
    readonly property color healthy: success
    readonly property color danger: error
}
