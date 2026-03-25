# Intel VPL Simulcast Repro

`hello` を使って Intel VPL + simulcast の disconnect 周辺の問題を再現・確認する手順です。

## 1. 設定ファイルを準備

まず `example` から設定ファイルを作成します。

```bash
cp test/hello_intel_vpl_simulcast_repro.example.json \
   test/hello_intel_vpl_simulcast_repro.json
```

`test/hello_intel_vpl_simulcast_repro.json` を環境に合わせて編集します。

- `signaling_urls`
- `channel_id`

必要に応じて `video_codec_type` を `H265` / `H264` / `VP9` で切り替えて確認します。

## 2. ビルド

通常のビルドだと libsora.a が更新されないことがあります。フォルダを消してやり直すか、
`sora` のバンドル済み静的ライブラリを更新し、それを `hello` に再リンクします。

```bash
cd ~/develop/git/shiguredo/sora-cpp-sdk

_install/ubuntu-24.04_x86_64/release/cmake/bin/cmake \
  --build _build/ubuntu-24.04_x86_64/release/sora \
  --target sora bundled_sora_bundling -j8

cp _build/ubuntu-24.04_x86_64/release/sora/bundled/libsora.a \
   _install/ubuntu-24.04_x86_64/release/sora/lib/libsora.a

_install/ubuntu-24.04_x86_64/release/cmake/bin/cmake \
  --build _build/ubuntu-24.04_x86_64/release/test \
  --target hello -j8
```

## 3. 再現スクリプトを実行

```bash
cd ~/develop/git/shiguredo/sora-cpp-sdk
ulimit -c unlimited

./test/repeat-hello-until-abort.sh \
  _build/ubuntu-24.04_x86_64/release/test/hello \
  test/hello_intel_vpl_simulcast_repro.json \
  20 10 0.5 /tmp/sora-hello-phase-check 0
```

引数の意味は以下です。

1. `hello` バイナリ
2. パラメータ JSON
3. 試行回数
4. 接続維持秒数 (`RUN_SECONDS`)
5. 試行間 sleep 秒
6. ログ出力ディレクトリ
7. `STOP_ON_DETECT` (`1`: 検出時即終了, `0`: 継続)

## 4. 結果の見方

成功時の例:

```text
summary:
  abort during connected : 0
  abort during disconnect: 0
  non-abort failures     : 0
```

失敗時は `abort detected` と `log: .../run-N.log` が出るので、対象ログを確認します。

## 5. ログ確認コマンド例

```bash
rg -n "disconnect|VideoSendStreamImpl::Stop|OnEncodedImage|deque::|Hardening assertion" \
  /tmp/sora-hello-phase-check/run-*.log
```
