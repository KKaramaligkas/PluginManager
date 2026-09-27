#!/bin/sh
# Refreshes res/cacert.pem with the current Mozilla CA store shipped by certifi.
set -e
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
python3 -m venv "$tmp/venv"
"$tmp/venv/bin/pip" -q install certifi
ca=$("$tmp/venv/bin/python" -c "import certifi; print(certifi.where())")
ver=$("$tmp/venv/bin/python" -c "import certifi; print(certifi.__version__)")
{
    echo "# Mozilla CA certificate store (certifi $ver), MPL-2.0: https://www.mozilla.org/MPL/2.0/"
    echo "# Used by the Plugin Manager to verify HTTPS servers. Regenerate: tools/update_cacert.sh"
    cat "$ca"
} > res/cacert.pem
rm -rf "$tmp"
echo "res/cacert.pem updated (certifi $ver)"
