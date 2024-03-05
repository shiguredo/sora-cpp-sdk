package jp.shiguredo.sora.audiomanager;

import android.content.Context;

class MainThreadWrapper implements SoraAudioManager {
    private SoraAudioManager soraAudioManager;

    private static class OnChangeRouteObserverWrapper implements OnChangeRouteObserver {
        OnChangeRouteObserver observer;

        public OnChangeRouteObserverWrapper(OnChangeRouteObserver observer) {
            this.observer = observer;
        }

        public void OnChangeRoute() {
            SoraThreadUtils.runOnMainThread(() -> {
                if (this.observer == null) {
                    return;
                }
                this.observer.OnChangeRoute();
            });
        }
    }

    MainThreadWrapper(Context context) {
        SoraThreadUtils.runOnMainThread(() -> {
            this.soraAudioManager = SoraAudioManagerFactory.create(context);
        });
    }

    @Override
    public void start(OnChangeRouteObserver observer) {
        SoraThreadUtils.runOnMainThread(() -> {
            soraAudioManager.start(new OnChangeRouteObserverWrapper(observer));
        });
    }

    @Override
    public void stop() {
        SoraThreadUtils.runOnMainThread(() -> {
            soraAudioManager.stop();
        });
    }

    @Override
    public void setHandsfree(boolean on) {
        SoraThreadUtils.runOnMainThread(() -> {
            soraAudioManager.setHandsfree(on);
        });
    }

    @Override
    public boolean isHandsfree() {
        return soraAudioManager.isHandsfree();
    }
}
