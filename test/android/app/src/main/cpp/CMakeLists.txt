# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.18.1)

set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(SORA_DIR "" CACHE PATH "Sora のルートディレクトリ")

project("hello" C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)

find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_library(hello
  SHARED
    jni_onload.cc
    native-lib.cpp
    ../../../../../hello.cpp
)

target_link_libraries(hello PRIVATE Sora::sora)

# libwebrtc が ELF フォーマットの新しい仕様を使ってリンクしているため、
# Android NDK に用意されている標準のリンカーだと以下のようなリンクエラーになってしまう。
#
# C/C++: ld.lld: error: /home/runner/work/sora-cpp-sdk/sora-cpp-sdk/_install/android/release/webrtc/lib/arm64-v8a/libwebrtc.a(jsep_ice_candidate.o):(.rodata+0x4): unknown relocation (315) against symbol typeinfo for webrtc::JsepIceCandidate
#
# なので libwebrtc と同じバージョンのリンカーを使ってリンクするために -B でリンカーのパスを指定する。
#
# 新しい仕様の該当コミット: https://github.com/llvm/llvm-project/commit/04a906ec980e7bf49ffda0808766f51d08e8ae76
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -B ${LLVM_DIR}/bin")

target_compile_options(hello
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
    "$<$<COMPILE_LANGUAGE:CXX>:-isystem${LIBCXX_INCLUDE_DIR}>"
)

target_compile_definitions(hello PRIVATE HELLO_ANDROID)

find_library(ANDROID_LIB_LOG log)
find_library(ANDROID_LIB_ANDROID android)
find_library(ANDROID_LIB_OPENSLES OpenSLES)
find_library(ANDROID_LIB_EGL EGL)

target_link_libraries(hello
  PRIVATE
    ${ANDROID_LIB_LOG}
    ${ANDROID_LIB_ANDROID}
    ${ANDROID_LIB_OPENSLES}
    ${ANDROID_LIB_EGL}
)

file(READ ${SORA_DIR}/share/webrtc.ldflags _WEBRTC_ANDROID_LDFLAGS)
string(REGEX REPLACE "\n" ";" _WEBRTC_ANDROID_LDFLAGS "${_WEBRTC_ANDROID_LDFLAGS}")
target_link_options(hello
  PRIVATE
    ${_WEBRTC_ANDROID_LDFLAGS}
)
