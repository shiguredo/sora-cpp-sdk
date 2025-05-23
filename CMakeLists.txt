cmake_minimum_required(VERSION 3.23)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(SORA_CPP_SDK_TARGET "" CACHE STRING "ビルドターゲット")
set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(USE_LIBCXX OFF CACHE BOOL "libstdc++ の代わりに libc++ を使うかどうか")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(USE_NVCODEC_ENCODER OFF CACHE BOOL "NVIDIA Video Codec SDK によるハードウェアエンコーダを利用するかどうか")

project(sora-cpp-sdk C CXX)

set(SORA_TARGET ${SORA_CPP_SDK_TARGET})

if (SORA_TARGET STREQUAL "windows_x86_64")
  set(SORA_TARGET_OS "windows")
  set(SORA_TARGET_ARCH "x86_64")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_WINDOWS")
elseif (SORA_TARGET STREQUAL "macos_x86_64")
  set(SORA_TARGET_OS "macos")
  set(SORA_TARGET_ARCH "x86_64")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_MACOS")
elseif (SORA_TARGET STREQUAL "macos_arm64")
  set(SORA_TARGET_OS "macos")
  set(SORA_TARGET_ARCH "arm64")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_MACOS")
elseif (SORA_TARGET STREQUAL "ios")
  set(SORA_TARGET_OS "ios")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_IOS")
elseif (SORA_TARGET STREQUAL "android")
  set(SORA_TARGET_OS "android")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_ANDROID")
elseif (SORA_TARGET STREQUAL "ubuntu-22.04_x86_64")
  set(SORA_TARGET_OS "ubuntu")
  set(SORA_TARGET_OS_VERSION "22.04")
  set(SORA_TARGET_ARCH "x86_64")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_UBUNTU_2204")
elseif (SORA_TARGET STREQUAL "ubuntu-24.04_x86_64")
  set(SORA_TARGET_OS "ubuntu")
  set(SORA_TARGET_OS_VERSION "24.04")
  set(SORA_TARGET_ARCH "x86_64")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_UBUNTU_2404")
elseif (SORA_TARGET STREQUAL "ubuntu-24.04_armv8")
  set(SORA_TARGET_OS "ubuntu")
  set(SORA_TARGET_OS_VERSION "24.04")
  set(SORA_TARGET_ARCH "armv8")
  set(SORA_TARGET_DEF "SORA_CPP_SDK_UBUNTU_2404")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
if (USE_VPL_ENCODER)
  list(APPEND CMAKE_PREFIX_PATH ${VPL_ROOT_DIR}/lib/cmake/vpl)
endif()

set(Boost_USE_STATIC_LIBS ON)
if (WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)

find_package(WebRTC REQUIRED)
find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(Threads REQUIRED)

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# libsora.a を作る

add_library(sora STATIC)
set_target_properties(sora PROPERTIES CXX_STANDARD 20 C_STANDARD 20)

string(SUBSTRING "${SORA_CPP_SDK_COMMIT}" 0 8 SORA_CPP_SDK_COMMIT_SHORT)
string(SUBSTRING "${WEBRTC_COMMIT}" 0 8 WEBRTC_COMMIT_SHORT)
configure_file(src/version.gen.h.template ${CMAKE_CURRENT_BINARY_DIR}/version.gen.h)
target_include_directories(sora PRIVATE ${CMAKE_CURRENT_BINARY_DIR})


target_sources(sora
  PRIVATE
    src/aligned_encoder_adapter.cpp
    src/amf_context_impl.cpp
    src/audio_device_module.cpp
    src/audio_output_helper.cpp
    src/camera_device_capturer.cpp
    src/data_channel.cpp
    src/default_video_formats.cpp
    src/device_list.cpp
    src/device_video_capturer.cpp
    src/i420_encoder_adapter.cpp
    src/java_context.cpp
    src/open_h264_video_codec.cpp
    src/open_h264_video_decoder.cpp
    src/open_h264_video_encoder.cpp
    src/rtc_ssl_verifier.cpp
    src/rtc_stats.cpp
    src/scalable_track_source.cpp
    src/session_description.cpp
    src/sora_client_context.cpp
    src/sora_peer_connection_factory.cpp
    src/sora_signaling.cpp
    src/sora_video_codec.cpp
    src/sora_video_codec_factory.cpp
    src/sora_video_decoder_factory.cpp
    src/sora_video_encoder_factory.cpp
    src/ssl_verifier.cpp
    src/url_parts.cpp
    src/version.cpp
    src/vpl_session_impl.cpp
    src/websocket.cpp
    src/zlib_helper.cpp
)

