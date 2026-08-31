#!/usr/bin/env python3
"""
CUDA Version Manager

A utility for managing CUDA redistributable versions in the setup-cuda GitHub Action.
Uses only Python standard library (no external dependencies).

Usage:
    python cuda-version-manager.py list-versions
    python cuda-version-manager.py show-components 13.0.2
    python cuda-version-manager.py validate 13.0.2
    python cuda-version-manager.py add-version 13.1.1
    python cuda-version-manager.py add-component libcublas libcusolver
    python cuda-version-manager.py add-component libcublas --no-required --min-version 13.0.0
    python cuda-version-manager.py remove-component libcublas
    python cuda-version-manager.py check-updates
    python cuda-version-manager.py urls 13.0.2 --platform windows-x86_64
"""

import argparse
import json
import os
import re
import sys
import urllib.request
import urllib.error
from pathlib import Path
from typing import Optional

# NVIDIA redistributable base URL
REDIST_BASE_URL = "https://developer.download.nvidia.com/compute/cuda/redist"

# Path to components.json relative to this script
SCRIPT_DIR = Path(__file__).parent
COMPONENTS_FILE = SCRIPT_DIR.parent / "actions" / "setup-cuda" / "components.json"

# ANSI color codes (disabled if not a TTY)
USE_COLOR = sys.stdout.isatty()


def color(text: str, code: str) -> str:
    """Apply ANSI color code if terminal supports it."""
    if not USE_COLOR:
        return text
    return f"\033[{code}m{text}\033[0m"


def green(text: str) -> str:
    return color(text, "32")


def red(text: str) -> str:
    return color(text, "31")


def yellow(text: str) -> str:
    return color(text, "33")


def cyan(text: str) -> str:
    return color(text, "36")


def bold(text: str) -> str:
    return color(text, "1")


def fetch_url(url: str) -> str:
    """Fetch content from URL."""
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            return response.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP error {e.code} fetching {url}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"URL error fetching {url}: {e.reason}") from e


def fetch_json(url: str) -> dict:
    """Fetch and parse JSON from URL."""
    content = fetch_url(url)
    return json.loads(content)


def get_available_versions() -> list[str]:
    """
    Get list of available CUDA versions from NVIDIA redistributable index.
    Parses the HTML directory listing to find redistrib_*.json files.
    """
    index_url = f"{REDIST_BASE_URL}/"
    html = fetch_url(index_url)

    # Find all redistrib_X.Y.Z.json files
    pattern = r'redistrib_(\d+\.\d+\.\d+)\.json'
    versions = re.findall(pattern, html)

    # Sort versions semantically
    def version_key(v: str) -> tuple:
        parts = v.split(".")
        return tuple(int(p) for p in parts)

    versions = sorted(set(versions), key=version_key)
    return versions


def get_manifest(version: str) -> dict:
    """Download and parse the redistrib manifest for a specific version."""
    url = f"{REDIST_BASE_URL}/redistrib_{version}.json"
    return fetch_json(url)


def load_components_config() -> dict:
    """Load the components.json configuration file."""
    if not COMPONENTS_FILE.exists():
        raise FileNotFoundError(f"Components file not found: {COMPONENTS_FILE}")
    with open(COMPONENTS_FILE) as f:
        return json.load(f)


