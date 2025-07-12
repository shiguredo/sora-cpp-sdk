#include "sdl_renderer.h"

#include <csignal>

// WebRTC
#include <rtc_base/logging.h>

SDLRenderer::SDLRenderer(int width, int height, bool fullscreen)
    : BaseRenderer(width, height, 30),
      window_(nullptr),
      renderer_(nullptr),
      dispatch_(nullptr) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": SDL_Init failed " << SDL_GetError();
    return;
  }

  window_ = SDL_CreateWindow("Sora C++ SDK - SDL Example", width, height,
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
  renderer_ = SDL_CreateRenderer(window_, NULL);
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
  return SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN;
}

void SDLRenderer::SetFullScreen(bool fullscreen) {
  SDL_SetWindowFullscreen(window_, fullscreen);
  if (fullscreen) {
    SDL_HideCursor();
  } else {
    SDL_ShowCursor();
  }
}

void SDLRenderer::PollEvent() {
  SDL_Event e;
  // 必ずメインスレッドから呼び出す
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_WINDOW_RESIZED &&
        e.window.windowID == SDL_GetWindowID(window_)) {
      SetSize(e.window.data1, e.window.data2);
    }
    if (e.type == SDL_EVENT_KEY_UP) {
      switch (e.key.key) {
        case SDLK_F:
          SetFullScreen(!IsFullScreen());
          break;
        case SDLK_Q:
          std::raise(SIGTERM);
          break;
      }
    }
    if (e.type == SDL_EVENT_QUIT) {
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
  renderer_ = SDL_CreateRenderer(window_, NULL);
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

  SDL_Surface* surface = SDL_CreateSurfaceFrom(
      width, height, SDL_PIXELFORMAT_ARGB8888, image, width * 4);
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_DestroySurface(surface);

  SDL_FRect image_rect = {0, 0, (float)width, (float)height};
  SDL_FRect draw_rect = {0, 0, (float)width, (float)height};

  SDL_RenderTexture(renderer_, texture, &image_rect, &draw_rect);

  SDL_DestroyTexture(texture);

  SDL_RenderPresent(renderer_);

  if (dispatch_) {
    dispatch_(std::bind(&SDLRenderer::PollEvent, this));
  }
}
