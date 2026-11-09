#import <Foundation/Foundation.h>
#include <cstring>

extern "C" char* wolf_ios_documents_path() {
    @autoreleasepool {
        NSString* path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        return path ? strdup(path.fileSystemRepresentation) : nullptr;
    }
}
