#ifndef SORA_JAVA_CONTEXT_H_
#define SORA_JAVA_CONTEXT_H_

namespace sora {

// Android プラットフォームの場合は有効な JNIEnv* を返す。
// それ以外のプラットフォームの場合は nullptr を返す。
void* GetJNIEnv();

}  // namespace sora

#endif