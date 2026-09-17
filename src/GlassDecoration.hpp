#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CGlassDecoration : public IHyprWindowDecoration {
  public:
    explicit CGlassDecoration(PHLWINDOW window);
    ~CGlassDecoration() override;

    [[nodiscard]] SDecorationPositioningInfo getPositioningInfo() override;
    void                                     onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                                     draw(PHLMONITOR monitor, float const& alpha) override;
    [[nodiscard]] eDecorationType            getDecorationType() override;
    void                                     updateWindow(PHLWINDOW window) override;
    void                                     damageEntire() override;
    [[nodiscard]] eDecorationLayer           getDecorationLayer() override;
    [[nodiscard]] uint64_t                   getDecorationFlags() override;
    [[nodiscard]] std::string                getDisplayName() override;

    [[nodiscard]] PHLWINDOW getOwner();
    void                    renderPass(PHLMONITOR monitor, const float& alpha);
    void                    onFullscreenStateChanged();

    // Owner test without the shared_ptr copy getOwner() hands out.
    [[nodiscard]] bool  ownsWindow(const PHLWINDOW& window) const { return m_window == window; }
    // self_sample as of the last rendered frame, 0 when the glass is off. Read on
    // every watched surface commit, so it must not walk the preset chain.
    [[nodiscard]] float lastSelfSample() const { return m_lastSelfSample; }

    // Weak over the UP Hyprland owns: use .get()/-> only, never .lock().
    WP<CGlassDecoration> m_self;

  private:
    PHLWINDOWREF m_window;
    SP<Render::IFramebuffer> m_sampleFramebuffer;
    Vector2D     m_samplePaddingRatio;

    // Last geometry seen in updateWindow(), to detect real moves/resizes
    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    // Whether we currently hold the noblur window property on our window.
    // Glass replaces Hyprland's blur, and Hyprland's cached-blur optimization
    // (blur:new_optimizations) composites translucent windows against a
    // snapshot taken before plugin decorations render — without noblur the
    // glass is invisible on static windows (#46).
    bool m_noBlurApplied = false;

    float m_lastSelfSample = 0.0f;

    void updateNoBlurProp(bool glassEnabled);
    void withdrawNoBlur();

    [[nodiscard]] bool        resolveEnabled() const;
    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;

    friend class CGlassPassElement;
};