target_compile_definitions(sora PRIVATE ${SORA_TARGET_DEF})

# インストールするヘッダーファイル
file(GLOB_RECURSE SORA_HEADERS RELATIVE ${CMAKE_SOURCE_DIR} "include/*")
target_sources(sora
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${SORA_HEADERS})

# NvCodec が無効な場合もダミーの実装をコンパイルする
if (NOT USE_NVCODEC_ENCODER)
  target_sources(sora PRIVATE src/cuda_context_cuda.cpp)
endif()

target_link_libraries(sora
  PUBLIC
    WebRTC::webrtc
    Boost::json
    Boost::filesystem
)

if (USE_LIBCXX)
  target_compile_options(sora
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE_DIR}>"
  )
endif()

target_include_directories(sora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(sora INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
target_compile_definitions(sora
  PRIVATE
    $<$<BOOL:${USE_NVCODEC_ENCODER}>:USE_NVCODEC_ENCODER>
    $<$<BOOL:${USE_VPL_ENCODER}>:USE_VPL_ENCODER>
    $<$<BOOL:${USE_AMF_ENCODER}>:USE_AMF_ENCODER>
)

# OpenH264
target_include_directories(sora PRIVATE ${OPENH264_ROOT}/include)

# 指定したライブラリを自身の静的ライブラリにバンドルする
function(bundle_static_library target static_libs bundled_target)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bundled)
  set(bundled_tgt_full_name
    ${CMAKE_BINARY_DIR}/bundled/${CMAKE_STATIC_LIBRARY_PREFIX}${target}${CMAKE_STATIC_LIBRARY_SUFFIX})

  if (MSVC)
    add_custom_command(
      COMMAND ${CMAKE_AR} /NOLOGO /OUT:${bundled_tgt_full_name} $<TARGET_FILE:${target}> ${static_libs}
      OUTPUT ${bundled_tgt_full_name}
      VERBATIM
      COMMENT "Bundling libs: ${static_libs} to $<TARGET_FILE:${target}>")
  else ()
    file(WRITE  ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "CREATE ${bundled_tgt_full_name}\n" )
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "ADDLIB $<TARGET_FILE:${target}>\n")
    foreach(lib IN LISTS static_libs)
      file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "ADDLIB ${lib}\n")
    endforeach()
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "SAVE\n")
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "END\n")
    file(GENERATE
      OUTPUT ${CMAKE_BINARY_DIR}/${bundled_target}.ar
      INPUT ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in)

    add_custom_command(
      COMMAND ${CMAKE_AR} -M < ${CMAKE_BINARY_DIR}/${bundled_target}.ar
      OUTPUT ${bundled_tgt_full_name}
      VERBATIM
      COMMENT "Bundling libs: ${static_libs} to $<TARGET_FILE:${target}>")
  endif()

  add_custom_target(${bundled_target}_bundling ALL DEPENDS ${bundled_tgt_full_name})
  add_dependencies(${bundled_target}_bundling ${target})

  add_library(${bundled_target} STATIC IMPORTED)
  set_target_properties(${bundled_target}
    PROPERTIES
      IMPORTED_LOCATION ${bundled_tgt_full_name}
      INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${target},INTERFACE_INCLUDE_DIRECTORIES>)
  add_dependencies(${bundled_target} ${bundled_target}_bundling)
endfunction()

set(BUNDLE_STATIC_LIBS)

