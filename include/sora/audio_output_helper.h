#ifndef SORA_AUDIO_OUTPUT_HELPER_H_
#define SORA_AUDIO_OUTPUT_HELPER_H_

#include <memory>

/**
 * PR 概要より引用
 * iOS 向けに音声出力先変更機能を追加します。基本的にはアプリの場合は RTCAudioSession で直接行えば良いと言う考えから、
 * あくまで Unity での利用にフォーカスしたヘルパーとしての立ち位置。
 *
 * include/sora/audio_output_helper.h の AudioOutputHelper クラスがメインで以下の機能を提供している
 * - IsHandsfree()
 *   - 今ハンズフリーモードかを確認する。
 * - SetHandsfree(bool enable)
 *   - ハンズフリーモードにする。もしくは解除する。
 * 
 * 状態変化発生時は AudioChangeRouteObserver::OnDidChangeRoute が発火する
 * これに引数がないのは AudioSessionDelegate の audioSessionDidChangeRoute に previousRoute しか存在しないことに由来する。
 * OnDidChangeRoute 内で IsHandsfree を呼んで使ってもらえばと考えている。
 *
 * SetHandsfree は output が AVAudioSessionPortBuiltInSpeaker、 input が AVAudioSessionPortBuiltInMic にする関数なので、
 * それに対応して、現在の output と input がそうなっていることをもって IsHandsfree は true を返す。
 *
 * hello アプリケーションをビルドすることで挙動が確認できるよう hello アプリケーションを改変してある。
 *
 * ただし AirPods などでは SetHandsfree(false) をしても1秒に満たない程度の時間で、
 * ハンズフリーが解除される挙動を示すため実装時には注意が必要そう。
 * OS のデバイス切り替えも同様の挙動を示すので、これが正なのだが、
 * UI 変更は OnDidChangeRoute イベント発火時に行うという実装が適当だと思わる。
 *
 * iOS がアプリケーションからできる出力先変更の最大がこのレベルになるので、
 * Android 実装時にも同様のレベルに揃えることを想定して AudioOutputHelperInterface を用意しクロスプラットフォームへの準備をしてありる。
 * IsHandsfree, SetHandsfree ともに static にできるのに static にしてないのは、 Android が static にできるかわからないため。    
*/

/**
* PR コメントより引用
* SetHandsfree でやってるのは overrideOutputAudioPort: AVAudioSessionPortOverrideSpeaker で、
* 呼んでいる関数では、さもスピーカーだけ変更しているように見えるが。
* こちらのドキュメントに記載されているとおり出力先を内蔵スピーカーに切り替えるだけでなく、
* マイクも内蔵マイクに切り替えている。
* 
* スピーカーは内蔵スピーカーだけどマイクはイヤホンのマイクというチグハグにならないようにするための実装だと思うが、
* マイクも切り替わっているのに SetUseLoudSpeaker というのも変で、
* UseBuiltinAudioInOut だと受話用のスピーカーか外向けのスピーカーかがわからなくなってしまうので、
* 内蔵スピーカーと内蔵マイクの組み合わせを端的に表せる Handsfree というワードを使いました。
* 
* Android についても共通挙動にしようと考えているため、OS の API の命名に拘らず機能の実態に近い名前にした。
* 
* また、 IsHandsfree については上記の理由からデバイスを確認して、
* 内蔵スピーカーと内蔵マイクの組み合わせを使っていることをもって、ハンズフリーモードであると返すようにしてある。
*/

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
