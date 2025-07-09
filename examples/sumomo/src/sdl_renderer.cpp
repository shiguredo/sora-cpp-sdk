#include "sdl_renderer.h"

#include <cmath>
#include <csignal>

// WebRTC
#include <api/video/i420_buffer.h>
#include <libyuv/convert_from.h>
#include <libyuv/video_common.h>
#include <rtc_base/logging.h>

#define STD_ASPECT 1.33
#define WIDE_ASPECT 1.78
#define FRAME_INTERVAL (1000 / 30)

SDLRenderer::SDLRenderer(int width, int height, bool fullscreen)
    : BaseRenderer(width, height, 30),
      window_(nullptr),
      renderer_(nullptr),
      dispatch_(nullptr) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": SDL_Init failed " << SDL_GetError();
    return;
  }

  window_ =
      SDL_CreateWindow("Sora C++ SDK - SDL Example", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width, height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (window_ == nullptr) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": SDL_CreateWindow failed "
                      << SDL_GetError();
    return;
  }

  if (fullscreen) {
    SetFullScreen(true);
  }

#if defined(__APPLE__)
  // Apple Silicon Mac + macOS 11.0 だと、
  // SDL_CreateRenderer をメインスレッドで呼ばないとエラーになる
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (renderer_ == nullptr) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": SDL_CreateRenderer failed "
                      << SDL_GetError();
    return;
  }
#endif

  Start();
}

SDLRenderer::~SDLRenderer() {
  Stop();
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

bool SDLRenderer::IsFullScreen() {
  return SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void SDLRenderer::SetFullScreen(bool fullscreen) {
  SDL_SetWindowFullscreen(window_,
                          fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
}

void SDLRenderer::PollEvent() {
  SDL_Event e;
  // 必ずメインスレッドから呼び出す
  while (SDL_PollEvent(&e) > 0) {
    if (e.type == SDL_WINDOWEVENT &&
        e.window.event == SDL_WINDOWEVENT_RESIZED &&
        e.window.windowID == SDL_GetWindowID(window_)) {
      SetSize(e.window.data1, e.window.data2);
    }
    if (e.type == SDL_KEYUP) {
      switch (e.key.keysym.sym) {
        case SDLK_f:
          SetFullScreen(!IsFullScreen());
          break;
        case SDLK_q:
          std::raise(SIGTERM);
          break;
      }
    }
    if (e.type == SDL_QUIT) {
      std::raise(SIGTERM);
    }
  }
}

void SDLRenderer::SetDispatchFunction(
    std::function<void(std::function<void()>)> dispatch) {
  webrtc::MutexLock lock(GetMutex());
  dispatch_ = std::move(dispatch);
}

void SDLRenderer::RenderThreadStarted() {
#if !defined(__APPLE__)
  // Apple 以外の OpenGL あたりの実装だと、
  // SDL_CreateRenderer を描画スレッドと同一のスレッドで呼ばないと何も表示されない
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (renderer_ == nullptr) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": SDL_CreateRenderer failed "
                      << SDL_GetError();
    return;
  }
#endif

  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
}
void SDLRenderer::RenderThreadFinished() {
  SDL_DestroyRenderer(renderer_);
  renderer_ = nullptr;
}

void SDLRenderer::Render(uint8_t* image,
                         int width,
                         int height,
                         const std::vector<SinkInfo>& sink_infos) {
  SDL_RenderClear(renderer_);

  SDL_Surface* surface =
      SDL_CreateRGBSurfaceFrom(image, width, height, 32, width * 4, 0, 0, 0, 0);
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_FreeSurface(surface);

  SDL_Rect image_rect = {0, 0, width, height};
  SDL_Rect draw_rect = {0, 0, width, height};

  // flip (自画像とか？)
  // SDL_RenderCopyEx(renderer_, texture, &image_rect, &draw_rect, 0, nullptr, SDL_FLIP_HORIZONTAL);
  SDL_RenderCopy(renderer_, texture, &image_rect, &draw_rect);

  SDL_DestroyTexture(texture);

  SDL_RenderPresent(renderer_);

  if (dispatch_) {
    dispatch_(std::bind(&SDLRenderer::PollEvent, this));
  }
}
