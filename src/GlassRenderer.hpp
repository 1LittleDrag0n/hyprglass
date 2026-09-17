#pragma once

#include "PluginConfig.hpp"

#include <array>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Maximum downscale factor for blur sampling. Half-res (2) is 4x cheaper
// per blur pass. Only applied when blur is strong enough to hide the lower
// resolution — weak blur at half-res shows visible pixelation.
inline constexpr int   BLUR_DOWNSCALE_MAX       = 2;
inline constexpr float BLUR_DOWNSCALE_THRESHOLD = 0.35f; // min blur_strength for downscale

// Must match the `regionRects[16]` array size declared in Shaders.hpp.
inline constexpr int MAX_REGION_RECTS = 16;

// Box-local pixel rect uploaded to the shader's regionRects uniform array.
struct SRegionRect {
    float x = 0, y = 0, w = 0, h = 0;
};
static_assert(sizeof(SRegionRect) == 4 * sizeof(float));

// Layers only: alpha mask from the temp FBO that captured the rendered surface.
// Constrains the glass effect to regions where the layer has visible content.
// Windows do not use masking, they pass mask=nullptr to applyGlassEffect.
struct SMaskInfo {
    GLuint   textureId;
    GLenum   target;
    Vector2D uvOffset; // mapping from glass box UV → full surface UV
    Vector2D uvScale;
    float    alphaThreshold = 0.001f;

    // 0 = alpha-threshold mask, 1 = ext-background-effect-v1 protocol region
    int                                        maskMode        = 0;
    std::array<SRegionRect, MAX_REGION_RECTS>  regionRects{};
    int                                        regionRectCount = 0;
};

// Affine map from source-framebuffer pixels into the sample framebuffer.
// sampleBackground() blits through it and blendOwnContent() draws through it;
// both derive every coordinate from here so the two can never drift apart.
struct SSampleMap {
    int   fullWidth = 1, fullHeight = 1;              // padded box, framebuffer pixels
    int   width = 1, height = 1;                      // sample framebuffer size
    int   srcX0 = 0, srcY0 = 0, srcX1 = 1, srcY1 = 1; // padded box in source pixels, (srcX0, srcY0) lands on sample (0, 0)
    float scaleX = 1, scaleY = 1;

    [[nodiscard]] CBox toSample(const CBox& framebufferBox) const;
};

[[nodiscard]] SSampleMap sampleMapFor(const CBox& box, int downscale);

void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, Vector2D& outPaddingRatio, int downscale = 1);

// Draws the window's own committed surfaces over the sampled background, so the
// blur that follows works on a mix of the desktop and the window's own content.
// box is the same framebuffer-space box sampleBackground() was given.
void blendOwnContent(SP<Render::IFramebuffer>& sampleFramebuffer, PHLWINDOW window, PHLMONITOR monitor,
                      const CBox& box, int downscale, float amount, float cornerRadius, float roundingPower);

// callerFramebuffer is re-bound after the blur ping-pong; the viewport is
// restored from its size so it always matches the re-bound framebuffer
// (monitor fields would be wrong on 90°/270° transformed monitors).
void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    SP<Render::IFramebuffer> callerFramebuffer);

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr);

} // namespace GlassRenderer
