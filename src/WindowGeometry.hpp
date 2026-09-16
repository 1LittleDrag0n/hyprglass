#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <optional>

namespace WindowGeometry {

[[nodiscard]] inline std::optional<CBox> computeWindowBox(PHLWINDOW window, PHLMONITOR monitor) {
    if (!window || !monitor)
        return std::nullopt;

    const auto workspace = window->m_workspace;
    const auto workspaceOffset = workspace && !window->m_pinned
        ? workspace->m_renderOffset->value()
        : Vector2D();

    auto box = window->getWindowMainSurfaceBox();
    box.translate(workspaceOffset);
    box.translate(-monitor->m_position + window->m_floatingOffset);
    box.scale(monitor->m_scale);
    box.round();
    return box;
}

// The monitor transform CGlassDecoration::renderPass() applies to its own
// transformBox, factored out so CGlassPassElement::needsLiveBlur() can apply
// it identically to the box it evaluates wantsBackgroundResample() against —
// the two must agree on 90/270-degree-rotated monitors.
[[nodiscard]] inline CBox applyMonitorTransform(CBox box, PHLMONITOR monitor) {
    if (!monitor)
        return box;

    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    box.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    return box;
}

} // namespace WindowGeometry
