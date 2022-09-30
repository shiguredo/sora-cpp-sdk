// Sora
#include <sora/sora_default_client.h>

// CLI11
#include <CLI/CLI.hpp>

// Boost
#include <boost/optional/optional.hpp>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

struct MessagingRecvOnlySampleConfig : sora::SoraDefaultClientConfig {
  std::string signaling_url;
  std::string channel_id;
};

class MessagingRecvOnlySample : public std::enable_shared_from_this<MessagingRecvOnlySample>,
                                public sora::SoraDefaultClient {
 public:
  MessagingRecvOnlySample(MessagingRecvOnlySampleConfig config)
      : sora::SoraDefaultClient(config), config_(config) {}

  void Run() {
    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls.push_back(config_.signaling_url);
    config.channel_id = config_.channel_id;
    config.role = "recvonly";
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();
    ioc_->run();
  }

  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    ioc_->stop();
  }


 private:
  MessagingRecvOnlySampleConfig config_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
};

void add_optional_bool(CLI::App& app,
                       const std::string& option_name,
                       boost::optional<bool>& v,
                       const std::string& help_text) {
  auto f = [&v](const std::string& input) {
    if (input == "true") {
      v = true;
    } else if (input == "false") {
      v = false;
    } else if (input == "none") {
      v = boost::none;
    } else {
      throw CLI::ConversionError(input, "optional<bool>");
    }
  };
  app.add_option_function<std::string>(option_name, f, help_text)
      ->type_name("TEXT")
      ->check(CLI::IsMember({"true", "false", "none"}));
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  MessagingRecvOnlySampleConfig config;

  CLI::App app("Messaging Recvonly Sample for Sora C++ SDK");
  app.set_help_all_flag("--help-all",
                        "Print help message for all modes and exit");

  int log_level = (int)rtc::LS_ERROR;
  auto log_level_map = std::vector<std::pair<std::string, int>>(
      {{"verbose", 0}, {"info", 1}, {"warning", 2}, {"error", 3}, {"none", 4}});
  app.add_option("--log-level", log_level, "Log severity level threshold")
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case));

  // Sora に関するオプション
  app.add_option("--signaling-url", config.signaling_url, "Signaling URL")
      ->required();
  app.add_option("--channel-id", config.channel_id, "Channel ID")->required();

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    exit(app.exit(e));
  }

  config.use_audio_device = false;
  config.use_hardware_encoder = false;

  if (log_level != rtc::LS_NONE) {
    rtc::LogMessage::LogToDebug((rtc::LoggingSeverity)log_level);
    rtc::LogMessage::LogTimestamps();
    rtc::LogMessage::LogThreads();
  }

  auto sdlsample = sora::CreateSoraClient<MessagingRecvOnlySample>(config);
  sdlsample->Run();

  return 0;
}