if (SORA_TARGET_OS STREQUAL "windows")
  # 文字コードを utf-8 として扱うのと、シンボルテーブル数を増やす
  target_compile_options(sora PRIVATE /utf-8 /bigobj)
  set_target_properties(sora
    PROPERTIES
      # CRTライブラリを静的リンクさせる
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )

  target_link_libraries(sora
    PUBLIC
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
      ws2_32.lib
  #    amstrmid.lib
      Strmiids.lib
  #    crypt32.lib
      dmoguids.lib
      iphlpapi.lib
      msdmo.lib
      Secur32.lib
      wmcodecdspuuid.lib
  )

  target_compile_definitions(sora
    PRIVATE
      _CONSOLE
      _WIN32_WINNT=0x0A00
      WEBRTC_WIN
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      UNICODE
      _UNICODE
  )
  target_compile_definitions(sora
    PUBLIC
      WEBRTC_WIN
      RTC_ENABLE_H265
      _ITERATOR_DEBUG_LEVEL=0
  )

  if (USE_NVCODEC_ENCODER)
    target_sources(sora
      PRIVATE
        src/hwenc_nvcodec/nvcodec_video_codec.cpp
        src/hwenc_nvcodec/nvcodec_video_encoder.cpp
        third_party/NvCodec/NvCodec/NvEncoder/NvEncoder.cpp
        third_party/NvCodec/NvCodec/NvEncoder/NvEncoderD3D11.cpp
        src/hwenc_nvcodec/nvcodec_video_decoder.cpp)
    target_include_directories(sora
      PRIVATE
        third_party/NvCodec/include
        third_party/NvCodec/NvCodec)
    target_link_libraries(sora
      PRIVATE
        DXGI.lib
        D3D11.lib
    )

    # enable_language(CUDA) は CUDA 用 Visual Studio Integration を利用して CUDA を検出しているが、
    # CUDA ドライバがインストールできない場所では VS Integration をインストールできないため、
    # enable_language(CUDA) は利用できない。
    # なので（deprecated だけど）FindCUDA を利用してコンパイルする。

    find_package(CUDA REQUIRED)

    set_source_files_properties(
        src/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_codec_cuda.cpp
        third_party/NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
      PROPERTIES
        CUDA_SOURCE_PROPERTY_FORMAT OBJ
    )
    cuda_compile(CUDA_FILES
        src/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_codec_cuda.cpp
        third_party/NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
      OPTIONS
        # VS 2022 の 17.10.x 以上に上げると、ツールセットのバージョンが 14.40 以上になってしまい、以下のエラーが出るため -allow-unsupported-compiler を指定する
        #
        # G:\dev\sora-cpp-sdk\_install\windows_x86_64\release\cuda\include\crt/host_config.h(153): fatal error C1189: #error:  -- unsupported Microsoft Visual Studio version! Only the versions between 20 17 and 2022 (inclusive) are supported! The nvcc flag '-allow-unsupported-compiler' can be used to override this version check; however, using an unsupported host compiler may cause compilation  failure or incorrect run time execution. Use at your own risk. [G:\dev\sora-cpp-sdk\_build\windows_x86_64\release\sora\sora.vcxproj]
        #
        # host_config.h では以下のような記述になっている
        #
        # ```
        # #if _MSC_VER < 1910 || _MSC_VER >= 1940
        #
        # #error -- unsupported Microsoft Visual Studio version! Only the versions between 2017 and 2022 (inclusive) are supported! (...snip...)
        # ```
        #
        # 17.10 は _MSC_VER が 1940 になるため、このエラーが出る。
        #
        # VS のバージョンと _MSC_VER のリストは以下を参照:
        #   https://devblogs.microsoft.com/cppblog/msvc-toolset-minor-version-number-14-40-in-vs-2022-v17-10/
        -allow-unsupported-compiler
        # 更に STL が CUDA 12.4 以上のバージョンを要求するため、STL のバージョンも無視する
        # ref: https://stackoverflow.com/questions/78515942/cuda-compatibility-with-visual-studio-2022-version-17-10
        -Xcompiler /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
        -Xcompiler /utf-8
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/third_party/NvCodec/include
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/third_party/NvCodec/NvCodec
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/include
        -Xcompiler "/MT$<$<CONFIG:Debug>:d>"
        -Xcompiler /D_HAS_ITERATOR_DEBUGGING=0
        -Xcompiler /DUSE_NVCODEC_ENCODER=1
    )
    target_sources(sora PRIVATE ${CUDA_FILES})
    target_include_directories(sora PRIVATE ${CUDA_INCLUDE_DIRS})
    # CUDA を要求したくないので sora.lib に含める
    list(APPEND BUNDLE_STATIC_LIBS ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64/cudart_static.lib)
  endif()

  if (USE_VPL_ENCODER)
    find_package(VPL REQUIRED)
    target_sources(sora
      PRIVATE
        src/hwenc_vpl/vpl_video_codec.cpp
        src/hwenc_vpl/vpl_video_decoder.cpp
        src/hwenc_vpl/vpl_video_encoder.cpp
    )
    target_link_libraries(sora PRIVATE "$<BUILD_INTERFACE:VPL::dispatcher>")
    # vpl を要求したくないので sora.lib に含める
    list(APPEND BUNDLE_STATIC_LIBS ${VPL_ROOT_DIR}/lib/vpl$<$<CONFIG:Debug>:d>.lib)
  endif()

  if (USE_AMF_ENCODER)
    target_include_directories(sora PRIVATE ${AMF_ROOT_DIR}/amf)
    target_sources(sora
      PRIVATE
        ${AMF_ROOT_DIR}/amf/public/common/AMFFactory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/AMFSTL.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamFactory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamFile.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamMemory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/Thread.cpp
        ${AMF_ROOT_DIR}/amf/public/common/TraceAdapter.cpp
        ${AMF_ROOT_DIR}/amf/public/common/Windows/ThreadWindows.cpp
        src/hwenc_amf/amf_video_codec.cpp
        src/hwenc_amf/amf_video_decoder.cpp
        src/hwenc_amf/amf_video_encoder.cpp
    )
  endif()

