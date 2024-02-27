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
            //メインスレッドでない場合はメインスレッドで再実行
            if (!SoraThreadUtils.runOnMainThread(this::OnChangeRoute)) {
                return;
            }

            if (this.observer == null) {
                return;
            }
            this.observer.OnChangeRoute();
        }
    }

    MainThreadWrapper(Context context) {
        //メインスレッドでない場合はメインスレッドで再実行
        if (!SoraThreadUtils.runOnMainThread(() -> {
            this.soraAudioManager = SoraAudioManagerFactory.create(context);
        })) {
            return;
        }
        this.soraAudioManager = SoraAudioManagerFactory.create(context);
    }

    @Override
    public void start(OnChangeRouteObserver observer) {
        //メインスレッドでない場合はメインスレッドで再実行
        if (!SoraThreadUtils.runOnMainThread(() -> start(observer))) {
            return;
        }
        soraAudioManager.start(new OnChangeRouteObserverWrapper(observer));
    }

    @Override
    public void stop() {
        //メインスレッドでない場合はメインスレッドで再実行
        if (!SoraThreadUtils.runOnMainThread(this::stop)) {
            return;
        }
        soraAudioManager.stop();
    }

    @Override
    public void setHandsfree(boolean on) {
        //メインスレッドでない場合はメインスレッドで再実行
        if (!SoraThreadUtils.runOnMainThread(() -> setHandsfree(on))) {
            return;
        }
        soraAudioManager.setHandsfree(on);
    }

    @Override
    public boolean isHandsfree() {
        return soraAudioManager.isHandsfree();
    }
}
