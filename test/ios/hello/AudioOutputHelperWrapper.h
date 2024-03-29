//
//  AudioOutputHelperWrapper.h
//  hello
//
//  Created by tnoho on 2023/09/07.
//

#ifndef AudioOutputHelperWrapper_h
#define AudioOutputHelperWrapper_h

#import <Foundation/Foundation.h>

// iOS の他の Objective-C API と遜色なく使えるようラッピングする

@protocol AudioChangeRouteDelegate <NSObject>
- (void)didChangeRoute;
@end

@interface AudioOutputHelperWrapper : NSObject
- (instancetype)initWithDelegate:(id<AudioChangeRouteDelegate>)delegate;
- (BOOL)isHandsfree;
- (void)setHandsfree:(BOOL)enable;
@end

#endif /* AudioOutputHelperWrapper_h */
