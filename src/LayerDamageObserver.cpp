#include "LayerDamageObserver.hpp"

#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/protocols/core/Subcompositor.hpp>
#include <hyprland/src/protocols/types/SurfaceRole.hpp>

#include <algorithm>

using namespace Desktop::View;

namespace {
    // Hyprland's own T1-parent walk guards against a cycle in the parent chain.
    constexpr int MAX_TREE_DEPTH = 64;

    bool isEnabled() {
        return g_pGlobalState && !g_pGlobalState->observerListeners.empty();
    }

    // Root of the tree this surface belongs to: the window/layer/lock-screen root
    // surface, or the topmost popup of a layer-owned popup chain. Mirrors how
    // Hyprland propagates the T1 owner down subsurface and popup chains.
    SP<CWLSurfaceResource> ownerRootSurface(SP<CWLSurfaceResource> resource) {
        for (int depth = 0; resource && depth < MAX_TREE_DEPTH; ++depth) {
            const auto role = resource->m_role;
            if (role->role() == SURFACE_ROLE_SUBSURFACE) {
                const auto subsurface = static_cast<CSubsurfaceRole*>(role.get())->m_subsurface.lock();
                if (!subsurface)
                    return nullptr;
                resource = subsurface->m_parent.lock();
                continue;
            }

            if (role->role() == SURFACE_ROLE_XDG_SHELL) {
                const auto xdgSurface = static_cast<CXDGSurfaceRole*>(role.get())->m_xdgSurface.lock();
                const auto popup      = xdgSurface ? xdgSurface->m_popup.lock() : nullptr;
                if (!popup)
                    return resource;

                const auto parent = popup->m_parent.lock();
                if (!parent) // layer-shell popup: no toplevel above it
                    return resource;

                resource = parent->m_surface.lock();
                continue;
            }

            return resource;
        }

        return nullptr;
    }

    // Window owning this surface's tree; null for layers and layer-owned popups.
    PHLWINDOW ownerWindow(const PHLWINDOW& viewWindow, const SP<CWLSurfaceResource>& resource) {
        if (viewWindow)
            return viewWindow;

        const auto ownerWl = CWLSurface::fromResource(ownerRootSurface(resource));
        return ownerWl ? CWindow::fromView(ownerWl->view()) : nullptr;
    }

    // Mirrors Hyprland's own commit-time damage gates (Window.cpp, Subsurface.cpp,
    // Popup.cpp): a window that is unmapped, hidden or on an invisible workspace
    // draws nothing, so its commits cannot change what a layer samples. Surfaces
    // with no owning window (layers, layer-owned popups) are never gated.
    bool passesVisibilityGate(const PHLVIEW& view, const PHLWINDOW& window) {
        if (!window)
            return true;

        if (!window->m_isMapped)
            return false;

        if (view->type() == VIEW_TYPE_WINDOW && window->isHidden())
            return false;

        return !window->m_workspace || window->m_workspace->m_visible;
    }

    bool surfaceInTree(const SP<CWLSurfaceResource>& surface, const SP<CWLSurfaceResource>& root) {
        if (!root)
            return false;
        return root->findFirstPreorder([&surface](SP<CWLSurfaceResource> candidate) { return candidate == surface; }) != nullptr;
    }

