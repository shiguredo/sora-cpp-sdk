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

void Run() {
    HelloSoraConfig config;
    config.signaling_urls.push_back("シグナリングURL");
    config.channel_id = "チャンネルID";
    config.role = "sendonly";
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
