#!/usr/bin/env bash
set -euo pipefail

if ! command -v node >/dev/null 2>&1; then
  apt-get update
  apt-get install -y nodejs npm
fi

if ! command -v npm >/dev/null 2>&1; then
  apt-get update
  apt-get install -y npm
fi

apt-get update
apt-get install -y build-essential python3 ca-certificates

useradd --system --home /opt/blinq-server --shell /usr/sbin/nologin blinq 2>/dev/null || true
mkdir -p /opt/blinq-server/data /opt/blinq-server/uploads
install -m 0755 /tmp/blinq-server/server.js /opt/blinq-server/server.js
install -m 0644 /tmp/blinq-server/package.json /opt/blinq-server/package.json
install -m 0644 /tmp/blinq-server/blinq-server.service /etc/systemd/system/blinq-server.service

(cd /opt/blinq-server && npm install --omit=dev)

if [ ! -f /etc/blinq-server.env ]; then
  cat >/etc/blinq-server.env <<'ENV'
BLINQ_SERVER_PORT=45476
BLINQ_DATA_DIR=/opt/blinq-server/data
BLINQ_UPLOAD_DIR=/opt/blinq-server/uploads
ENV
fi

chown -R blinq:blinq /opt/blinq-server
chown root:blinq /etc/blinq-server.env
chmod 640 /etc/blinq-server.env

systemctl disable --now blinq-relay 2>/dev/null || true
systemctl daemon-reload
systemctl enable --now blinq-server

if command -v ufw >/dev/null 2>&1 && ufw status | grep -q "Status: active"; then
  ufw allow 45476/tcp
fi

systemctl status blinq-server --no-pager
