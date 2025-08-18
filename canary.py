import argparse
import re
import subprocess

VERSION_FILE = "VERSION"
EXAMPLES_DEPS_FILE = "examples/DEPS"


def update_sdk_version(version_content):
    # VERSION ファイルはバージョン番号のみを含む
    version_str = version_content.strip()
    
    version_match = re.match(
        r"(\d{4}\.\d+\.\d+)(-canary\.(\d+))?", version_str
    )
    if version_match:
        major_minor_patch = version_match.group(1)
        canary_suffix = version_match.group(2)
        if canary_suffix is None:
            new_version = f"{major_minor_patch}-canary.0"
        else:
            canary_number = int(version_match.group(3))
            new_version = f"{major_minor_patch}-canary.{canary_number + 1}"
        
        return new_version, new_version
    else:
        raise ValueError(f"Invalid version format in VERSION file: {version_str}")


def write_version_file(filename, updated_content, dry_run):
    if dry_run:
        print(f"Dry run: The following changes would be written to {filename}:")
        print(updated_content)
    else:
        with open(filename, "w") as file:
            file.write(updated_content)
        print(f"{filename} updated.")


def update_deps_version(deps_content, new_version):
    """DEPS ファイルの SORA_CPP_SDK_VERSION を更新する"""
    lines = deps_content.split('\n')
    updated_lines = []
    for line in lines:
        if line.startswith('SORA_CPP_SDK_VERSION='):
            updated_lines.append(f'SORA_CPP_SDK_VERSION={new_version}')
        else:
            updated_lines.append(line)
    return '\n'.join(updated_lines)


def git_operations(new_version, dry_run):
    if dry_run:
        print("Dry run: Would execute git commit -am '[canary] Update VERSION and examples/DEPS'")
        print(f"Dry run: Would execute git tag {new_version}")
        print("Dry run: Would execute git push")
        print(f"Dry run: Would execute git push origin {new_version}")
    else:
        print("Executing: git commit -am '[canary] Update VERSION and examples/DEPS'")
        subprocess.run(["git", "commit", "-am", "[canary] Update VERSION and examples/DEPS"], check=True)

        print(f"Executing: git tag {new_version}")
        subprocess.run(["git", "tag", new_version], check=True)

        print("Executing: git push")
        subprocess.run(["git", "push"], check=True)

        print(f"Executing: git push origin {new_version}")
        subprocess.run(["git", "push", "origin", new_version], check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Update VERSION and examples/DEPS file and push changes with git."
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Perform a dry run without making any changes."
    )
    args = parser.parse_args()

    # Read and update the VERSION file
    with open(VERSION_FILE, "r") as file:
        version_content = file.read()
    updated_version_content, new_version = update_sdk_version(version_content)
    write_version_file(VERSION_FILE, updated_version_content, args.dry_run)

    # Read and update the examples/DEPS file
    with open(EXAMPLES_DEPS_FILE, "r") as file:
        examples_deps_content = file.read()
    updated_examples_deps_content = update_deps_version(examples_deps_content, new_version)
    write_version_file(EXAMPLES_DEPS_FILE, updated_examples_deps_content, args.dry_run)

    # Perform git operations
    git_operations(new_version, args.dry_run)


if __name__ == "__main__":
    main()
