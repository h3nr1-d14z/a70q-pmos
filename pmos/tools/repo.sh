#!/usr/bin/env bash
# Serve the pmbootstrap package repo (docker volume a70q-pmbwork, packages/edge/aarch64, signed with
# the pmos@local key that `pmbootstrap install` puts into the phone's /etc/apk/keys) over HTTP, so the
# phone updates our packages with plain `apk upgrade`.
#   pmos/tools/repo.sh start     start (or restart) the server on port 8099, survives Docker restarts
#   pmos/tools/repo.sh stop
#   pmos/tools/repo.sh phone     print the lines to add to the phone's /etc/apk/repositories
set -euo pipefail
PORT="${PORT:-8099}"
case "${1:-start}" in
start)
	docker rm -f a70q-repo >/dev/null 2>&1 || true
	docker run -d --name a70q-repo --restart unless-stopped -p "$PORT:8099" \
		-v a70q-pmbwork:/w:ro python:3-alpine \
		python -m http.server 8099 --directory /w/packages >/dev/null
	echo "serving a70q-pmbwork:/packages on :$PORT"
	;;
stop) docker rm -f a70q-repo >/dev/null ;;
phone)
	echo "http://172.16.42.2:$PORT/edge   # Mac over USB (ECM)"
	lan="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true)"
	[ -n "$lan" ] && echo "http://$lan:$PORT/edge   # Mac over WiFi/LAN (address can change)"
	;;
*) echo "usage: $0 start|stop|phone" >&2; exit 1 ;;
esac
