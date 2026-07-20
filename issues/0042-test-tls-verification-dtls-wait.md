# TLS 検証 E2E テストの DTLS 接続アサーションに待機を追加する

- Priority: Medium
- Created: 2026-07-20
- Completed: {YYYY-MM-DD}
- Model: qwen3.8-max-preview
- Branch: feature/fix-tls-test-dtls-wait
- Polished: {YYYY-MM-DD}

## 目的

`e2e-test/test_sumomo_tls_verification.py` の成功系テストが DTLS ハンドシェイク完了を待たずに `dtlsState == "connected"` をアサートしており、CI 環境でフレーキーになるリスクを解消する。

## 優先度根拠

CI の安定性に直結する。`test_sumomo_basic.py` は `time.sleep(3)` で DTLS 完了を待っているが、TLS 検証テストにはその待機がない。CI 負荷時にランダムに失敗するテストは開発生産性を下げるため Medium。

## 現状

成功系 3 テスト（`test_tls_system_ca_success:101-106`、`test_tls_insecure_skips_verification:187-193`、`test_tls_correct_ca_cert_success:219-225`）は `with sumomo:` 直後に `get_stats()` → `dtlsState == "connected"` をアサートしている。

`_wait_for_startup` が保証するのは HTTP `/stats` の 200 応答であり、DTLS ハンドシェイク完了ではない。既存の `test_sumomo_basic.py:34` は `time.sleep(3)` で DTLS 完了を待っている。

## 設計方針

`test_sumomo_basic.py` と同様に、`with sumomo:` 進入後に `time.sleep(3)` を挿入する。または `dtlsState` が `"connected"` になるまでポーリングするリトライロジックを追加する（より堅牢だが実装コストが高い）。

## 完了条件

- 成功系 3 テストに DTLS 完了を待つ処理が追加されている
- CI で 10 回連続でフレーキーなく通過する