def save_components_config(config: dict) -> None:
    """Save the components.json configuration file."""
    with open(COMPONENTS_FILE, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")


def format_size(size_bytes: int | str) -> str:
    """Format byte size as human-readable string."""
    size_bytes = int(size_bytes) if isinstance(size_bytes, str) else size_bytes
    for unit in ["B", "KB", "MB", "GB"]:
        if size_bytes < 1024:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024
    return f"{size_bytes:.1f} TB"


def version_tuple(v: str) -> tuple:
    """Convert version string to tuple for comparison."""
    return tuple(int(p) for p in v.split("."))


def version_gte(v1: str, v2: str) -> bool:
    """Return True if v1 >= v2."""
    return version_tuple(v1) >= version_tuple(v2)


def cmd_list_versions(args: argparse.Namespace) -> int:
    """List all available CUDA versions from NVIDIA redistributables."""
    print("Fetching available CUDA versions...")
    versions = get_available_versions()

    if not versions:
        print(red("No versions found"))
        return 1

    print(f"\nAvailable CUDA versions ({len(versions)} total):")
    print("-" * 40)

    # Load supported versions once
    try:
        config = load_components_config()
        supported_versions = set(config.get("supported_versions", []))
    except FileNotFoundError:
        supported_versions = set()

    # Group by major version
    current_major = None
    for version in versions:
        major = version.split(".")[0]
        if major != current_major:
            if current_major is not None:
                print()
            current_major = major
            print(bold(f"CUDA {major}.x:"))

        marker = green(" [supported]") if version in supported_versions else ""
        print(f"  {version}{marker}")

    return 0


def cmd_show_components(args: argparse.Namespace) -> int:
    """Show components available for a specific CUDA version."""
    version = args.version
    platform = args.platform

    print(f"Fetching manifest for CUDA {version}...")
    try:
        manifest = get_manifest(version)
    except RuntimeError as e:
        print(red(f"Error: {e}"))
        return 1

    # Get list of components (exclude metadata keys and non-dict entries)
    components = [
        k for k, v in manifest.items()
        if not k.startswith("_") and isinstance(v, dict)
    ]
    components.sort()

    print(f"\nComponents in CUDA {version}:")
    print("-" * 80)
    print(f"{'Component':<35} {'Version':<15} {'Platform':<20} {'Size':<10}")
    print("-" * 80)

    total_size = 0
    for comp_name in components:
        comp_info = manifest[comp_name]
        comp_version = comp_info.get("version", "N/A")

        # Check each platform
        platforms_found = []
        for plat in ["linux-x86_64", "linux-aarch64", "windows-x86_64"]:
            if plat in comp_info:
                platforms_found.append(plat)

        if platform and platform not in platforms_found:
            continue

        # Get size for specified platform or first available
        target_plat = platform if platform else (platforms_found[0] if platforms_found else None)
        if target_plat and target_plat in comp_info:
            size = comp_info[target_plat].get("size", 0)
            size = int(size) if isinstance(size, str) else size
            size_str = format_size(size)
            total_size += size
        else:
            size_str = "N/A"

        plat_str = ", ".join(p.replace("-x86_64", "").replace("-aarch64", "-arm") for p in platforms_found)
        print(f"{comp_name:<35} {comp_version:<15} {plat_str:<20} {size_str:<10}")

    print("-" * 80)
    print(f"Total components: {len(components)}")
    if platform:
        print(f"Total size ({platform}): {format_size(total_size)}")

    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    """Validate that a CUDA version provides all required components."""
    version = args.version

    print(f"Validating CUDA {version} against components.json...")

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    try:
        manifest = get_manifest(version)
    except RuntimeError as e:
        print(red(f"Error: {e}"))
        return 1

    components = config.get("components", {})
    missing = []
    warnings = []
    skipped = []

    for comp_name, comp_config in components.items():
        # Check min_version requirement
        min_ver = comp_config.get("min_version")
        if min_ver and not version_gte(version, min_ver):
            skipped.append(f"{comp_name} (requires CUDA >= {min_ver})")
            continue

        # Skip optional components that aren't version-gated
        if not comp_config.get("required", False) and not min_ver:
            continue

        platforms = comp_config.get("platforms", ["linux-x86_64", "windows-x86_64"])

        if comp_name not in manifest:
            missing.append((comp_name, "not found in manifest"))
            continue

        comp_info = manifest[comp_name]
        for plat in platforms:
            if plat not in comp_info:
                warnings.append(f"{comp_name}: not available for {plat}")

    print()
    if skipped:
        print(f"Skipped components (version requirements not met):")
        for s in skipped:
            print(f"  - {s}")
        print()

    if missing:
        print(red("VALIDATION FAILED"))
        print("\nMissing required components:")
        for comp, reason in missing:
            print(f"  {red('X')} {comp}: {reason}")
        return 1

    if warnings:
        print(yellow("VALIDATION PASSED WITH WARNINGS"))
        print("\nWarnings:")
        for warning in warnings:
            print(f"  {yellow('!')} {warning}")
    else:
        print(green("VALIDATION PASSED"))
        print("\nAll required components are available.")

    return 0


def cmd_add_version(args: argparse.Namespace) -> int:
    """Add a new version to supported_versions in components.json."""
    version = args.version

    # Validate version exists
    print(f"Validating CUDA {version}...")
    try:
        manifest = get_manifest(version)
    except RuntimeError as e:
        print(red(f"Error: Version {version} not found: {e}"))
        return 1

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    supported = config.get("supported_versions", [])
    if version in supported:
        print(yellow(f"Version {version} is already in supported_versions"))
        return 0

    # Add and sort versions
    supported.append(version)

    def version_key(v: str) -> tuple:
        return tuple(int(p) for p in v.split("."))

    supported.sort(key=version_key)
    config["supported_versions"] = supported

    save_components_config(config)
    print(green(f"Added {version} to supported_versions"))
    print(f"Supported versions: {', '.join(supported)}")

    return 0


def cmd_check_updates(args: argparse.Namespace) -> int:
    """Check for new CUDA versions not in supported_versions."""
    print("Checking for updates...")

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    available = get_available_versions()
    supported = set(config.get("supported_versions", []))

    # Find versions newer than our latest
    if supported:
        def version_key(v: str) -> tuple:
            return tuple(int(p) for p in v.split("."))

        latest_supported = max(supported, key=version_key)
        latest_tuple = version_key(latest_supported)

        new_versions = [v for v in available if version_key(v) > latest_tuple]
    else:
        new_versions = available

    if new_versions:
        print(yellow(f"\nNew versions available ({len(new_versions)}):"))
        for v in new_versions:
            print(f"  {cyan(v)}")
        print(f"\nTo add a version: python {sys.argv[0]} add-version <version>")
        return 0
    else:
        print(green("\nNo new versions available."))
        return 0


def cmd_add_component(args: argparse.Namespace) -> int:
    """Add one or more components to components.json."""
    names = args.names

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    # Determine which manifest to validate against
    version = args.version
    if not version:
        supported = config.get("supported_versions", [])
        if supported:
            version = max(supported, key=version_tuple)
        else:
            print(red("Error: No supported versions in components.json and no --version specified"))
            return 1

    print(f"Fetching manifest for CUDA {version}...")
    try:
        manifest = get_manifest(version)
    except RuntimeError as e:
        print(red(f"Error: {e}"))
        return 1

    # Collect all manifest component names for suggestions
    manifest_components = sorted(
        k for k, v in manifest.items()
        if not k.startswith("_") and isinstance(v, dict)
    )

    existing_components = config.get("components", {})
    added = []
    skipped = []
    errors = []

    for name in names:
        if name in existing_components:
            skipped.append((name, "already exists in components.json"))
            continue

        if name not in manifest:
            # Try a fuzzy suggestion
            suggestions = [c for c in manifest_components if name.lower() in c.lower()]
            hint = ""
            if suggestions:
                hint = f" Did you mean: {', '.join(suggestions[:5])}?"
            errors.append(f"{name}: not found in CUDA {version} manifest.{hint}")
            continue

        comp_info = manifest[name]

        # Auto-detect platforms from manifest
        if args.platforms:
            platforms = args.platforms
        else:
            platforms = [
                p for p in ["linux-x86_64", "linux-aarch64", "windows-x86_64"]
                if p in comp_info
            ]

        # Auto-populate description from manifest 'name' field
        if args.description:
            description = args.description
        else:
            description = comp_info.get("name", name)

        required = args.required

        entry: dict = {
            "required": required,
            "description": description,
            "platforms": platforms,
        }
        if args.min_version:
            entry["min_version"] = args.min_version

        existing_components[name] = entry
        added.append(name)

    config["components"] = existing_components
    if added:
        save_components_config(config)

    # Report results
    print()
    for name in added:
        entry = existing_components[name]
        req = "required" if entry["required"] else "optional"
        plats = ", ".join(entry["platforms"])
        print(green(f"+ {name}") + f" ({req}, {plats})")
        print(f"  {entry['description']}")
    for name, reason in skipped:
        print(yellow(f"~ {name}: {reason}"))
    for msg in errors:
        print(red(f"X {msg}"))

    if added:
        print(f"\n{green(f'{len(added)} component(s) added')} to {COMPONENTS_FILE.name}")
    if errors:
        return 1
    return 0


def cmd_remove_component(args: argparse.Namespace) -> int:
    """Remove one or more components from components.json."""
    names = args.names

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    components = config.get("components", {})
    removed = []
    not_found = []

    for name in names:
        if name not in components:
            not_found.append(name)
            continue
        del components[name]
        removed.append(name)

    config["components"] = components
    if removed:
        save_components_config(config)

    print()
    for name in removed:
        print(red(f"- {name}"))
    for name in not_found:
        print(yellow(f"~ {name}: not found in components.json"))

    if removed:
        print(f"\n{len(removed)} component(s) removed from {COMPONENTS_FILE.name}")
    return 0


def cmd_urls(args: argparse.Namespace) -> int:
    """Generate download URLs for a CUDA version."""
    version = args.version
    platform = args.platform

    try:
        config = load_components_config()
    except FileNotFoundError as e:
        print(red(f"Error: {e}"))
        return 1

    try:
        manifest = get_manifest(version)
    except RuntimeError as e:
        print(red(f"Error: {e}"))
        return 1

    components = config.get("components", {})
    total_size = 0

    print(f"Download URLs for CUDA {version} ({platform}):")
    print("-" * 80)

    for comp_name, comp_config in components.items():
        # Check min_version requirement
        min_ver = comp_config.get("min_version")
        if min_ver and not version_gte(version, min_ver):
            continue

        # Skip optional components that aren't version-gated
        if not comp_config.get("required", False) and not min_ver:
            continue

        platforms = comp_config.get("platforms", ["linux-x86_64", "windows-x86_64"])
        if platform not in platforms:
            continue

        if comp_name not in manifest:
            print(f"{red('X')} {comp_name}: not found in manifest")
            continue

        comp_info = manifest[comp_name]
        if platform not in comp_info:
            print(f"{yellow('!')} {comp_name}: not available for {platform}")
            continue

        plat_info = comp_info[platform]
        relative_path = plat_info.get("relative_path", "")
        sha256 = plat_info.get("sha256", "")
        size = plat_info.get("size", 0)
        size = int(size) if isinstance(size, str) else size
        total_size += size

        url = f"{REDIST_BASE_URL}/{relative_path}"
        print(f"\n{bold(comp_name)}:")
        print(f"  URL: {url}")
        print(f"  SHA256: {sha256}")
        print(f"  Size: {format_size(size)}")

    print("-" * 80)
    print(f"Total download size: {bold(format_size(total_size))}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="CUDA Version Manager for setup-cuda GitHub Action",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s list-versions                              List all available CUDA versions
  %(prog)s show-components 13.0.2                     Show components for CUDA 13.0.2
  %(prog)s validate 13.0.2                            Validate components.json against 13.0.2
  %(prog)s add-version 13.1.1                         Add 13.1.1 to supported versions
  %(prog)s add-component libcublas libcusolver         Add components to components.json
  %(prog)s add-component libcublas --no-required       Add as optional component
  %(prog)s remove-component libcublas                  Remove a component
  %(prog)s check-updates                              Check for newer versions
  %(prog)s urls 13.0.2 -p windows-x86_64              Generate download URLs
        """,
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # list-versions
    subparsers.add_parser("list-versions", help="List all available CUDA versions")

    # show-components
    show_parser = subparsers.add_parser("show-components", help="Show components for a CUDA version")
    show_parser.add_argument("version", help="CUDA version (e.g., 13.0.2)")
    show_parser.add_argument("-p", "--platform", help="Filter by platform (e.g., linux-x86_64)")

    # validate
    validate_parser = subparsers.add_parser("validate", help="Validate components.json against a version")
    validate_parser.add_argument("version", help="CUDA version to validate")

    # add-version
    add_parser = subparsers.add_parser("add-version", help="Add a version to supported_versions")
    add_parser.add_argument("version", help="CUDA version to add")

    # add-component
    add_comp_parser = subparsers.add_parser(
        "add-component",
        help="Add component(s) to components.json",
        description="Add one or more CUDA redistributable components to components.json. "
        "Validates against the NVIDIA manifest and auto-detects platforms and description.",
    )
    add_comp_parser.add_argument(
        "names", nargs="+", help="Component name(s) as they appear in the NVIDIA manifest"
    )
    add_comp_parser.add_argument(
        "--version", default=None,
        help="CUDA version manifest to validate against (default: latest supported)",
    )
    add_comp_parser.add_argument(
        "--required", default=True, action=argparse.BooleanOptionalAction,
        help="Mark component as required (default: --required)",
    )
    add_comp_parser.add_argument(
        "--description", default=None,
        help="Override auto-detected description",
    )
    add_comp_parser.add_argument(
        "--platforms", nargs="+", default=None,
        help="Override auto-detected platforms (e.g., linux-x86_64 windows-x86_64)",
    )
    add_comp_parser.add_argument(
        "--min-version", default=None,
        help="Minimum CUDA version for this component (e.g., 13.0.0)",
    )

    # remove-component
    remove_comp_parser = subparsers.add_parser(
        "remove-component",
        help="Remove component(s) from components.json",
    )
    remove_comp_parser.add_argument(
        "names", nargs="+", help="Component name(s) to remove"
    )

    # check-updates
    subparsers.add_parser("check-updates", help="Check for new CUDA versions")

    # urls
    urls_parser = subparsers.add_parser("urls", help="Generate download URLs for a version")
    urls_parser.add_argument("version", help="CUDA version")
    urls_parser.add_argument("-p", "--platform", default="linux-x86_64", help="Platform (default: linux-x86_64)")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    commands = {
        "list-versions": cmd_list_versions,
        "show-components": cmd_show_components,
        "validate": cmd_validate,
        "add-version": cmd_add_version,
        "add-component": cmd_add_component,
        "remove-component": cmd_remove_component,
        "check-updates": cmd_check_updates,
        "urls": cmd_urls,
    }

    return commands[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
