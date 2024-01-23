#include "sora/java_context.h"

#ifdef SORA_CPP_SDK_ANDROID
#include <modules/utility/include/jvm_android.h>
#include <sdk/android/native_api/jni/jvm.h>
#include <sdk/android/src/jni/jvm.h>
#endif

namespace sora {

void* GetJNIEnv() {
#ifdef SORA_CPP_SDK_ANDROID
  // webrtc::JVM::Initialize を呼んでおかないと
  // libwebrtc の内部関数で落ちることがあるので設定しておく
  static bool jvm_initialized = false;
  if (!jvm_initialized) {
    webrtc::JVM::Initialize(webrtc::jni::GetJVM());
    jvm_initialized = true;
  }
  return webrtc::AttachCurrentThreadIfNeeded();
#else
  return nullptr;
#endif
}

}  // namespace sora