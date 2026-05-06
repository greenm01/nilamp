#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: tools/package_macos_release.sh --version V --plugin PATH --clap-bundle NAME --dist-dir DIR --gpg-key KEY
EOF
}

version=
plugin=
clap_bundle=
dist_dir=
gpg_key=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            version="${2:-}"
            shift 2
            ;;
        --plugin)
            plugin="${2:-}"
            shift 2
            ;;
        --clap-bundle)
            clap_bundle="${2:-}"
            shift 2
            ;;
        --dist-dir)
            dist_dir="${2:-}"
            shift 2
            ;;
        --gpg-key)
            gpg_key="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [ -z "$version" ] || [ -z "$plugin" ] || [ -z "$clap_bundle" ] ||
   [ -z "$dist_dir" ] || [ -z "$gpg_key" ]; then
    usage
    exit 2
fi

case "$(uname -s)" in
    Darwin) ;;
    *)
        echo "package_macos_release: macOS packaging must run on Darwin" >&2
        exit 1
        ;;
esac

if [ ! -f "$plugin" ]; then
    echo "package_macos_release: plugin not found: $plugin" >&2
    exit 1
fi

command -v codesign >/dev/null 2>&1 || {
    echo "package_macos_release: codesign not found" >&2
    exit 1
}
command -v gpg >/dev/null 2>&1 || {
    echo "package_macos_release: gpg not found" >&2
    exit 1
}
command -v shasum >/dev/null 2>&1 || {
    echo "package_macos_release: shasum not found" >&2
    exit 1
}
command -v ditto >/dev/null 2>&1 || {
    echo "package_macos_release: ditto not found" >&2
    exit 1
}

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
dist_abs="$(cd "$repo_root" && mkdir -p "$dist_dir" && cd "$dist_dir" && pwd)"
stage_root="$(mktemp -d "${TMPDIR:-/tmp}/nilamp-release.XXXXXX")"
trap 'rm -rf "$stage_root"' EXIT

package_name="nilamp-twd-mkii-v${version}-macos"
package_dir="$stage_root/$package_name"
zip_path="$dist_abs/$package_name.zip"
sums_path="$dist_abs/SHA256SUMS"

rm -f "$zip_path" "$zip_path.asc" "$sums_path" "$sums_path.asc"
mkdir -p "$package_dir"

cp -f "$plugin" "$package_dir/$clap_bundle"
codesign --force --sign - "$package_dir/$clap_bundle"

cat > "$package_dir/install.command" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
clap_name="nilamp-twd-mkii.clap"
source_clap="$script_dir/$clap_name"
target_dir="$HOME/Library/Audio/Plug-Ins/CLAP"
target_clap="$target_dir/$clap_name"

if [ ! -f "$source_clap" ]; then
    echo "Could not find $clap_name next to install.command" >&2
    exit 1
fi

mkdir -p "$target_dir"
cp -f "$source_clap" "$target_clap"
codesign --force --sign - "$target_clap"

echo "Installed $target_clap"
echo "Restart your DAW or rescan CLAP plug-ins if nilamp was already open."
EOF
chmod 755 "$package_dir/install.command"

cat > "$package_dir/README-macOS.txt" <<EOF
nilamp TWD MKII v${version} for macOS

Install:
  Double-click install.command, or copy ${clap_bundle} to:
  ~/Library/Audio/Plug-Ins/CLAP/${clap_bundle}

After installing, restart your DAW or rescan CLAP plug-ins.

Verify release artifacts:
  gpg --verify SHA256SUMS.asc SHA256SUMS
  shasum -a 256 -c SHA256SUMS
  gpg --verify ${package_name}.zip.asc ${package_name}.zip

Release signing key fingerprint:
  C3504EE1EE38410CE1C433BC372B8AAACB867F13

Keller reference:
  nilamp is based on Helmut Keller's "A Tube Amp Modeling Project".
  See https://www.helmutkelleraudio.de/ for Keller's original work.

Note:
  GPG signatures verify publisher/artifact integrity. This release is ad-hoc
  codesigned for local macOS loading; it is not Apple Developer ID notarized.
EOF

cp -f "$repo_root/LICENSE" "$package_dir/LICENSE"

(cd "$stage_root" && ditto -c -k --keepParent "$package_name" "$zip_path")

(
    cd "$dist_abs"
    shasum -a 256 "$(basename "$zip_path")" > "$(basename "$sums_path")"
    gpg --batch --yes --armor --local-user "$gpg_key" --detach-sign "$(basename "$zip_path")"
    gpg --batch --yes --armor --local-user "$gpg_key" --detach-sign "$(basename "$sums_path")"
)

echo "$zip_path"
echo "$zip_path.asc"
echo "$sums_path"
echo "$sums_path.asc"
