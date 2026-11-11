#pragma once
#include <SDL.h>
#include <stdexcept>

inline SDL_RendererFlip normalize_picture_area(SDL_FRect& destination) {
    int flip = SDL_FLIP_NONE;
    if (destination.w < 0) { destination.x += destination.w; destination.w = -destination.w; flip |= SDL_FLIP_HORIZONTAL; }
    if (destination.h < 0) { destination.y += destination.h; destination.h = -destination.h; flip |= SDL_FLIP_VERTICAL; }
    return static_cast<SDL_RendererFlip>(flip);
}
inline void draw_picture_copy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* source,
    SDL_FRect destination, double angle) {
    const auto flip = normalize_picture_area(destination);
    if (destination.w == 0 || destination.h == 0) return;
    // Game.exe rotates the scaled rectangle around its center for every anchor.
    if (SDL_RenderCopyExF(renderer, texture, source, &destination, angle, nullptr, flip) != 0)
        throw std::runtime_error(SDL_GetError());
}
