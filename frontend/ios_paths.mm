#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <cstring>

extern "C" char* wolf_ios_documents_path() {
    @autoreleasepool {
        NSString* path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        return path ? strdup(path.fileSystemRepresentation) : nullptr;
    }
}

extern "C" void wolf_ios_safe_area(SDL_Window* window, float* left, float* top, float* right, float* bottom) {
    *left = *top = *right = *bottom = 0;
    SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT) return;
    UIView* view = info.info.uikit.window.rootViewController.view;
    const auto size = view.bounds.size;
    if (size.width <= 0 || size.height <= 0) return;
    const auto insets = view.safeAreaInsets;
    *left = insets.left / size.width; *right = insets.right / size.width;
    *top = insets.top / size.height; *bottom = insets.bottom / size.height;
}
