#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cstdlib>

extern "C" void wolf_ios_wait_for_alert_dismissal(SDL_Window* window) {
    void (^wait)(void) = ^{ @autoreleasepool {
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT) return;
        UIViewController* root = info.info.uikit.window.rootViewController;
        // SDL returns from an alert action before UIKit's dismissal animation.
        // Wait before presenting the next submenu on the same controller.
        while ([root.presentedViewController isKindOfClass:[UIAlertController class]])
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }};
    if ([NSThread isMainThread]) wait(); else dispatch_sync(dispatch_get_main_queue(), wait);
}

extern "C" int wolf_ios_prompt_integer(SDL_Window* window, const char* title, int initial, int* value) {
    __block int result = 0;
    void (^show)(void) = ^{ @autoreleasepool {
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT) return;
        UIViewController* presenter = info.info.uikit.window.rootViewController;
        while (presenter.presentedViewController) presenter = presenter.presentedViewController;
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:[NSString stringWithUTF8String:title]
            message:@"Enter a whole number (-2147483648 to 2147483647)." preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
            field.text = [NSString stringWithFormat:@"%d", initial];
            field.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
            field.clearButtonMode = UITextFieldViewModeAlways;
            field.autocorrectionType = UITextAutocorrectionTypeNo;
        }];
        UITextField* input = alert.textFields.firstObject;
        __block BOOL done = NO;
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction* action) { done = YES; }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Set" style:UIAlertActionStyleDefault handler:^(UIAlertAction* action) {
            const char* text = input.text.UTF8String; char* end = nullptr;
            errno = 0; const long long parsed = std::strtoll(text, &end, 10);
            if (end != text && *end == '\0' && errno != ERANGE && parsed >= INT_MIN && parsed <= INT_MAX) {
                *value = static_cast<int>(parsed); result = 1;
            }
            done = YES;
        }]];
        [presenter presentViewController:alert animated:YES completion:nil];
        while (!done || presenter.presentedViewController == alert)
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }};
    if ([NSThread isMainThread]) show(); else dispatch_sync(dispatch_get_main_queue(), show);
    return result;
}

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
