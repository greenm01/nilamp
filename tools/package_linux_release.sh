#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: tools/package_linux_release.sh --version V --plugin PATH --clap-bundle NAME --dist-dir DIR --gpg-key KEY [--existing-sums PATH]
EOF
}

version=
plugin=
clap_bundle=
dist_dir=
gpg_key=
existing_sums=

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
        --existing-sums)
            existing_sums="${2:-}"
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
    Linux) ;;
    *)
        echo "package_linux_release: Linux packaging must run on Linux" >&2
        exit 1
        ;;
esac

if [ ! -f "$plugin" ]; then
    echo "package_linux_release: plugin not found: $plugin" >&2
    exit 1
fi

command -v gpg >/dev/null 2>&1 || {
    echo "package_linux_release: gpg not found" >&2
    exit 1
}
command -v sha256sum >/dev/null 2>&1 || {
    echo "package_linux_release: sha256sum not found" >&2
    exit 1
}
command -v tar >/dev/null 2>&1 || {
    echo "package_linux_release: tar not found" >&2
    exit 1
}

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
dist_abs="$(cd "$repo_root" && mkdir -p "$dist_dir" && cd "$dist_dir" && pwd)"
stage_root="$(mktemp -d "${TMPDIR:-/tmp}/nilamp-release.XXXXXX")"
trap 'rm -rf "$stage_root"' EXIT

arch="$(uname -m)"
package_name="nilamp-twd-mkii-v${version}-linux-${arch}"
package_dir="$stage_root/$package_name"
tar_path="$dist_abs/$package_name.tar.gz"
sums_path="$dist_abs/SHA256SUMS"
existing_sum_lines=

if [ -n "$existing_sums" ] && [ -f "$existing_sums" ]; then
    tar_name="$(basename "$tar_path")"
    existing_sum_lines="$(grep -v -F "  $tar_name" "$existing_sums" || true)"
fi

rm -f "$tar_path" "$tar_path.asc" "$sums_path" "$sums_path.asc"
mkdir -p "$package_dir"

cp -f "$plugin" "$package_dir/$clap_bundle"
chmod 755 "$package_dir/$clap_bundle"

cat > "$package_dir/install.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
clap_name="nilamp-twd-mkii.clap"
source_clap="$script_dir/$clap_name"
target_dir="$HOME/.clap"
target_clap="$target_dir/$clap_name"

if [ ! -f "$source_clap" ]; then
    echo "Could not find $clap_name next to install.sh" >&2
    exit 1
fi

mkdir -p "$target_dir"
cp -f "$source_clap" "$target_clap"
chmod 755 "$target_clap"

echo "Installed $target_clap"
echo "Restart your DAW or rescan CLAP plug-ins if nilamp was already open."
EOF
chmod 755 "$package_dir/install.sh"

cat > "$package_dir/README-Linux.txt" <<EOF
nilamp TWD MKII v${version} for Linux ${arch}

Install:
  ./install.sh

or copy ${clap_bundle} to:
  ~/.clap/${clap_bundle}

REAPER on Linux scans CLAP plug-ins from:
  /usr/local/lib/clap
  /usr/lib/clap
  ~/.clap
  \$CLAP_PATH

After installing, restart your DAW or rescan CLAP plug-ins.

Verify release artifacts:
  gpg --verify SHA256SUMS.asc SHA256SUMS
  sha256sum -c SHA256SUMS
  gpg --verify ${package_name}.tar.gz.asc ${package_name}.tar.gz

Release signing key fingerprint:
  C3504EE1EE38410CE1C433BC372B8AAACB867F13

Keller reference:
  nilamp is based on Helmut Keller's "A Tube Amp Modeling Project".
  See https://www.helmutkelleraudio.de/ for Keller's original work.
EOF

cp -f "$repo_root/LICENSE" "$package_dir/LICENSE"

(cd "$stage_root" && tar -czf "$tar_path" "$package_name")

(
    cd "$dist_abs"
    tar_name="$(basename "$tar_path")"
    if [ -n "$existing_sum_lines" ]; then
        printf '%s\n' "$existing_sum_lines" > "$(basename "$sums_path")"
    else
        : > "$(basename "$sums_path")"
    fi
    sha256sum "$tar_name" >> "$(basename "$sums_path")"
    gpg --batch --yes --armor --local-user "$gpg_key" --detach-sign "$tar_name"
    gpg --batch --yes --armor --local-user "$gpg_key" --detach-sign "$(basename "$sums_path")"
)

echo "$tar_path"
echo "$tar_path.asc"
echo "$sums_path"
echo "$sums_path.asc"
