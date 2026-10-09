#!/bin/sh
set -eu
umask 077
backup_dir=/var/backups/nutrition-bot
mkdir -p "$backup_dir"
backup_file="$backup_dir/bot-$(date -u +%Y%m%dT%H%M%SZ).sqlite3"
test ! -e "$backup_file"
sqlite3 /var/lib/nutrition-bot/bot.sqlite3 ".backup '$backup_file'"
test "$(sqlite3 "$backup_file" 'PRAGMA integrity_check;')" = ok
# Delete only matching old backup files in the fixed backup directory.
find "$backup_dir" -maxdepth 1 -type f -name 'bot-????????T??????Z.sqlite3' -mtime +14 -delete
printf '%s\n' "$backup_file"
