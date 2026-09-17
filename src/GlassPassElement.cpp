#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

CGlassPassElement::CGlassPassElement(const SGlassPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassPassElement::draw() {
    if (!m_data.decoration.valid())
        return {};

    // Hyprland renders a floating window over fullscreen more than once per
    // frame; every copy but the last queued is a no-op, so the glass is applied
    // exactly once and samples the framebuffer as it is under the last copy.
    if (!m_data.decoration->isCurrentGlassPass(m_data.frameSerial, m_data.queueIndex))
        return {};

    m_data.decoration->renderPass(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassPassElement::boundingBox() {
    if (!m_data.decoration.valid())
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    // Expand by the sampling margin so the pass damages everything we read from.
    // Hyprland scales boundingBox() by the monitor scale itself, so hand it
    // logical units; the margin is framebuffer pixels, hence / scale.
    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    box->scale(1.0 / scale).expand(GlassRenderer::SAMPLE_PADDING_PX / scale);
    return box;
}

bool CGlassPassElement::needsLiveBlur() {
    // Windows need live blur so the render pass fully re-renders the
    // background behind the glass before we sample it. Without this,
    // partial damage (e.g. typing in a window below) leaves stale pixels
    // in the padded sampling region, causing blinking artifacts.
    // Layers don't need this — they have their own blur cache with
    // scene generation tracking.
    return m_data.decoration.valid() && m_data.decoration->getOwner();
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    return m_data.decoration.valid() && m_data.decoration->getOwner();
}