    void onSurfaceCommit(const SP<CWLSurfaceResource>& resource) {
        if (!g_pGlobalState || !resource)
            return;

        const auto& config = g_pGlobalState->config;
        if (!config.layersEnabled || !**config.layersEnabled || g_pGlobalState->layerSurfaces.empty())
            return;

        // cheap skip when nothing can want a live resample
        const bool globalLive = config.layersLiveResample && **config.layersLiveResample;
        if (!globalLive && std::ranges::none_of(g_pGlobalState->layerNamespaceLiveResample, [](const auto& kv) { return kv.second; }))
            return;

        // same bit Hyprland tests: commits without damage change nothing behind us
        if (!resource->m_current.updated.bits.damage)
            return;

        const auto wlSurface = CWLSurface::fromResource(resource);
        if (!wlSurface)
            return;

        const auto view = wlSurface->view();
        // lock surfaces never reached the old damage path, and a glassed layer is
        // not sampled from under the lock
        if (!view || view->type() == VIEW_TYPE_LOCK_SCREEN)
            return;

        const auto viewWindow = CWindow::fromView(view); // non-null only for a window root
        if (!passesVisibilityGate(view, ownerWindow(viewWindow, resource)))
            return;

        // nullopt for anything Hyprland would not render (and for IME popups, which
        // cannot be placed globally at all) — skipping is the only correct answer
        const auto box = wlSurface->getSurfaceBoxGlobal();
        if (!box.has_value())
            return;

        CRegion damage = wlSurface->computeDamage();
        if (damage.empty())
            return;

        // X11 clients draw at their own scale; only a window root carries it
        if (viewWindow && viewWindow->m_isX11 && viewWindow->m_X11SurfaceScaledBy != 1.f)
            damage.scale(1.0 / viewWindow->m_X11SurfaceScaledBy);

        // the animated origin, not Hyprland's animation goal: it is where the
        // content is actually drawn this frame
        damage.translate(box->pos());
        const CBox damagedBox = damage.getExtents();

        for (const auto& [_, state] : g_pGlobalState->layerSurfaces) {
            const auto layer = state->getLayerSurface();
            if (!layer || !layer->m_mapped)
                continue;

            if (!state->liveResampleEnabled())
                continue;

            const auto  monitor   = layer->m_monitor.lock();
            const float monScale  = monitor ? monitor->m_scale : 1.0f;
            CBox        sampleBox = CBox{layer->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT),
                                         layer->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)};
            sampleBox.expand(GlassRenderer::SAMPLE_PADDING_PX / monScale);

            if (!sampleBox.overlaps(damagedBox))
                continue;

            // a layer's own content is not its background
            if (surfaceInTree(resource, layer->wlSurface() ? layer->wlSurface()->resource() : nullptr))
                continue;

            state->markBackgroundDirty();
        }
    }

    void watchSurface(const SP<CWLSurfaceResource>& resource) {
        if (!g_pGlobalState || !resource)
            return;

        const WP<CWLSurfaceResource> key = resource;

        auto&                        watched = g_pGlobalState->watchedSurfaces;
        if (watched.contains(key))
            return;

        const auto wlSurface = CWLSurface::fromResource(resource);
        if (!wlSurface)
            return;

        auto& entry  = watched[key];
        entry.commit = resource->m_events.commit.listen([key] {
            if (const auto surface = key.lock())
                onSurfaceCommit(surface);
        });
        // erasing from inside the callback is safe: the emitting signal holds a
        // strong ref for the whole emit and the capture is by value
        entry.destroy = wlSurface->m_events.destroy.listen([key] {
            if (g_pGlobalState)
                g_pGlobalState->watchedSurfaces.erase(key);
        });
    }

    void watchView(const PHLVIEW& view) {
        if (!isEnabled() || !view)
            return;

        watchSurface(view->resource());
    }
}

void LayerDamageObserver::setEnabled(bool enabled) {
    if (!g_pGlobalState)
        return;

    if (!enabled) {
        g_pGlobalState->observerListeners.clear();
        g_pGlobalState->watchedSurfaces.clear();
        return;
    }

    if (isEnabled())
        return;

    // before the compositor protocol exists there is nothing to watch; leave
    // isEnabled() false so the next arm attempt retries
    if (!PROTO::compositor)
        return;

    // view.create covers windows, layers, popups, subsurfaces and lock surfaces,
    // and runs after Hyprland's own role commit handler
    g_pGlobalState->observerListeners.push_back(Event::bus()->m_events.view.create.listen([](const PHLVIEW& view) { watchView(view); }));
    // an XWayland window associates its wl_surface only after view.create, so its
    // root is reachable no earlier than the map
    g_pGlobalState->observerListeners.push_back(Event::bus()->m_events.window.open.listen([](PHLWINDOW window) { watchView(window); }));

    // View state registries cannot enumerate popups/subsurfaces, so sweep every
    // live surface and keep the ones that belong to a view.
    PROTO::compositor->forEachSurface([](SP<CWLSurfaceResource> resource) {
        const auto wlSurface = CWLSurface::fromResource(resource);
        if (wlSurface && wlSurface->view())
            watchSurface(resource);
    });
}
