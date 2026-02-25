import argparse
import re
import subprocess

VERSION_FILE = "VERSION"
EXAMPLES_DEPS_FILE = "examples/DEPS"


def increment_version(version_str: str) -> str:
    """バージョン文字列をインクリメントする"""
    match = re.match(r"(\d{4})\.(\d+)\.(\d+)(-canary\.(\d+))?", version_str)
    if not match:
        raise ValueError(f"Invalid version format: {version_str}")

    year, minor, patch, _, canary = match.groups()

    if canary:
        # canary がある場合は canary 番号をインクリメント
        new_version = f"{year}.{minor}.{patch}-canary.{int(canary) + 1}"
    else:
        # canary がない場合はマイナーバージョンを上げてパッチを 0 にする
        new_version = f"{year}.{int(minor) + 1}.0-canary.0"

    return new_version


def confirm_update(current_version: str, new_version: str) -> bool:
    """ユーザーに更新の確認を求める"""
    response = (
        input(f"Update version from {current_version} to {new_version}? (y/n): ")
        .strip()
        .lower()
    )
    return response == "y"


def check_current_branch(expected_branch: str) -> bool:
    """現在のブランチが期待するブランチかを確認する"""
    result = subprocess.run(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True
    )
    current_branch = result.stdout.strip()
    return current_branch == expected_branch


def update_deps_version(deps_content: str, new_version: str) -> str:
    """DEPS ファイルの SORA_CPP_SDK_VERSION を更新する"""
    lines = deps_content.split("\n")
    updated_lines = []
    sdk_version_updated = False
    for line in lines:
        line = line.strip()
        if line.startswith("SORA_CPP_SDK_VERSION="):
            updated_lines.append(f"SORA_CPP_SDK_VERSION={new_version}")
            sdk_version_updated = True
        else:
            updated_lines.append(line)
    if not sdk_version_updated:
        raise ValueError("SORA_CPP_SDK_VERSION not found in DEPS file.")
    return "\n".join(updated_lines)


def main():
    parser = argparse.ArgumentParser(
        description="Update VERSION and examples/DEPS file and push changes with git."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Perform a dry run without making any changes.",
    )
    args = parser.parse_args()

    # develop ブランチかを確認
    if not check_current_branch("develop"):
        print("This script should be run on the 'develop' branch.")
        return

    # 現在のバージョンを読み込む
    with open(VERSION_FILE, "r") as file:
        current_version = file.read().strip()

    new_version = increment_version(current_version)

    # dry-run の場合は実行予定の内容を表示して終了
    if args.dry_run:
        print(
            f"Dry run: VERSION would be updated from {current_version} to {new_version}"
        )
        print(
            f"Dry run: {EXAMPLES_DEPS_FILE} SORA_CPP_SDK_VERSION would be updated to {new_version}"
        )
        print(
            "Dry run: git commit -m '[canary] バージョンを上げる' would be executed"
        )
        print(f"Dry run: git tag {new_version} would be executed")
        print("Dry run: git push -u origin develop --tags would be executed")
        return

    # ユーザーに確認
    if not confirm_update(current_version, new_version):
        print("Update cancelled.")
        return

    # VERSION ファイルを更新
    with open(VERSION_FILE, "w") as file:
        file.write(new_version + "\n")
    print(f"{VERSION_FILE} updated.")

    # examples/DEPS ファイルを更新
    with open(EXAMPLES_DEPS_FILE, "r") as file:
        deps_content = file.read()
    updated_deps_content = update_deps_version(deps_content, new_version)
    with open(EXAMPLES_DEPS_FILE, "w") as file:
        file.write(updated_deps_content.rstrip() + "\n")
    print(f"{EXAMPLES_DEPS_FILE} updated.")

    # git 操作
    subprocess.run(["git", "add", VERSION_FILE, EXAMPLES_DEPS_FILE])
    subprocess.run(["git", "commit", "-m", "[canary] バージョンを上げる"])
    subprocess.run(["git", "tag", new_version])
    subprocess.run(["git", "push", "-u", "origin", "develop", "--tags"])


if __name__ == "__main__":
    main()
