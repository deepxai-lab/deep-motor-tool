#pragma once
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <cmath>

namespace studio::ui {
inline QColor defaultBackground() { return QColor("#0b1019"); }

inline QColor normalizedBackground(const QColor& requested) {
    const QColor color = requested.isValid() ? requested.toRgb() : defaultBackground();
    // 背景始终使用用户选定的不透明颜色，不压暗、不增加覆盖层。
    return QColor(color.red(), color.green(), color.blue());
}

inline QColor savedBackground() {
    return normalizedBackground(QColor(QSettings().value(
        "ui/backgroundColor", defaultBackground().name()).toString()));
}

inline QColor blendColor(const QColor& first, const QColor& second, double amount) {
    return QColor(qRound(first.red() * (1 - amount) + second.red() * amount),
                  qRound(first.green() * (1 - amount) + second.green() * amount),
                  qRound(first.blue() * (1 - amount) + second.blue() * amount));
}

inline double luminance(const QColor& color) {
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
}

inline double contrastRatio(const QColor& first, const QColor& second) {
    const double a = luminance(first), b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

inline QColor readableColor(const QColor& preferred, const QColor& surface, double ratio = 4.5) {
    const QColor endpoint = contrastRatio(Qt::white, surface) >= contrastRatio(Qt::black, surface)
                                ? QColor(Qt::white) : QColor(Qt::black);
    for (int step = 0; step <= 100; ++step) {
        const QColor candidate = blendColor(preferred, endpoint, step / 100.0);
        if (contrastRatio(candidate, surface) >= ratio)
            return candidate;
    }
    return endpoint;
}

// 各区域使用独立的语义颜色，主题切换时背景、文字、状态和曲线一起更新。
struct ThemeColors {
    QColor window, sidebar, card, input, plot, button, hover, pressed, border;
    QColor text, muted, disabled, accent, accentFill, onAccent, selection, onSelection;
    QColor success, successSurface, warning, warningSurface, danger, dangerSurface;
    QColor activeSurface, grid, actual, target;
    bool light = false;

    explicit ThemeColors(const QColor& selected = savedBackground()) {
        window = normalizedBackground(selected);
        light = luminance(window) > 0.18;
        const QColor white(Qt::white), black(Qt::black);
        const bool deep = luminance(window) < 0.08;
        card = blendColor(window, light || deep ? white : black, light ? 0.82 : deep ? 0.045 : 0.16);
        sidebar = blendColor(window, light ? white : black, light ? 0.65 : 0.1);
        input = blendColor(window, light ? white : black, light ? 0.92 : 0.2);
        plot = input;
        const QColor preferredText(light ? "#172b40" : "#e4edf7");
        text = readableColor(preferredText, window);
        text = readableColor(text, card);
        muted = readableColor(light ? QColor("#52667c") : QColor("#9cb0c7"), window);
        muted = readableColor(muted, card);
        disabled = blendColor(card, text, 0.48);
        border = blendColor(card, text, light ? 0.27 : 0.23);
        button = blendColor(card, text, light ? 0.045 : 0.06);
        hover = blendColor(card, text, light ? 0.09 : 0.12);
        pressed = blendColor(card, text, 0.16);
        accent = readableColor(light ? QColor("#006c79") : QColor("#65e6cf"), card);
        accent = readableColor(accent, window);
        accentFill = light ? QColor("#087e8b") : QColor("#32cbb1");
        onAccent = readableColor(light ? white : QColor("#071b1b"), accentFill);
        selection = light ? QColor("#176c81") : QColor("#237d76");
        onSelection = readableColor(white, selection);
        successSurface = blendColor(card, light ? QColor("#b7e9da") : QColor("#0f4940"), 0.75);
        warningSurface = blendColor(card, light ? QColor("#fff0c2") : QColor("#443418"), 0.8);
        dangerSurface = blendColor(card, light ? QColor("#ffe0e3") : QColor("#491f2c"), 0.8);
        success = readableColor(light ? QColor("#00684e") : QColor("#72e6ca"), successSurface);
        warning = readableColor(light ? QColor("#795000") : QColor("#f1ce7f"), warningSurface);
        danger = readableColor(light ? QColor("#a42639") : QColor("#ffadb5"), dangerSurface);
        activeSurface = blendColor(card, light ? QColor("#b9e8e6") : QColor("#164f48"), 0.7);
        grid = blendColor(plot, text, 0.17);
        actual = readableColor(light ? QColor("#008066") : QColor("#49dbbd"), plot, 3.0);
        target = readableColor(light ? QColor("#365bc7") : QColor("#8babff"), plot, 3.0);
    }
};

inline QPalette themePalette(const QColor& background = defaultBackground()) {
    const ThemeColors c(background);
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.input);
    palette.setColor(QPalette::AlternateBase, c.card);
    palette.setColor(QPalette::ToolTipBase, c.card);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.button);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.text);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.onSelection);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabled);
    return palette;
}
} // 命名空间 studio::ui
