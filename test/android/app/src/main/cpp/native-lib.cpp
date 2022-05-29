#include <jni.h>
#include <memory>
#include <string>
#include <thread>

#include <rtc_base/logging.h>
#include <sdk/android/native_api/jni/scoped_java_ref.h>

#include "../../../../../hello.h"

static std::unique_ptr<std::thread> g_th;
static jobject g_ctx = nullptr;

void SetAndroidApplicationContext(JNIEnv* env, jobject ctx) {
  if (g_ctx != nullptr) {
    env->DeleteGlobalRef(g_ctx);
    g_ctx = nullptr;
  }
  if (ctx != nullptr) {
    g_ctx = env->NewGlobalRef(ctx);
  }
}
void* GetAndroidApplicationContext(void* env) {
  return g_ctx;
}

extern "C" JNIEXPORT void JNICALL
Java_jp_shiguredo_hello_MainActivity_run(JNIEnv* env,
                                         jobject /* this */,
                                         jobject ctx) {
  SetAndroidApplicationContext(env, ctx);
  g_th.reset(new std::thread([]() {
    HelloSoraConfig cfg;
    cfg.signaling_urls.push_back("シグナリングURL");
    cfg.channel_id = "チャンネルID";
    cfg.role = "sendonly";
    std::shared_ptr<HelloSora> hello(new HelloSora(cfg));
    hello->Init();
    hello->Run();
    RTC_LOG(LS_INFO) << "Finished io_context thread";
  }));
}