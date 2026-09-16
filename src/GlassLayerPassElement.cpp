#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

CGlassLayerPassElement::CGlassLayerPassElement(const SGlassLayerPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassLayerPassElement::draw() {
    if (m_data.layerState && m_data.layerState->getLayerSurface())
        m_data.layerState->sampleAndRedirect(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassLayerPassElement::paddedLogicalBox() const {
    if (!m_data.layerState)
        return std::nullopt;

    auto layerSurface = m_data.layerState->getLayerSurface();
    if (!layerSurface)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    return LayerGeometry::computePaddedLogicalLayerBox(layerSurface, monitor, GlassRenderer::SAMPLE_PADDING_PX);
}

std::optional<CBox> CGlassLayerPassElement::boundingBox() {
    return paddedLogicalBox();
}

bool CGlassLayerPassElement::needsLiveBlur() {
    // Ensure the render pass fully re-renders the background behind this
    // element before we sample it. Without this, partial damage causes the
    // glass to sample a mix of fresh wallpaper and its own stale output.
    // Per-monitor sceneGeneration prevents non-focused monitors from
    // re-sampling, so the continuous damage cost is limited.
    //
    // Must agree with boundingBox() on whether a box exists: Hyprland's
    // CRenderPass::render() asserts a bounding box for any element reporting
    // live blur ("No bounding box for an element with live blur is illegal",
    // Pass.cpp) and aborts the compositor if it's absent.
    return paddedLogicalBox().has_value();
}

bool CGlassLayerPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassLayerPassElement::disableSimplification() {
    return m_data.layerState && m_data.layerState->getLayerSurface();
}
