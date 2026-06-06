#!/bin/sh
# usb-mount.sh <appid>
# Bind-mount host-visible /tmp/usb partitions into the supplied app's jail.
# The app must hold .bench-usb.lock under /tmp/<appid> for exit cleanup.
# Managed bind mounts are unmounted unconditionally before each setup run.
# A watcher waits on the app-held run lock and re-runs cleanup after the app exits.
appid=$1

if [ -z "$appid" ]; then
  echo "usb-mount: missing appid" >&2
  exit 1
fi

host_usb_root=/tmp/usb
jail_root="/var/palm/jail/$appid"
app_tmp_root="$jail_root/tmp/$appid"
# The app jail has its own /tmp tree, so the host-visible USB mounts need a
# decoder-bench-owned landing path inside that jail.
managed_root="$app_tmp_root/usb"
lock_path="$app_tmp_root/.bench-usb.lock"
watcher_log="$app_tmp_root/usb-mount.log"
mounted_count=0

cleanup_managed_mounts() {
  # Unmount deepest paths first so a nested bind mount cannot keep its parent
  # mounted during stale-mount cleanup.
  awk -v prefix="$managed_root/" '
        index($2, prefix) == 1 {
            printf "%08d %s\n", length($2), $2
        }
    ' /proc/mounts | sort -rn |
    while IFS=' ' read -r _ target; do
      if ! umount "$target"; then
        echo "usb-mount: failed to unmount $target" >&2
      fi
    done
}

cleanup_managed_mounts

# Use the host mount table here. On the current target, app-visible
# /tmp/usb entries can be skeleton directories without the real partition data.
while IFS=' ' read -r _ mountpoint _rest; do
  case $mountpoint in
    "$host_usb_root"/*) ;;
    *) continue ;;
  esac

  suffix=${mountpoint#"$host_usb_root"/}
  target="$managed_root/$suffix"
  if ! mkdir -p "$target"; then
    echo "usb-mount: failed to create target $target" >&2
    continue
  fi
  if mount --bind "$mountpoint" "$target"; then
    mounted_count=$((mounted_count + 1))
  else
    echo "usb-mount: failed to bind $mountpoint to $target" >&2
  fi
done </proc/mounts

if [ "$mounted_count" -eq 0 ]; then
  echo "usb-mount: no host /tmp/usb sources mounted" >&2
  exit 1
fi

if command -v flock >/dev/null 2>&1; then
  # The app process holds this lock for its whole lifetime. The watcher blocks
  # here until process exit closes that fd, then removes the managed binds.
  (
    if ! flock -x 9; then
      echo "usb-mount: failed to wait on run lock $lock_path" >&2
      exit 0
    fi
    cleanup_managed_mounts
  ) 9<>"$lock_path" >>"$watcher_log" 2>&1 &
else
  echo "usb-mount: flock unavailable; exit cleanup disabled" >&2
fi

exit 0
