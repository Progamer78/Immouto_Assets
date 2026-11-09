#pragma once
#include "runtime.hpp"
#include <SDL.h>

void process_file_operations(SDL_Renderer* renderer, wolf::Resources& writable,
    const wolf::Json& operations, int width, int height);
