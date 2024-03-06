package jp.shiguredo.sora.audiomanager;

public interface SoraAudioManager {
    interface OnChangeRouteObserver {
        // オーディオデバイスの変更を通知するコールバック
        void OnChangeRoute();
    }

    /*
     * オーディオの制御を開始する
     * Java は destructor がないので start - stop にする
     */
    void start(OnChangeRouteObserver observer);

    // オーディオの制御を終了する
    void stop();

    // ハンズフリーに設定する
    void setHandsfree(boolean on);

    // ハンズフリーかを確認する
    boolean isHandsfree();
}
