#include <rotor.hpp>
#include <rotor/asio.hpp>
#include <rtc_base/logging.h>
#include <boost/asio.hpp>
 
struct server_actor : public rotor::actor_base_t {
    using rotor::actor_base_t::actor_base_t;
    void on_start() noexcept override {
        rotor::actor_base_t::on_start();
        RTC_LOG(LS_INFO) << "hello world";
        std::cout << "hello world" << std::endl;
        supervisor->shutdown();
    }
};
 
class RTCManager {
public:
};

extern "C" {

void sora_run() {
    boost::asio::io_context io_context;
    auto system_context = rotor::asio::system_context_asio_t::ptr_t{new rotor::asio::system_context_asio_t(io_context)};
    auto strand = std::make_shared<boost::asio::io_context::strand>(io_context);
    auto timeout = boost::posix_time::milliseconds{500};
    auto sup = system_context->create_supervisor<rotor::asio::supervisor_asio_t>()
               .strand(strand).timeout(timeout).finish();
 
    sup->create_actor<server_actor>().timeout(timeout).finish();
 
    sup->start();
    io_context.run();
    std::cout << "hoge-" << std::endl;
    RTC_LOG(LS_INFO) << "fuga-";
}

}