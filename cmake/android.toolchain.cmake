# android が提供している android.toolchain.cmake は、
# （当然ながら）Android NDK に内容している C/C++ コンパイラを利用している。
# しかし libwebrtc は、Chromium の管理している clang を利用しているので、
# それを利用するように上書きする。
include("${ANDROID_OVERRIDE_TOOLCHAIN_FILE}")
set(CMAKE_C_COMPILER "${ANDROID_OVERRIDE_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${ANDROID_OVERRIDE_CXX_COMPILER}")
