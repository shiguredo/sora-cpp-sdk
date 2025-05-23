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
list(APPEND CMAKE_PREFIX_PATH ${CATCH2_ROOT}/lib/cmake)

set(Boost_USE_STATIC_LIBS ON)
if (WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
find_package(Threads REQUIRED)
find_package(Catch2)

find_library(Blend2D_LIBRARY NAMES blend2d PATHS ${BLEND2D_ROOT_DIR}/lib)

function (init_target target)
  target_sources(${target} PRIVATE fake_video_capturer.cpp)
  if (WIN32)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 C_STANDARD 20)
  else()
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
  endif()
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${target} PRIVATE BL_STATIC=1)
  target_include_directories(${target} PRIVATE ${BLEND2D_ROOT_DIR}/include)
  target_link_libraries(${target} PRIVATE Sora::sora ${Blend2D_LIBRARY})

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

if (TEST_DEVICE_LIST)
  add_executable(device_list)
  target_sources(device_list PRIVATE device_list.cpp)
  init_target(device_list)
endif()

if (TEST_E2E)
  add_executable(e2e)
  target_sources(e2e PRIVATE e2e.cpp)
  init_target(e2e)
  target_link_libraries(e2e PRIVATE Catch2::Catch2WithMain Catch2::Catch2)
endif()
