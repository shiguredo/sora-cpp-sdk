//
//  ViewController.m
//  hello
//
//  Created by melpon on 2022/05/03.
//

#import <thread>
#import <memory>
#import "AudioOutputHelperWrapper.h"
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
    // rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    // rtc::LogMessage::LogTimestamps();
    // rtc::LogMessage::LogThreads();

    IosAudioInit([](std::string error) {
        if (!error.empty()) {
            NSLog(@"Failed to IosAudioInit: error=%@", [NSString stringWithUTF8String:error.c_str()]);
        }
    });
    NSString* path = [[NSBundle mainBundle] resourcePath];
    // NSLog(@"path1=%@", path);
    NSString* path2 = [path stringByAppendingPathComponent:@"model_coeffs"];
    NSLog(@"SORA_LYRA_MODEL_COEFFS_PATH=%@", path2);
    setenv("SORA_LYRA_MODEL_COEFFS_PATH", [path2 UTF8String], 1);
    auto context = sora::SoraClientContext::Create(sora::SoraClientContextConfig());
    HelloSoraConfig config;
    config.signaling_urls.push_back("シグナリングURL");
    config.channel_id = "チャンネルID";
    config.role = "sendrecv";
    // config.mode = HelloSoraConfig::Mode::Lyra;
    auto hello = std::make_shared<HelloSora>(context, config);
    hello->Run();
}

std::shared_ptr<std::thread> th;

@interface ViewController () <AudioChangeRouteDelegate>

- (IBAction)setHandsfree:(UIButton *)sender;
@property (weak, nonatomic) IBOutlet UIButton *handsfreeButton;

@end

@implementation ViewController {
    AudioOutputHelperWrapper* audioHelperWrapper_;
}

- (void)setHandsfreeButtonText {
    BOOL isHandsfree = [audioHelperWrapper_ isHandsfree];
    if (isHandsfree) {
        self.handsfreeButton.titleLabel.text = @"Set Default";
    } else {
        self.handsfreeButton.titleLabel.text = @"Set Handsfree";
    }
}

- (void)viewDidLoad {
    th.reset(new std::thread(&Run));
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    audioHelperWrapper_ = [[AudioOutputHelperWrapper alloc] initWithDelegate:self];
    [self setHandsfreeButtonText];
}


- (IBAction)setHandsfree:(UIButton *)sender {
    BOOL isHandsfree = [audioHelperWrapper_ isHandsfree];
    [audioHelperWrapper_ setHandsfree:!isHandsfree];
}

- (void)didChangeRoute {
    [self setHandsfreeButtonText];
}

@end
