#pragma once
#include <SDL.h>
#include <algorithm>
#include <stdexcept>

inline void draw_window_skin(SDL_Renderer* renderer, SDL_Texture* texture, int width, int height,
    const SDL_FRect& destination, float zoom, float zoom_y, SDL_RendererFlip flip = SDL_FLIP_NONE) {
    if (width % 3 || height % 3 || width < 3 || height < 3)
        throw std::runtime_error("Window skin must divide into 3 by 3 cells");
    const int cell_w = width / 3, cell_h = height / 3;
    const float edge_w = cell_w * zoom, edge_h = cell_h * zoom_y;
    // The supplied engine overlaps the unscaled corners when a generated
    // window is shorter than both borders. It does not shrink the borders.
    const float xs[] = {destination.x, destination.x + edge_w, destination.x + destination.w - edge_w};
    const float ys[] = {destination.y, destination.y + edge_h, destination.y + destination.h - edge_h};
    const float ws[] = {edge_w, std::max(0.f, destination.w - edge_w * 2), edge_w};
    const float hs[] = {edge_h, std::max(0.f, destination.h - edge_h * 2), edge_h};
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
        const int target_col = flip & SDL_FLIP_HORIZONTAL ? 2 - col : col;
        const int target_row = flip & SDL_FLIP_VERTICAL ? 2 - row : row;
        if (ws[target_col] <= 0 || hs[target_row] <= 0) continue;
        const SDL_Rect source{col * cell_w, row * cell_h, cell_w, cell_h};
        const SDL_FRect area{xs[target_col], ys[target_row], ws[target_col], hs[target_row]};
        if (SDL_RenderCopyExF(renderer, texture, &source, &area, 0, nullptr, flip) != 0) throw std::runtime_error(SDL_GetError());
    }
}
