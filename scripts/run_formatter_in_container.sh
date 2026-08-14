#!/bin/bash
# .github/workflows/formatter.yml 相当のチェックを Apple container を使ってローカルで実行するスクリプト
#
# 使用方法:
#   make format-check
#   または
#   ./scripts/run_formatter_in_container.sh
#
# 事前準備:
#   - Apple Silicon Mac であること
#   - container コマンドがインストール済みであること
#     https://github.com/apple/container
#   - container system start が実行済みであること
#
# このスクリプトは以下を実行します:
#   1. Ubuntu 24.04 コンテナを起動
#   2. 依存パッケージをインストール (llvm.sh で clang-21 を導入)
#   3. python3 run.py build --disable-cuda --test ubuntu-24.04_x86_64
#   4. python3 run.py iwyu ubuntu-24.04_x86_64
#   5. python3 run.py format
#   6. git diff --exit-code で差分チェック
#
# リソースについて:
#   デフォルトでは --arch amd64 --cpus 4 --memory 4g で実行します。
#   マシンのスペックに応じて CONTAINER_ARCH / CONTAINER_CPUS / CONTAINER_MEMORY で調整してください。
#   --arch amd64 は Rosetta 2 経由で x86_64 コンテナを実行します。

set -eu

# container コマンドの存在確認
if ! command -v container &> /dev/null; then
  echo "Error: 'container' command not found."
  echo "Please install from https://github.com/apple/container"
  exit 1
fi

# リポジトリルートに移動
cd "$(dirname "$0")/.."

LLVM_VERSION=21
IMAGE="ubuntu:24.04"
CONTAINER_ARCH="${CONTAINER_ARCH:-amd64}"
CONTAINER_CPUS="${CONTAINER_CPUS:-4}"
CONTAINER_MEMORY="${CONTAINER_MEMORY:-4g}"

echo "==> Pulling ${IMAGE} (${CONTAINER_ARCH}) image..."
container image pull --arch "${CONTAINER_ARCH}" "${IMAGE}"

echo "==> Running formatter check in container..."
echo "    Arch: ${CONTAINER_ARCH}, CPU: ${CONTAINER_CPUS}, Memory: ${CONTAINER_MEMORY}"

container run \
  --rm \
  --arch "${CONTAINER_ARCH}" \
  --cpus "${CONTAINER_CPUS}" \
  --memory "${CONTAINER_MEMORY}" \
  -v "$(pwd):/work" \
  -w /work \
  "${IMAGE}" \
  bash -eux -c "
    apt-get update
    apt-get install -y software-properties-common wget git build-essential

    # X11 / OpenGL (ビルドに必要)
    apt-get install -y libx11-dev libxext-dev libgl-dev

    # clang-21 と clang-format-21 のインストール
    wget https://apt.llvm.org/llvm.sh
    chmod a+x llvm.sh
    ./llvm.sh ${LLVM_VERSION}
    apt install -y clang-format-${LLVM_VERSION}

    # ninja (cmake のビルドに必要)
    apt-get install -y ninja-build

    # ビルド
    python3 run.py build --disable-cuda --test ubuntu-24.04_x86_64

    # IWYU (include-what-you-use) チェック
    python3 run.py iwyu ubuntu-24.04_x86_64

    # clang-format チェック
    python3 run.py format

    # 差分チェック
    if ! git diff --exit-code; then
      echo ''
      echo '========================================='
      echo '  Check failed'
      echo '  There are diffs from IWYU or clang-format'
      echo '  Run the following command locally to fix:'
      echo '    python3 run.py format'
      echo '========================================='
      # Revert changes (to avoid ownership changes since running as root in container)
      git checkout -- .
      exit 1
    fi

    echo 'All checks passed'
  "
