#!/bin/sh
# Serve the demo app store on port 8811.
# Then start the companion with:
#   STORE_URL=http://127.0.0.1:8811/index.txt node companion/server.js
cd "$(dirname "$0")" && exec python3 -m http.server 8811
