# ビルド

様々なプラットフォームへ対応しているため、ビルドがとても複雑です。

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

github actions でビルドを行い確認していますので、まずは github actions の [build.yml](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

github actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は discord へご連絡ください。

## サンプルのビルド

> [!important]
> ここでは macos arm64 でのビルドを想定しています

サンプルをビルドする際、二つのパターンでビルドすることができます。

- github に公開されているバイナリの sdk を利用する
- ローカルでビルドした sdk を利用する

ここでは一通りの機能を実装しているサンプルである sumomo を例にします。
また、ビルド用の `run.py` はプラットフォーム毎に用意されています。

### github に公開されているバイナリの sdk を利用する

```bash
python3 examples/sumomo/macos_arm64/run.py
```

### ローカルでビルドした sdk を利用する

```bash
python3 examples/sumomo/macos_arm64/run.py --sora-dir .
```

ローカルでビルドした sdk のルートディレクトリを `--sora-dir` にて指定してください。

> [!note]
> `--sora-dir` を指定した際は examples 以下の `VERSION` ファイルを利用しません。
