#!/bin/bash
# Run as nick, not root. Requires the existing Immich installation.
set -euo pipefail
umask 077
LAN_IP=${1:?Usage: bash configure-lan.sh LAN_IPV4}
[[ "$LAN_IP" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] || { echo 'Invalid IPv4'; exit 1; }
ip -4 -o address show dev enp2s0 | grep -Fq " inet $LAN_IP/" || {
  echo 'STOP: address is not assigned to enp2s0'; exit 1;
}
cd /home/nick/immich-app
sudo /usr/local/sbin/immich-check-storage
sudo docker compose config --quiet
test "$(grep -Fc "      - '127.0.0.1:2283:2283'" docker-compose.yml)" -eq 1 || {
  echo 'STOP: expected localhost mapping not found exactly once'; exit 1;
}
if grep -Fq "      - '$LAN_IP:2283:2283'" docker-compose.yml; then
  echo 'LAN mapping already present'
else
  test "$(grep -c ':2283' docker-compose.yml)" -eq 1 || {
    echo 'STOP: unexpected port mappings; review Compose manually'; exit 1;
  }
  backup="docker-compose.yml.before-lan-$(date +%Y%m%d-%H%M%S)"
  test ! -e "$backup"
  cp -p docker-compose.yml "$backup"
  sed -i "/^      - '127\.0\.0\.1:2283:2283'$/a\      - '$LAN_IP:2283:2283'" docker-compose.yml
  if ! sudo docker compose config --quiet; then
    cp -p "$backup" docker-compose.yml
    echo 'Invalid Compose; original file restored. No restart performed.'
    exit 1
  fi
  echo "Saved original Compose: $backup"
fi
sudo systemctl enable docker tailscaled immich
sudo systemctl reset-failed immich
sudo systemctl restart immich
echo 'Waiting for HTTP response (up to 120 seconds)...'
ready=false
for ((attempt=1; attempt<=60; attempt++)); do
  if curl -fsS --max-time 2 -o /dev/null http://127.0.0.1:2283; then
    ready=true
    break
  fi
  sleep 2
done
if [[ "$ready" != true ]]; then
  echo 'HTTP not ready; inspect journalctl -u immich. Do not reboot yet.'
  sudo docker compose ps -a
  exit 1
fi
systemctl is-enabled docker tailscaled immich
systemctl is-active docker tailscaled immich
sudo docker compose ps -a
curl -I --max-time 10 "http://$LAN_IP:2283"
sudo tailscale serve status
echo 'Server checks complete. Verify LAN access from a second device before reboot.'
