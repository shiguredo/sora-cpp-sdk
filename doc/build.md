# ビルド

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

## サンプルのビルド

サンプルをビルドする際、二つのパターンでビルドすることができます。

- GitHub に公開されているバイナリの SDK を利用する
- ローカルでビルドした SDK を利用する

ここでは一通りの機能を実装しているサンプルである sumomo を例にします。

### GitHub に公開されているバイナリの SDK を利用する

```bash
$ python3 examples/sumomo/macos_arm64/run.py
```

### ローカルでビルドした SDK を利用する

```bash
$ python3 examples/sumomo/macos_arm64/run.py --sora-dir .
```

ローカルでビルドした SDK を `--sora-dir` にて指定してください。
