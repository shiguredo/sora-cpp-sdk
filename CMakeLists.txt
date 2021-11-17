cmake_minimum_required(VERSION 3.16)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(ROTOR_ROOT_DIR "" CACHE PATH "Rotor のインストール先ディレクトリ\n空文字だった場合はデフォルト検索パスの Rotor を利用する")
set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(USE_LIBCXX OFF CACHE BOOL "libstdc++ の代わりに libc++ を使うかどうか")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")

project(sora-cpp-sdk C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_PREFIX_PATH ${ROTOR_ROOT_DIR})

set(Boost_USE_STATIC_RUNTIME ON)

find_package(WebRTC REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem json date_time system regex)
find_package(rotor REQUIRED)
find_package(Threads REQUIRED)

# libsora.a を作る

add_library(sora STATIC)
set_target_properties(sora PROPERTIES CXX_STANDARD 17 C_STANDARD 17)

target_sources(sora
  PRIVATE
    src/sora.cpp
    src/ssl_verifier.cpp
    src/url_parts.cpp
)

target_link_libraries(sora
  PUBLIC
    rotor::rotor_asio
    rotor::rotor
    WebRTC::WebRTC
    Boost::filesystem
    Boost::system
)

if (USE_LIBCXX)
  target_compile_options(sora
    PUBLIC
      "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE_DIR}>"
  )
endif()

if (WIN32)
  # 文字コードを utf-8 として扱うのと、シンボルテーブル数を増やす
  target_compile_options(sora PRIVATE /utf-8 /bigobj)
  set_target_properties(sora
    PROPERTIES
      # CRTライブラリを静的リンクさせる
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )

  target_link_libraries(sora
    PRIVATE
  #    dbghelp.lib
  #    delayimp.lib
  #    dnsapi.lib
  #    msimg32.lib
  #    oleaut32.lib
  #    psapi.lib
  #    shell32.lib
  #    shlwapi.lib
  #    usp10.lib
  #    version.lib
  #    wininet.lib
      winmm.lib
  #    ws2_32.lib
  #    amstrmid.lib
  #    Strmiids.lib
  #    crypt32.lib
  #    dmoguids.lib
  #    iphlpapi.lib
  #    msdmo.lib
  #    Secur32.lib
  #    wmcodecdspuuid.lib
  )

  target_compile_definitions(sora
    PUBLIC
      _CONSOLE
      _WIN32_WINNT=0x0A00
      WEBRTC_WIN
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      UNICODE
      _UNICODE
      _HAS_ITERATOR_DEBUGGING=0
  )
elseif (APPLE)

  target_compile_options(sora PRIVATE -fconstant-string-class=NSConstantString -mmacosx-version-min=11.0)
  target_link_options(sora PRIVATE -ObjC)
  set_target_properties(sora PROPERTIES CXX_VISIBILITY_PRESET hidden)

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_MAC
  )

  #target_link_libraries(sora
  #  PRIVATE
  #    "-framework Foundation"
  #    "-framework AVFoundation"
  #    "-framework CoreServices"
  #    "-framework CoreFoundation"
  #    "-framework AudioUnit"
  #    "-framework AudioToolbox"
  #    "-framework CoreAudio"
  #    "-framework CoreGraphics"
  #    "-framework CoreMedia"
  #    "-framework CoreVideo"
  #    "-framework VideoToolbox"
  #    "-framework AppKit"
  #    "-framework Metal"
  #    "-framework MetalKit"
  #    "-framework OpenGL"
  #)

elseif (UNIX)
  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
  )
  # Linux 系の場合はカスタムされた libc++ を使っているためオプション追加
  target_compile_definitions(sora PUBLIC _LIBCPP_ABI_UNSTABLE _LIBCPP_DISABLE_AVAILABILITY)

  set_target_properties(sora PROPERTIES POSITION_INDEPENDENT_CODE ON)
  #target_link_libraries(sora
  #  PRIVATE
  #    X11
  #    Xau
  #    Xdmcp
  #    Xtst
  #    xcb
  #    plds4
  #    Xext
  #    expat
  #    dl
  #    nss3
  #    nssutil3
  #    plc4
  #    nspr4
  #    rt
  #    Threads::Threads
  #)
endif()




# テスト用バイナリを作る
enable_testing()
add_executable(test_hello test/hello.cpp)
set_target_properties(test_hello PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
set_target_properties(test_hello PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(test_hello PRIVATE sora)
SET(CTEST_OUTPUT_ON_FAILURE ON)
add_test(
  NAME test_hello
  COMMAND $<TARGET_FILE:test_hello>
)
set_tests_properties(test_hello PROPERTIES TIMEOUT 3) 