//
//  ViewController.m
//  hello
//
//  Created by melpon on 2022/05/03.
//

#import <thread>
#import <memory>
#import "ViewController.h"
#import "../../hello.h"

#import <sdk/objc/components/audio/RTCAudioSession.h>
#import <sdk/objc/components/audio/RTCAudioSessionConfiguration.h>

void IosAudioInit(std::function<void(std::string)> on_complete) {
  auto config = [RTCAudioSessionConfiguration webRTCConfiguration];
  config.category = AVAudioSessionCategoryPlayAndRecord;
  [[RTCAudioSession sharedInstance] initializeInput:^(NSError* error) {
    if (error != nil) {
      on_complete([error.localizedDescription UTF8String]);
    } else {
      on_complete("");
    }
  }];
}

void Run() {
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    rtc::LogMessage::LogTimestamps();
    rtc::LogMessage::LogThreads();

    IosAudioInit([](std::string error) {
        if (!error.empty()) {
            NSLog(@"Failed to IosAudioInit: error=%@", [NSString stringWithUTF8String:error.c_str()]);
        }
    });
    NSString* path = [[NSBundle mainBundle] resourcePath];
    NSLog(@"path1=%@", path);
    NSString* path2 = [path stringByAppendingPathComponent:@"model_coeffs"];
    NSLog(@"path2=%@", path2);
    setenv("SORA_LYRA_MODEL_COEFFS_PATH", [path2 UTF8String], 1);
    HelloSoraConfig config;
    config.signaling_urls.push_back("シグナリングURL");
    config.channel_id = "チャンネルID";
    config.role = "sendonly";
    config.mode = HelloSoraConfig::Mode::Lyra;
    auto hello = sora::CreateSoraClient<HelloSora>(config);
    hello->Run();
}

std::shared_ptr<std::thread> th;

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    th.reset(new std::thread(&Run));
    [super viewDidLoad];
    // Do any additional setup after loading the view.
}


@end
