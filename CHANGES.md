# 変更履歴

- CHANGE
  - 下位互換のない変更
- UPDATE
  - 下位互換がある変更
- ADD
  - 下位互換がある追加
- FIX
  - バグ修正

## develop

- [CHANGE] Lyra を削除する
  - @enm10k
- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2024.3.0 にあげる
  - @enm10k

## sora-cpp-sdk-2024.2.0

- [CHANGE] momo_sample を sumomo にリネームする
  - @enm10k
- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2024.2.0 にあげる
  - WEBRTC_BUILD_VERSION を m121.6167.3.0 にあげる
  - BOOST_VERSION を 1.84.0 にあげる
  - LYRA_VERSION を 1.3.2 にあげる
  - CMAKE_VERSION を 3.28.1 にあげる
  - SDL2_VERSION を 2.30.0 にあげる
  - CLI11_VERSION を v2.4.1 にあげる
  - @enm10k
- [FIX] sdl_sample で `--audio-codec-lyra-bitrate` が未指定の時に不定な値が送信されるのを修正する
  - SDLSampleConfig.audio_codec_lyra_bitrate の値が初期化されておらず、未指定の時に不定な値が送信されるようになっていた
    - SDLSampleConfig.audio_codec_lyra_bitrate の初期値を 0 に設定する
    - 値が 0 の時、Sora に値は送信されない
  - @miosakuma

## sora-cpp-sdk-2024.1.0

- [CHANGE] JetPack のバージョンを 5.1.2 にあげる
  - JetPack 5.1.1, 5.1.2 で動作を確認
  - JetPack 5.1 では、互換性の問題で JetsonJpegDecoder がエラーになることを確認
  - @enm10k
- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2024.1.0 にあげる
  - @miosakuma

## sora-cpp-sdk-2023.17.0

- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.17.0 にあげる
  - WEBRTC_BUILD_VERSION を m120.6099.1.2 にあげる
  - @miosakuma
- [UPDATE]
  - momo_sample と sdl_sample のビデオコーデックに H265 を指定可能にする
  - @miosakuma

## sora-cpp-sdk-2023.15.0

- [UPDATE]
  - momo_sample に --hw-mjpeg-decoder オプションを追加する
  - @enm10k
- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.15.1 にあげる
  - WEBRTC_BUILD_VERSION を m119.6045.2.1 にあげる
  - CMake を 3.27.7 に上げる
  - @miosakuma

## sora-cpp-sdk-2023.14.0

- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.14.0 にあげる
  - WEBRTC_BUILD_VERSION を m117.5938.2.0 にあげる
  - @torikizi

## sora-cpp-sdk-2023.13.0

- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.13.0 にあげる
  - WEBRTC_BUILD_VERSION を m116.5845.6.1 にあげる
  - @torikizi

## sora-cpp-sdk-2023.9.0

- [UPDATE] Sora C++ SDK を 2023.9.0 にあげる
  - @voluntas @miosakuma
- [UPDATE] WEBRTC_BUILD_VERSION を m115.5790.7.0 にあげる
  - @miosakuma
- [UPDATE] SDL2_VERSION を 2.28.1 にあげる
  - @voluntas

## sora-cpp-sdk-2023.7.0

- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.7.0 にあげる
  - CMAKE_VERSION を 3.26.4 にあげる
  - @torikizi

## sora-cpp-sdk-2023.6.0

- [UPDATE] Sora C++ SDK を 2023.6.0 にあげる
  - @miosakuma
- [UPDATE] WEBRTC_BUILD_VERSION を m114.5735.2.0 にあげる
  - @miosakuma
- [ADD] Windows の momo_sample に --relwithdebinfo オプションを追加する
  - @melpon

## sora-cpp-sdk-2023.5.1

- [UPDATE] Sora C++ SDK を 2023.5.1 にあげる
  - @miosakuma

## sora-cpp-sdk-2023.5.0

- [CHANGE] SoraDefaultClient を削除して SoraClientContext を追加する
  - @melpon
- [UPDATE] JetPack 5.1 に対応する
  - @melpon
- [UPDATE] VERSION のライブラリをアップデートする
  - SORA_CPP_SDK_VERSION を 2023.5.0 にあげる
  - WEBRTC_BUILD_VERSION を m114.5735.0.1 にあげる
  - BOOST_VERSION を 1.82.0 にあげる
  - CMAKE_VERSION を 3.25.0 にあげる
  - SDL2_VERSION を 2.26.5 にあげる
  - CLI11_VERSION を 2.3.2 にあげる
  - @miosakuma
- [UPDATE] CXX_STANDARD を 20 にあげる
  - @miosakuma
- [ADD] デバイス一覧を取得する機能追加する
  - @melpon

## sora-cpp-sdk-2023.2.0

- [UPDATE] Sora C++ SDK を 2023.2.0 にあげる
  - @miosakuma

## sora-cpp-sdk-2023.1.0

- [UPDATE] Sora C++ SDK を 2023.1.0 にあげる
  - @torikizi
