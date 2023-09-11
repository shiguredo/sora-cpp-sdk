#ifndef SORA_AUDIO_OUTPUT_HELPER_H_
#define SORA_AUDIO_OUTPUT_HELPER_H_

#include <memory>

namespace sora {

class AudioChangeRouteObserver {
 public:
  virtual ~AudioChangeRouteObserver() {}
  virtual void OnChangeRoute() = 0;
};

class AudioOutputHelperInterface {
 public:
  virtual ~AudioOutputHelperInterface() {}

  virtual bool IsHandsfree() = 0;
  virtual void SetHandsfree(bool enable) = 0;
};

class AudioOutputHelper {
 public:
  AudioOutputHelper(AudioChangeRouteObserver* observer);

  bool IsHandsfree();
  void SetHandsfree(bool enable);

 private:
  std::unique_ptr<AudioOutputHelperInterface> impl_;
};

}  // namespace sora

#endif  // SORA_AUDIO_OUTPUT_HELPER_H_
