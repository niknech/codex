#!/bin/sh
set -eu
fail() { echo "Immich storage check failed: $*" >&2; exit 1; }
mountpoint -q /mnt/photos || fail '/mnt/photos is not a mount point'
actual_uuid=$(findmnt -n -o UUID --mountpoint /mnt/photos)
[ "$actual_uuid" = 'f8d17a00-1f7e-4ed9-86be-2a71af0587b7' ] || fail 'unexpected filesystem UUID'
[ "$(findmnt -n -o FSTYPE --mountpoint /mnt/photos)" = ext4 ] || fail 'expected ext4'
options=$(findmnt -n -o OPTIONS --mountpoint /mnt/photos)
case ",$options," in *,rw,*) ;; *) fail 'filesystem is not read-write' ;; esac
[ -d /mnt/photos/immich ] || fail 'media directory missing'
[ ! -L /mnt/photos/immich ] || fail 'media directory must not be a symlink'
echo 'Immich storage check OK'
