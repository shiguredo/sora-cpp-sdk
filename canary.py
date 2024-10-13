import argparse
import re
import subprocess

VERSION_FILE = "VERSION"
EXAMPLES_VERSION_FILE = "examples/VERSION"


def update_version(version_content):
    updated_content = []
    sora_version_updated = False

    for line in version_content:
        if line.startswith("SORA_CPP_SDK_VERSION="):
            version_match = re.match(
                r"SORA_CPP_SDK_VERSION=(\d{4}\.\d+\.\d+)(-canary\.(\d+))?", line
            )
            if version_match:
                major_minor_patch = version_match.group(1)
                canary_suffix = version_match.group(2)
                if canary_suffix is None:
                    new_version = f"{major_minor_patch}-canary.0"
                else:
                    canary_number = int(version_match.group(3))
                    new_version = f"{major_minor_patch}-canary.{canary_number + 1}"

                updated_content.append(f"SORA_CPP_SDK_VERSION={new_version}")
                sora_version_updated = True
            else:
                updated_content.append(line)
        else:
            updated_content.append(line)

    if not sora_version_updated:
        raise ValueError("SORA_CPP_SDK_VERSION not found in VERSION file.")

    return updated_content


def write_version_file(filename, updated_content, dry_run):
    if dry_run:
        print(f"Dry run: The following changes would be written to {filename}:")
        for line in updated_content:
            print(line.strip())
    else:
        with open(filename, "w") as file:
            file.write("\n".join(updated_content) + "\n")
        print(f"{filename} updated.")


def git_operations(dry_run):
    commands = [
        ["git", "commit", "-am", "Update VERSION and examples/VERSION"],
        ["git", "push"],
        [
            "git",
            "tag",
            "-a",
            "v$(grep SORA_CPP_SDK_VERSION VERSION | cut -d '=' -f 2)",
            "-m",
            "Tagging new release",
        ],
    ]

    for command in commands:
        if dry_run:
            print(f"Dry run: Would execute: {' '.join(command)}")
        else:
            print(f"Executing: {' '.join(command)}")
            subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Update VERSION and examples/VERSION file and push changes with git."
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Perform a dry run without making any changes."
    )
    args = parser.parse_args()

    # Read the VERSION file
    with open(VERSION_FILE, "r") as file:
        version_content = file.readlines()

    # Update the VERSION content
    updated_content = update_version(version_content)

    # Write updated content back to VERSION file
    write_version_file(VERSION_FILE, updated_content, args.dry_run)

    # Also update examples/VERSION
    write_version_file(EXAMPLES_VERSION_FILE, updated_content, args.dry_run)

    # Perform git operations
    git_operations(args.dry_run)


if __name__ == "__main__":
    main()
