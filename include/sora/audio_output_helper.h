#ifndef SORA_AUDIO_OUTPUT_HELPER_H_
#define SORA_AUDIO_OUTPUT_HELPER_H_

#include <memory>

namespace sora {

class AudioChangeRouteObserver {
 public:
  virtual ~AudioChangeRouteObserver() {}
  /**
   * マイクやスピーカーのデバイス変更があった場合に呼び出される
   * 
   * マイクやスピーカーの設定が変更された場合にも呼び出されることがあるため注意
   * IsHandsfree と併用することが望ましい
   * 
   * メモ(tnoho)
   * iOS の audioSessionDidChangeRoute で発火させている
   * audioSessionDidChangeRoute のコールバック引数自体が現在の状態を伝えないため引数なしとした
   */
  virtual void OnChangeRoute() = 0;
};

class AudioOutputHelperInterface {
 public:
  virtual ~AudioOutputHelperInterface() {}

  /**
   * マイクとスピーカーがハンズフリーに使う組み合わせである内蔵スピーカーと内蔵マイクであることを確認する
   * 
   * メモ(tnoho)
   * iOS で AVAudioSessionPortOverrideSpeaker に設定した際のデバイスの組み合わせであることを確認している
   */
  virtual bool IsHandsfree() = 0;
  /**
   * マイクとスピーカーをハンズフリーに使う組み合わせである内蔵スピーカーと内蔵マイクに切り替える
   * 
   * メモ(tnoho)
   * iOS で AVAudioSessionPortOverrideSpeaker に設定した際にはハンズフリーに使う組み合わせである内蔵スピーカーと内蔵マイクに切り替える
   * AVAudioSessionPortOverrideSpeaker にして内蔵スピーカーと内蔵マイクに切り替えるか
   * AVAudioSessionPortOverrideNone で受話用スピーカーと内蔵マイク、もしくはその他のデバイスに戻すか
   * の二択しかないため粒度を合わせることにした
   */
  virtual void SetHandsfree(bool enable) = 0;
};

std::unique_ptr<AudioOutputHelperInterface> CreateAudioOutputHelper(
    AudioChangeRouteObserver* observer);

}  // namespace sora

#endif  // SORA_AUDIO_OUTPUT_HELPER_H_
