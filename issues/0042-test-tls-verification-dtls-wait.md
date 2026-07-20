# TLS 検証 E2E テストの DTLS 接続アサーションに待機を追加する

- Priority: Medium
- Created: 2026-07-20
- Completed: {YYYY-MM-DD}
- Model: qwen3.8-max-preview
- Branch: feature/fix-tls-test-dtls-wait
- Polished: 2026-07-20

## 目的

`e2e-test/test_sumomo_tls_verification.py` の成功系テストが DTLS ハンドシェイク完了を待たずに `dtlsState == "connected"` をアサートしており、CI 環境でフレーキーになるリスクを解消する。

## 優先度根拠

CI の安定性に直結する。CI 負荷時に DTLS ハンドシェイクが `_wait_for_startup` の既存待機時間（`initial_wait=2` 秒 + HTTP ポーリング）内に完了しない場合、`dtlsState` が `"connecting"` のままアサーションが失敗する。ランダムに失敗するテストは開発生産性を下げるため Medium。

## 現状

成功系 3 テスト（`test_tls_system_ca_success:101-106`、`test_tls_insecure_skips_verification:187-193`、`test_tls_correct_ca_cert_success:219-225`）は `with sumomo:` 直後に `get_stats()` → `dtlsState == "connected"` をアサートしている。

`_wait_for_startup`（`sumomo.py:745-851`）が保証するのは HTTP `/stats` の 200 応答であり、DTLS ハンドシェイク完了ではない。`__enter__` 内で `initial_wait=2` 秒 + 1 秒間隔の HTTP ポーリングが実行されるため、`with` ブロック進入時点で最低 2〜3 秒は経過している。しかし DTLS ハンドシェイクは HTTP エンドポイントの応答と並行して進むため、CI 高負荷時には 2〜3 秒で完了しない可能性がある。

なお `test_sumomo_basic.py:34` の `time.sleep(3)` は sendonly/recvonly 両ピア起動後のメディアフロー確立（`packetsSent > 0` 等）を待つものであり、DTLS 完了だけを目的とした待機ではない。

### 0005 との関係

`issues/0005-test-apply-pytest-retry-to-flaky-tests.md` は pytest-retry による症状緩和であり、対象 6 ファイルに `test_sumomo_tls_verification.py` は含まれていない。本 issue は根本原因（待機不足）の除去であり、0005 とは補完関係にある。0005 が解決しても本 issue は不要にならない。

## 設計方針

`dtlsState` が `"connected"` になるまでポーリングするリトライロジックを追加する。`_wait_for_startup` が既に 2〜3 秒待っているため、固定 `time.sleep(3)` の追加は合計 5〜6 秒の固定待機となり CI 実行時間を無駄に伸ばす。ポーリング方式なら DTLS が完了した時点で即座に次に進める。

ポーリングの仕様:

- 配置場所: `e2e-test/helper.py` に `wait_for_dtls_connected(sumomo, timeout=10, interval=0.5)` として共通関数を置く。既存の `helper.py` は stats の dict を受け取る純粋関数の集まりだが、本関数は `Sumomo` オブジェクトを受け取り HTTP リクエストを伴う。`Sumomo` クラスをテスト観点で肥大化させないため、stats ポーリングは stats ユーティリティの延長として `helper.py` に置く。`test_sumomo_basic.py`（4 箇所）、`test_sumomo_device.py`（6 箇所）、`test_sumomo_apple_video_toolbox.py`、`test_sumomo_nvidia_video_codec.py`、`test_sumomo_amd_amf.py`、`test_sumomo_raspberry_pi.py`、`test_sumomo_intel_vpl.py` にも同一の `dtlsState == "connected"` アサーションが存在し、将来これらもポーリングに移行できる
- 責務: 関数内で `assert` し、戻り値は `None`。既存の純粋関数パターンからの意図的な逸脱である
- タイムアウト: 10 秒（deadline 管理に `get_stats()` の所要時間を含む。`get_stats()` は HTTP タイムアウト 10 秒（`sumomo.py:525`）のため、1 回の呼び出しが最大 10 秒ブロックする可能性がある。`interval` は応答後の追加スリープである）
- ポーリング間隔: 0.5 秒（`get_stats()` 応答後の追加待機）
- 早期離脱: `dtlsState` が `"failed"` または `"closed"` になった場合は即座にアサーションエラー（タイムアウトまで待たない）
- `get_transport()` が `None` を返す場合: transport エントリがまだ stats に出現していないため、リトライ継続（`"connecting"` 相当の扱い）
- タイムアウト時: 最後の `dtlsState` の値を含むアサーションメッセージで失敗

置き換え後のテストコードの形:

```python
# 置き換え前
stats = sumomo.get_stats()
transport = get_transport(stats)
assert transport is not None, "transport が取得できない"
assert transport["dtlsState"] == "connected", (
    f"DTLS が未接続: {transport['dtlsState']}"
)

# 置き換え後
wait_for_dtls_connected(sumomo)
```

`from helper import get_transport` の import は、他のアサーションで使わないテストでは削除する。

### スコープ外

`test_sumomo_basic.py` と `test_sumomo_device.py` の同一パターンは本 issue の修正対象外。これらは `time.sleep(3)` が事実上の DTLS 待機を兼ねており、TLS 検証テストほどフレーキーリスクが高くない。将来のポーリング移行は別 issue で扱う。

失敗系 2 テスト（`test_tls_invalid_ca_cert_fails`、`test_tls_empty_ca_cert_fails`）は `_wait_for_startup` 内でプロセスが異常終了するため本変更の影響を受けない。

## 完了条件

- `helper.py` に `wait_for_dtls_connected` 共通関数が追加されている
- 成功系 3 テストに DTLS 完了をポーリングする処理が追加されている
- `dtlsState == "failed"` または `"closed"` 時の早期離脱が実装されている
- 既存の E2E テスト（`test_sumomo_basic.py`、`test_sumomo_tls_verification.py`、`test_sumomo_device.py`）が全通過する
