#!/bin/zsh
lsof -ti :8770 | xargs kill 2>/dev/null
sleep 0.3
cd "$(dirname "$0")/engine"
python3 viewer_server.py --open
