#pragma once

// Client surface commits are the only signal that content below a glassed layer
// changed (e.g. a playing video) - the scene-generation cache only sees window events.
namespace LayerDamageObserver {
    // Follows layers:enabled. Enabling subscribes to view creation and sweeps the
    // surfaces that already exist; disabling drops every listener.
    void setEnabled(bool enabled);
}
