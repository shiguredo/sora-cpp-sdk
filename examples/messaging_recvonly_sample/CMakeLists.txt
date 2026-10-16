cmake_minimum_required(VERSION 3.23)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(BOOST_ROOT "" CACHE PATH "Boost のルートディレクトリ")
set(SORA_DIR "" CACHE PATH "Sora のルートディレクトリ")
set(CLI11_DIR "" CACHE PATH "CLI11 のルートディレクトリ")

project(sora-messaging-recvonly-sample C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)
if(WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

add_executable(messaging_recvonly_sample)
set_target_properties(messaging_recvonly_sample PROPERTIES CXX_STANDARD 20 C_STANDARD 20)
set_target_properties(messaging_recvonly_sample PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(messaging_recvonly_sample PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_sources(messaging_recvonly_sample PRIVATE src/messaging_recvonly_sample.cpp)

if(NOT WIN32)
  target_compile_options(messaging_recvonly_sample
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
      "$<$<COMPILE_LANGUAGE:CXX>:-isystem${LIBCXX_INCLUDE_DIR}>"
  )
else()
  # 文字コードを utf-8 として扱うのと、シンボルテーブル数を増やす
  # clang-cl は /EHsc を指定しないと C++ の例外処理が無効になるため、C++ の例外処理を有効にする
  target_compile_options(messaging_recvonly_sample PRIVATE /utf-8 /bigobj "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
  set_target_properties(messaging_recvonly_sample
    PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )

  if (USE_LIBCXX)
    # clang-cl は MSVC 互換ドライバなので、clang ドライバ向けのオプションは /clang: を付けて渡す
    target_compile_options(messaging_recvonly_sample
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/clang:-nostdinc++>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:/I${LIBCXX_INCLUDE_DIR}>"
    )
  endif()
endif()
target_include_directories(messaging_recvonly_sample PRIVATE ${CLI11_DIR}/include)
target_link_libraries(messaging_recvonly_sample PRIVATE Sora::sora)

target_compile_definitions(messaging_recvonly_sample PRIVATE CLI11_HAS_FILESYSTEM=0)
if(WIN32)
  target_compile_definitions(messaging_recvonly_sample
    PRIVATE
      _CONSOLE
      _WIN32_WINNT=0x0A00
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      # libc++ の __config_site は _LIBCPP_HARDENING_MODE を定義しないため、
      # libwebrtc をビルドしたときと同じ値を指定して挙動を揃える
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
      # libc++ のシンボルを __declspec(dllimport) にしないために必要
      _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
  )
endif()

if (LINUX)
  set_target_properties(messaging_recvonly_sample PROPERTIES BUILD_RPATH "\$ORIGIN")
endif()
