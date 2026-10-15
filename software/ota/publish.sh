#!/usr/bin/env bash
# Publishes the panel and the signed firmware (build.sh output) to
# https://mxg.sh/lowy-na-lisa/ (static folder of the mxg.sh site on la-casa,
# served by the swag container from /config/www/mxg.sh).
#   software/ota/publish.sh
# Devices only accept firmware signed with the project key, so publishing the
# images is safe; the page itself only talks to devices the visitor picks.
set -euo pipefail
cd "$(dirname "$0")/../web-flasher"
[ -f firmware/manifest.json ] || { echo "run software/ota/build.sh first" >&2; exit 1; }
dest=/opt/docker/data/swag/config/www/mxg.sh/lowy-na-lisa
rsync -rlt --delete --chmod=D755,F644 -e "ssh -o BatchMode=yes" \
  index.html spektrum-logo.png inter-var.woff2 inter-LICENSE.txt devices.json firmware \
  "la-casa:$dest/"
echo "published $(cat firmware/manifest.json) -> https://mxg.sh/lowy-na-lisa/"
