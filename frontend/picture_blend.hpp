#pragma once
#include <SDL.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

inline SDL_BlendMode subtractive_blend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}

// Metal performs subtraction directly on the GPU. SDL's software renderer
// cannot use custom blends; keep its pixel-accurate fallback out of that path.
template<class Draw> void draw_subtractive(SDL_Renderer* renderer, Draw draw) {
    auto* target = SDL_GetRenderTarget(renderer);
    int width = 0, height = 0, logical_w = 0, logical_h = 0;
    if ((target ? SDL_QueryTexture(target, nullptr, nullptr, &width, &height) :
        SDL_GetRendererOutputSize(renderer, &width, &height)) != 0) throw std::runtime_error(SDL_GetError());
    SDL_Rect viewport{}, clip{}; float sx = 1, sy = 1;
    SDL_RenderGetLogicalSize(renderer, &logical_w, &logical_h);
    SDL_RenderGetViewport(renderer, &viewport); SDL_RenderGetScale(renderer, &sx, &sy);
    const bool clipped = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
    SDL_RenderGetClipRect(renderer, &clip);
    SDL_BlendMode blend; Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawBlendMode(renderer, &blend); SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
    auto full = [&] {
        SDL_RenderSetLogicalSize(renderer, 0, 0); SDL_RenderSetScale(renderer, 1, 1);
        SDL_RenderSetViewport(renderer, nullptr); SDL_RenderSetClipRect(renderer, nullptr);
    };
    auto layout = [&] {
        SDL_RenderSetLogicalSize(renderer, logical_w, logical_h); SDL_RenderSetScale(renderer, sx, sy);
        SDL_RenderSetViewport(renderer, &viewport); SDL_RenderSetClipRect(renderer, clipped ? &clip : nullptr);
    };
    const auto bytes = static_cast<std::size_t>(width) * height * 4;
    std::vector<unsigned char> destination(bytes), source(bytes);
    full();
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, destination.data(), width * 4) != 0)
        throw std::runtime_error(SDL_GetError());
    using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
    Texture layer(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height), SDL_DestroyTexture);
    if (!layer || SDL_SetRenderTarget(renderer, layer.get()) != 0) throw std::runtime_error(SDL_GetError());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
    layout(); SDL_SetRenderDrawBlendMode(renderer, blend); SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    draw(); full();
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, source.data(), width * 4) != 0)
        throw std::runtime_error(SDL_GetError());
    for (std::size_t i = 0; i < bytes; i += 4) for (int c = 0; c < 3; ++c)
        destination[i + c] = static_cast<unsigned char>(std::max(0, destination[i + c] - source[i + c]));
    if (SDL_SetRenderTarget(renderer, target) != 0) throw std::runtime_error(SDL_GetError());
    full();
    Texture result(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height), SDL_DestroyTexture);
    if (!result || SDL_UpdateTexture(result.get(), nullptr, destination.data(), width * 4) != 0 ||
        SDL_SetTextureBlendMode(result.get(), SDL_BLENDMODE_NONE) != 0 || SDL_RenderCopy(renderer, result.get(), nullptr, nullptr) != 0)
        throw std::runtime_error(SDL_GetError());
    layout(); SDL_SetRenderDrawBlendMode(renderer, blend); SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
}
