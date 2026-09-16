#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <cmath>

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

std::optional<CBox> CGlassPassElement::paddedLogicalBox() const {
    if (!m_data.decoration.valid())
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    // IPassElement::boundingBox() is a monitor-local LOGICAL coordinate
    // contract; computeWindowBox() returns physical pixels for renderPass()'s
    // own use, so convert back and expand by our sampling padding here.
    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    box->scale(1.0 / scale).expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize().round();
    if (!std::isfinite(box->x) || !std::isfinite(box->y) || !std::isfinite(box->w) || !std::isfinite(box->h) || box->w <= 0.0 || box->h <= 0.0)
        return std::nullopt;

    return box;
}

std::optional<CBox> CGlassPassElement::boundingBox() {
    return paddedLogicalBox();
}

bool CGlassPassElement::needsLiveBlur() {
    // Windows need live blur so the render pass fully re-renders the
    // background behind the glass before we sample it. Without this,
    // partial damage (e.g. typing in a window below) leaves stale pixels
    // in the padded sampling region, causing blinking artifacts.
    // Layers don't need this — they have their own blur cache with
    // scene generation tracking.
    //
    // Must agree with boundingBox() on whether a box exists: Hyprland's
    // CRenderPass::render() asserts a bounding box for any element reporting
    // live blur ("No bounding box for an element with live blur is illegal",
    // Pass.cpp) and aborts the compositor if it's absent.
    return paddedLogicalBox().has_value();
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    return m_data.decoration.valid() && m_data.decoration->getOwner();
}
