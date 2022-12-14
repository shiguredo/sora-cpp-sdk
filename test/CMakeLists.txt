cmake_minimum_required(VERSION 3.23)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(SORA_DIR "" CACHE PATH "Sora のルートディレクトリ")

project(sora-test C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)
if (WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
find_package(Threads REQUIRED)
find_package(Libdrm)
find_package(Libva)

function (init_target target)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(${target} PRIVATE Sora::sora)
  target_link_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/../third_party/lyra/bazel-bin)

  if (USE_LIBCXX)
    target_compile_options(${target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE_DIR}>"
    )
  endif()

  if (WIN32)
    # 文字コードを utf-8 として扱うのと、シンボルテーブル数を増やす
    target_compile_options(${target} PRIVATE /utf-8 /bigobj)
    set_target_properties(${target}
      PROPERTIES
        # CRTライブラリを静的リンクさせる
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_compile_definitions(${target}
      PRIVATE
        _CONSOLE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
  elseif (APPLE)
    set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden)
  elseif (JETSON)
    target_compile_definitions(${target} PRIVATE JETSON)
    target_link_directories(${target} PRIVATE ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra)
  endif()

endfunction()

# hello.exe を作る

add_executable(hello)
target_sources(hello PRIVATE hello.cpp)
init_target(hello)

if (TEST_CONNECT_DISCONNECT)
  add_executable(connect_disconnect)
  target_sources(connect_disconnect PRIVATE connect_disconnect.cpp)
  init_target(connect_disconnect)
endif()

if (TEST_DATACHANNEL)
  add_executable(datachannel)
  target_sources(datachannel PRIVATE datachannel.cpp)
  init_target(datachannel)
endif()

if (TEST_LYRA)
  add_executable(lyra)
  target_sources(lyra PRIVATE lyra.cpp)
  init_target(lyra)
endif()
