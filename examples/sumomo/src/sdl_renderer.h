#ifndef SDL_RENDERER_H_
#define SDL_RENDERER_H_

#include <memory>
#include <string>
#include <vector>

// SDL
#include <SDL2/SDL.h>

// Boost
#include <boost/asio.hpp>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <rtc_base/synchronization/mutex.h>

#include "base_renderer.h"

class SDLRenderer : public BaseRenderer {
 public:
  SDLRenderer(int width, int height, bool fullscreen);
  ~SDLRenderer();

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
