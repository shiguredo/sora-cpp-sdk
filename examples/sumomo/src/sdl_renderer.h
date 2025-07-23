#ifndef SDL_RENDERER_H_
#define SDL_RENDERER_H_

#include <cstdint>
#include <functional>
#include <vector>

// SDL
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "base_renderer.h"

class SDLRenderer : public BaseRenderer {
 public:
  SDLRenderer(int width, int height, bool fullscreen);
  ~SDLRenderer() override;

  void SetDispatchFunction(std::function<void(std::function<void()>)> dispatch);

  void RenderThreadStarted() override;
  void RenderThreadFinished() override;
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
