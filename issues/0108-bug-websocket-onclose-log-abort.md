# Websocket::OnClose のログが RTC_LOG 経由で SIGABRT する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-websocket-onclose-log-abort
- Polished: {YYYY-MM-DD}

## 目的

zakuro の E2E テストを CI に組み込んだところ、シグナリング URL を複数指定して実 Sora に接続したときに
zakuro が SIGABRT することが判明した。原因は `sora::Websocket::OnClose` の `RTC_LOG` が
Boost.Beast の `static_string` をそのまま流していることにある。zakuro 側では回避できないため SDK 側で修正する。

## 現状

- `src/websocket.cpp` の `Websocket::OnClose` が次のログを出している

  ```cpp
  RTC_LOG(LS_INFO) << "Websocket::OnClose this=" << (void*)this
                   << " ec=" << ec.message() << " code=" << reason().code
                   << " reason=" << reason().reason;
  ```

- `Websocket::reason()` が返す `boost::beast::websocket::close_reason::reason` は
  `boost::beast::static_string<123, char>` であり、libwebrtc のログに流すには `absl::string_view` への暗黙変換が必要
- libwebrtc の `webrtc_logging_impl::LogStreamer::operator<<` は `const U& arg` を受け取り、その本体内で
  `MakeVal(arg)` を呼ぶ。`MakeVal(const absl::string_view&)` は引数のアドレス `&x` を保持するが、
  暗黙変換で作られる一時オブジェクトは `operator<<` の return 文の full-expression 末で破棄される

  ```cpp
  // libwebrtc の LogStreamer / MakeVal と同じ構造
  template <typename U>
  LogStreamer operator<<(const U& arg) const {
    // ここで暗黙変換が起きると、変換の一時オブジェクトはこの return 文の
    // full-expression 末 (= operator<< を抜けた時点) で破棄される
    return LogStreamer(MakeVal(arg), this);
  }

  // 破棄された一時オブジェクトのアドレスを保持してしまう
  inline Val<LogArgType::kStringView, const absl::string_view*> MakeVal(
      const absl::string_view& x) {
    return {&x};
  }
  ```

- 後から `webrtc_logging_impl::Log()` がこのポインタを辿るため、壊れた `absl::string_view` (data=nullptr, size>0) が
  `std::string::append` に渡り、libc++ の hardening assertion で abort する

  ```
  string:3005: libc++ Hardening assertion __n == 0 || __s != nullptr failed: string::append received nullptr
  ```

- `std::string` / `const char*` / `std::string_view` は `MakeVal` の完全一致になるため安全であり、
  SDK 内でこの条件を踏むのは `Websocket::OnClose` のこの 1 箇所のみ (grep で確認済み)
- 再現する環境と再現しない環境
  - Ubuntu (zakuro の CI と、CI がビルドしたバイナリを Apple container 上で実行した場合) では 100% 再現する
  - macOS では破棄済みスタックが偶然有効な `absl::string_view` を保持するため再現しない
- 再現手順 (zakuro 側)
  - zakuro の `ci.yml` の `pytest` ジョブが、組織シークレットの signaling URL 2 本を使う `test_version` で
    `RuntimeError: zakuro process exited unexpectedly with code -6` で失敗する (3 回連続で再現)
  - ログの末尾は `OnConnect` (2 本目) → `DoClose wss` → 上記 assertion となる
- 最小再現による検証
  - libwebrtc の `LogStreamer` / `MakeVal` と同じ構造のコードを ASan 付きで実行すると `stack-use-after-scope` を検出する
    (破棄されたのは暗黙変換の一時オブジェクト)
  - 同じコードで `reason().reason.c_str()` に相当する形に変えると検出されない
- `Websocket::OnClose` は通常の切断でも通るため、シグナリング URL が 1 本でも同じ未定義動作が発生する。
  abort するかどうかは破棄済みスタックの内容次第であり、SDK を利用するアプリケーションでは潜在的にいつでも落ちうる

## 設計方針

- `Websocket::OnClose` のログで `reason().reason.c_str()` を流すようにする
  (`MakeVal(const char*)` の完全一致になり、一時オブジェクトを経由しなくなる)
- 同種の危険が他に無いか SDK 全体を確認する (現状は該当なし)
- libwebrtc の `MakeVal` が暗黙変換を扱えない問題は上流の課題であり、本 issue では扱わない
- 修正後は zakuro の CI の `pytest` ジョブが成功することを確認する

## 完了条件

- `Websocket::OnClose` のログが暗黙変換を伴わない形になっていること
- Ubuntu で、シグナリング URL を複数指定して実 Sora に接続したときに SIGABRT しないこと
- zakuro の CI の `pytest` ジョブが成功すること
