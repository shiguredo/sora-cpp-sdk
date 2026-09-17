#ifndef SDL_RENDERER_H_
#define SDL_RENDERER_H_

#include <cstdint>
#include <functional>
#include <vector>

// SDL3
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

// Sora C++ SDK
#include <sora/renderer/base_renderer.h>

// 枠割りと映像の合成は sora::BaseRenderer に任せ、このクラスは
// SDL のウィンドウとテクスチャへの描画だけを担当する。
class SDLRenderer : public sora::BaseRenderer {
 public:
  SDLRenderer(int width, int height, bool fullscreen);
  ~SDLRenderer() override;

  void SetDispatchFunction(std::function<void(std::function<void()>)> dispatch);

  // BaseRenderer から描画スレッドの開始時と終了時に呼ばれる。
  // SDL のレンダラは描画スレッドと同一のスレッドで生成・破棄する必要がある。
  void RenderThreadStarted() override;
  void RenderThreadFinished() override;
  // BaseRenderer が合成したキャンバスを SDL のテクスチャとして描画する。
  void Render(uint8_t* image,
              int width,
              int height,
              const std::vector<SinkInfo>& sink_infos) override;

 private:
  bool IsFullScreen();
  void SetFullScreen(bool fullscreen);
  void PollEvent();

  SDL_Window* window_;
  SDL_Renderer* renderer_;
  std::function<void(std::function<void()>)> dispatch_;
};

#endif
