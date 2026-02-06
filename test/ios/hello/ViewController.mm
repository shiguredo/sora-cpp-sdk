//
//  ViewController.m
//  hello
//
//  Created by melpon on 2022/05/03.
//

#import "ViewController.h"
#import <memory>
#import <thread>
#import "../../hello.h"
#import "AudioOutputHelperWrapper.h"

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
  // webrtc::LogMessage::LogToDebug(webrtc::LS_INFO);
  // webrtc::LogMessage::LogTimestamps();
  // webrtc::LogMessage::LogThreads();

  IosAudioInit([](std::string error) {
    if (!error.empty()) {
      NSLog(@"Failed to IosAudioInit: error=%@",
            [NSString stringWithUTF8String:error.c_str()]);
    }
  });
  auto context =
      sora::SoraClientContext::Create(sora::SoraClientContextConfig());
  HelloSoraConfig config;
  config.signaling_urls.push_back("シグナリングURL");
  config.channel_id = "チャンネルID";
  config.role = "sendrecv";
  auto hello = std::make_shared<HelloSora>(context, config);
  hello->Run();
}

std::shared_ptr<std::thread> th;

@interface ViewController () <AudioChangeRouteDelegate>

- (IBAction)setHandsfree:(UIButton*)sender;
@property(weak, nonatomic) IBOutlet UIButton* handsfreeButton;

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
  audioHelperWrapper_ =
      [[AudioOutputHelperWrapper alloc] initWithDelegate:self];
  [self setHandsfreeButtonText];
}

- (IBAction)setHandsfree:(UIButton*)sender {
  BOOL isHandsfree = [audioHelperWrapper_ isHandsfree];
  [audioHelperWrapper_ setHandsfree:!isHandsfree];
}

- (void)didChangeRoute {
  [self setHandsfreeButtonText];
}

@end