elseif (SORA_TARGET_OS STREQUAL "macos")
  enable_language(OBJCXX)

  target_sources(sora
    PRIVATE
      src/mac/mac_capturer.mm
      src/mac/mac_version.mm
      src/mac/mac_video_factory.mm
  )

  target_compile_options(sora PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(sora PUBLIC -ObjC)
  set_target_properties(sora PROPERTIES CXX_VISIBILITY_PRESET hidden)

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_MAC
  )

  target_link_libraries(sora
    PUBLIC
      #"-framework Foundation"
      "-framework AVFoundation"
      #"-framework CoreServices"
      #"-framework CoreFoundation"
      #"-framework AudioUnit"
      "-framework AudioToolbox"
      "-framework CoreAudio"
      "-framework QuartzCore"
      #"-framework CoreGraphics"
      "-framework CoreMedia"
      #"-framework CoreVideo"
      "-framework VideoToolbox"
      "-framework AppKit"
      "-framework Metal"
      "-framework MetalKit"
      "-framework OpenGL"
      "-framework IOSurface"
      "-framework ScreenCaptureKit"
  )

elseif (SORA_TARGET_OS STREQUAL "ios")
  enable_language(OBJCXX)

  target_sources(sora
    PRIVATE
      src/mac/mac_audio_output_helper.mm
      src/mac/mac_capturer.mm
      src/mac/mac_version.mm
      src/mac/mac_video_factory.mm
  )

  target_compile_options(sora PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(sora PUBLIC -ObjC)
  set_target_properties(sora PROPERTIES CXX_VISIBILITY_PRESET hidden)

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_MAC
      WEBRTC_IOS
      OPENSSL_IS_BORINGSSL
  )

elseif (SORA_TARGET_OS STREQUAL "android")

  target_sources(sora
    PRIVATE
      src/android/android_video_factory.cpp
      src/android/android_capturer.cpp
  )

  target_compile_options(sora
    PUBLIC
      -fexperimental-relative-c++-abi-vtables
  )

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_LINUX
      WEBRTC_ANDROID
      _LIBCPP_ABI_NAMESPACE=Cr
      _LIBCPP_ABI_VERSION=2
      _LIBCPP_DISABLE_AVAILABILITY
      _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
      _LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS
      _LIBCPP_ENABLE_NODISCARD
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
      # https://github.com/boostorg/container_hash/issues/22 と同じ問題が clang-15 でも起きるので、これを手動で定義して回避する
      BOOST_NO_CXX98_FUNCTION_BASE
  )

