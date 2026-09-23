#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <chrono>
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

    WP<CGlassDecoration> m_self;

  private:
    PHLWINDOWREF m_window;
    SP<Render::IFramebuffer> m_sampleFramebuffer;
    Vector2D     m_samplePaddingRatio;

    // Track last rendered position/size to detect actual changes and seed damage
    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    // Whether we currently hold the noblur window property on our window.
    // Glass replaces Hyprland's blur, and Hyprland's cached-blur optimization
    // (blur:new_optimizations) composites translucent windows against a
    // snapshot taken before plugin decorations render — without noblur the
    // glass is invisible on static windows (#46).
    bool m_noBlurApplied = false;

    // Fade the glass in when it is switched back on (e.g. a hyprwobbly wobble
    // ends and the hyprglass_disabled tag is dropped), so it does not pop.
    float                                 m_fade        = 1.0F;
    bool                                  m_lastEnabled = true;
    std::chrono::steady_clock::time_point m_fadeStart{};

    void updateNoBlurProp(bool glassEnabled);
    void withdrawNoBlur();

    [[nodiscard]] bool        resolveEnabled() const;
    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;

    friend class CGlassPassElement;
};
