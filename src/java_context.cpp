#include "sora/java_context.h"

#ifdef SORA_CPP_SDK_ANDROID
#include <sdk/android/native_api/jni/jvm.h>
#endif

namespace sora {

void* GetJNIEnv() {
#ifdef SORA_CPP_SDK_ANDROID
  return webrtc::AttachCurrentThreadIfNeeded();
#else
  return nullptr;
#endif
}

}