elseif (SORA_TARGET_OS STREQUAL "ubuntu")
  target_sources(sora PRIVATE src/v4l2/v4l2_video_capturer.cpp)

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_LINUX
      _LIBCPP_ABI_NAMESPACE=Cr
      _LIBCPP_ABI_VERSION=2
      _LIBCPP_DISABLE_AVAILABILITY
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
      # https://github.com/boostorg/container_hash/issues/22 と同じ問題が clang-15 でも起きるので、これを手動で定義して回避する
      BOOST_NO_CXX98_FUNCTION_BASE
      RTC_ENABLE_H265
  )

  set_target_properties(sora PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(sora
    PRIVATE
      X11
      #Xau
      #Xdmcp
      #Xtst
      #xcb
      #plds4
      #Xext
      #expat
      dl
      #nss3
      #nssutil3
      #plc4
      #nspr4
      rt
      Threads::Threads
  )

  if (USE_NVCODEC_ENCODER)
    target_sources(sora
      PRIVATE
        src/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_codec.cpp
        src/hwenc_nvcodec/nvcodec_video_codec_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_encoder.cpp
        src/hwenc_nvcodec/nvcodec_v4l2_capturer.cpp
        src/hwenc_nvcodec/nvcodec_video_encoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_decoder.cpp
        third_party/NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        third_party/NvCodec/NvCodec/NvEncoder/NvEncoder.cpp
        third_party/NvCodec/NvCodec/NvEncoder/NvEncoderCuda.cpp)
    target_include_directories(sora
      PRIVATE
        third_party/NvCodec/include
        third_party/NvCodec/NvCodec
        /usr/local/cuda/include)

    # これらのソースは CUDA としてコンパイルする
    set_source_files_properties(
        src/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_codec_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_encoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
        third_party/NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        third_party/NvCodec/NvCodec/NvEncoder/NvEncoderCuda.cpp
      PROPERTIES
        COMPILE_OPTIONS "-xcuda;--cuda-gpu-arch=sm_35;-std=gnu++17;-Wno-tautological-constant-out-of-range-compare"
    )

    # CUDA を要求したくないので libsora.a に含める
    list(APPEND BUNDLE_STATIC_LIBS /usr/local/cuda/lib64/libcudart_static.a)
  endif()

  if (USE_VPL_ENCODER)
    find_package(VPL REQUIRED)
    target_include_directories(sora PRIVATE /usr/local/cuda/include)
    target_sources(sora
      PRIVATE
        src/hwenc_vpl/vpl_video_codec.cpp
        src/hwenc_vpl/vpl_video_decoder.cpp
        src/hwenc_vpl/vpl_video_encoder.cpp
    )
    target_link_libraries(sora PRIVATE "$<BUILD_INTERFACE:VPL::dispatcher>")
    # vpl を要求したくないので libsora.a に含める
    list(APPEND BUNDLE_STATIC_LIBS $<TARGET_FILE:VPL::dispatcher>)
  endif()

  if (USE_AMF_ENCODER)
    target_include_directories(sora PRIVATE ${AMF_ROOT_DIR}/amf)
    target_sources(sora
      PRIVATE
        ${AMF_ROOT_DIR}/amf/public/common/AMFFactory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/AMFSTL.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamFactory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamFile.cpp
        ${AMF_ROOT_DIR}/amf/public/common/DataStreamMemory.cpp
        ${AMF_ROOT_DIR}/amf/public/common/Thread.cpp
        ${AMF_ROOT_DIR}/amf/public/common/TraceAdapter.cpp
        ${AMF_ROOT_DIR}/amf/public/common/Linux/ThreadLinux.cpp
        src/hwenc_amf/amf_video_codec.cpp
        src/hwenc_amf/amf_video_decoder.cpp
        src/hwenc_amf/amf_video_encoder.cpp
    )
  endif()

endif()

# 静的ライブラリを sora.lib に含める
if (BUNDLE_STATIC_LIBS)
  bundle_static_library(sora "${BUNDLE_STATIC_LIBS}" bundled_sora)
  add_library(Sora::sora ALIAS bundled_sora)
else()
  add_library(Sora::sora ALIAS sora)
endif()

include(GNUInstallDirs)

install(TARGETS sora
  EXPORT sora-config
  FILE_SET HEADERS
  # Android の場合はライブラリを ANDROID_ABI 以下のディレクトリにインストールする
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/$<$<STREQUAL:${SORA_TARGET},"android">:/${ANDROID_ABI}>
)
install(EXPORT sora-config
  DESTINATION ${CMAKE_INSTALL_DATADIR}/sora
  NAMESPACE Sora::
)
install(
  FILES
    cmake/FindWebRTC.cmake
  DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake
)
install(FILES LICENSE NOTICE.md
  DESTINATION ${CMAKE_INSTALL_PREFIX}
)
if (SORA_TARGET STREQUAL "android")
  install(FILES ${SORA_WEBRTC_LDFLAGS}
    DESTINATION ${CMAKE_INSTALL_DATADIR}
  )
endif()
