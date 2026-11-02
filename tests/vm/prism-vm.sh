#!/usr/bin/env bash
# End-to-end test VM for Prism: a Fedora KDE guest run by libvirt (qemu:///session, no root needed).
#
# Usage: tests/vm/prism-vm.sh <command> [args]
#   create     download the Fedora Cloud image, build the overlay disk and seed ISO, define the VM
#   up         start the VM and wait until SSH answers
#   provision  wait for cloud-init (installs KDE, SDDM, build/runtime dependencies) to finish
#   install    install a Prism build tree (cmake --install output) into the guest
#   smoke      run tests/vm/smoke.sh inside the guest and collect logs
#   ssh [cmd]  open a shell (or run a command) in the guest
#   logs       copy journal and Prism logs into the state directory
#   down       shut the VM down
#   destroy    remove the VM and its disks (the base image cache is kept)
#   all        create, up, provision, install and smoke in one go
#
# Environment:
#   PRISM_VM_NAME      VM name (default prism-e2e)
#   PRISM_VM_DIR       state directory (default ~/.cache/prism-vm)
#   PRISM_VM_SSH_PORT  host port forwarded to guest SSH (default 2222)
#   PRISM_VM_MEM_MB    guest memory (default 6144)
#   PRISM_VM_CPUS      guest vCPUs (default 4)
#   PRISM_VM_DISK_GB   guest disk size (default 40)
#   PRISM_VM_IMAGE_URL override the base image URL
#   PRISM_BUILD_DIR    build tree to install (default cmake-build-podman)
#   PRISM_INSTALL_IMAGE podman image to run cmake --install in, for trees built in a container
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

NAME="${PRISM_VM_NAME:-prism-e2e}"
DIR="${PRISM_VM_DIR:-$HOME/.cache/prism-vm}"
PORT="${PRISM_VM_SSH_PORT:-2222}"
MEM="${PRISM_VM_MEM_MB:-6144}"
CPUS="${PRISM_VM_CPUS:-4}"
DISK_GB="${PRISM_VM_DISK_GB:-40}"
IMAGE_URL="${PRISM_VM_IMAGE_URL:-https://dl.fedoraproject.org/pub/fedora/linux/releases/44/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-44-1.7.x86_64.qcow2}"
BUILD_DIR="${PRISM_BUILD_DIR:-$REPO/cmake-build-podman}"
VIRSH=(virsh -c qemu:///session)
KEY="$DIR/id_ed25519"
SSH_OPTS=(-i "$KEY" -p "$PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR -o ConnectTimeout=5 -o IdentitiesOnly=yes)

log() { printf '==> %s\n' "$*"; }
die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need() {
  local tool
  for tool in "$@"; do
    command -v "$tool" >/dev/null 2>&1 || die "missing host tool: $tool"
  done
}

guest_ssh() { ssh "${SSH_OPTS[@]}" prism@127.0.0.1 "$@"; }

vm_defined() { "${VIRSH[@]}" dominfo "$NAME" >/dev/null 2>&1; }
vm_running() { [ "$("${VIRSH[@]}" domstate "$NAME" 2>/dev/null)" = "running" ]; }

cmd_create() {
  need virt-install virsh qemu-img xorriso curl ssh-keygen
  [ -c /dev/kvm ] || die "/dev/kvm is not available"
  mkdir -p "$DIR"
  vm_defined && die "VM $NAME already exists (run 'destroy' first)"

  local base="$DIR/$(basename "$IMAGE_URL")"
  if [ ! -f "$base" ]; then
    log "downloading $IMAGE_URL"
    curl -fsSL --retry 3 -o "$base.part" "$IMAGE_URL"
    mv "$base.part" "$base"
  fi

  [ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N '' -C prism-vm -f "$KEY"

  log "building overlay disk and cloud-init seed"
  rm -f "$DIR/$NAME.qcow2" "$DIR/$NAME-seed.iso"
  qemu-img create -q -f qcow2 -F qcow2 -b "$base" "$DIR/$NAME.qcow2" "${DISK_GB}G"

  local seed="$DIR/seed"
  rm -rf "$seed"
  mkdir -p "$seed"
  sed "s|@SSH_PUBKEY@|$(cat "$KEY.pub")|" "$HERE/cloud-init/user-data" >"$seed/user-data"
  printf 'instance-id: %s\nlocal-hostname: %s\n' "$NAME-$(date +%s)" "$NAME" >"$seed/meta-data"
  xorriso -as mkisofs -quiet -output "$DIR/$NAME-seed.iso" -volid cidata -joliet -rock \
    "$seed/user-data" "$seed/meta-data"

  log "defining VM $NAME (${CPUS} vCPU, ${MEM} MiB)"
  virt-install --connect qemu:///session --name "$NAME" --import --noautoconsole \
    --memory "$MEM" --vcpus "$CPUS" --cpu host-passthrough --osinfo fedora-unknown \
    --boot uefi --graphics none --video virtio \
    --disk "path=$DIR/$NAME.qcow2,bus=virtio" \
    --disk "path=$DIR/$NAME-seed.iso,device=cdrom" \
    --network "passt,portForward=127.0.0.1:${PORT}:22" \
    --sysinfo system.serial=prism-e2e \
    --print-xml >"$DIR/$NAME.xml"
  "${VIRSH[@]}" define "$DIR/$NAME.xml" >/dev/null
  log "created; run '$0 up'"
}

cmd_up() {
  vm_defined || die "VM $NAME does not exist (run 'create')"
  vm_running || "${VIRSH[@]}" start "$NAME" >/dev/null
  log "waiting for SSH on 127.0.0.1:$PORT"
  local i
  for i in $(seq 1 120); do
    if guest_ssh true 2>/dev/null; then
      log "guest reachable"
      return 0
    fi
    sleep 5
  done
  die "SSH did not come up within 10 minutes"
}

cmd_provision() {
  log "waiting for cloud-init (first boot installs KDE; this takes a while)"
  guest_ssh 'sudo cloud-init status --wait --long' || true
  guest_ssh 'test -f /var/lib/prism-vm-provisioned' ||
    die "provisioning did not finish; see 'ssh' and /var/log/cloud-init-output.log"
  log "provisioned"
}

cmd_install() {
  [ -d "$BUILD_DIR" ] || die "build directory not found: $BUILD_DIR"
  local stage
  stage="$(mktemp -d)"
  trap 'rm -rf "$stage"' RETURN
  log "staging install from $BUILD_DIR"
  if [ -n "${PRISM_INSTALL_IMAGE:-}" ]; then
    # The build tree records container paths (/src), so install from inside the build image.
    podman run --rm --security-opt label=disable -v "$REPO:/src" -v "$stage:/stage" -w /src \
      "$PRISM_INSTALL_IMAGE" env DESTDIR=/stage cmake --install "/src/$(basename "$BUILD_DIR")" \
      --prefix /usr >/dev/null
  else
    DESTDIR="$stage" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null
  fi
  # Own everything as root and never touch existing directories: extracting "./" as root with the host
  # user's mode and uid would otherwise leave / (mode 700), /usr and /usr/bin owned by the guest user.
  tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf "$DIR/prism-stage.tar.gz" .
  guest_ssh 'cat > /tmp/prism-stage.tar.gz' <"$DIR/prism-stage.tar.gz"
  guest_ssh 'sudo tar -C / --no-overwrite-dir --no-same-permissions -xzf /tmp/prism-stage.tar.gz &&
    sudo systemctl daemon-reload'
  log "installed into guest"
}

cmd_smoke() {
  guest_ssh 'cat > /tmp/smoke.sh' <"$HERE/smoke.sh"
  local rc=0
  guest_ssh 'sudo bash /tmp/smoke.sh' || rc=$?
  cmd_logs
  return "$rc"
}

cmd_logs() {
  mkdir -p "$DIR/logs"
  guest_ssh 'sudo journalctl -b --no-pager' >"$DIR/logs/journal.txt" 2>&1 || true
  guest_ssh 'sudo tar -czf - -C / var/log/cloud-init-output.log home/prism/.config/sunshine 2>/dev/null' \
    >"$DIR/logs/guest-files.tar.gz" 2>/dev/null || true
  log "logs in $DIR/logs"
}

cmd_down() {
  vm_running || return 0
  "${VIRSH[@]}" shutdown "$NAME" >/dev/null || true
  local i
  for i in $(seq 1 30); do
    vm_running || return 0
    sleep 2
  done
  "${VIRSH[@]}" destroy "$NAME" >/dev/null
}

cmd_destroy() {
  if vm_defined; then
    vm_running && "${VIRSH[@]}" destroy "$NAME" >/dev/null
    "${VIRSH[@]}" undefine "$NAME" --nvram >/dev/null
  fi
  rm -rf "$DIR/$NAME.qcow2" "$DIR/$NAME-seed.iso" "$DIR/$NAME.xml" "$DIR/seed" "$DIR/logs" \
    "$DIR/prism-stage.tar.gz"
  log "destroyed $NAME"
}

case "${1:-}" in
create) cmd_create ;;
up) cmd_up ;;
provision) cmd_provision ;;
install) cmd_install ;;
smoke) cmd_smoke ;;
ssh)
  shift
  guest_ssh "$@"
  ;;
logs) cmd_logs ;;
down) cmd_down ;;
destroy) cmd_destroy ;;
all)
  cmd_create
  cmd_up
  cmd_provision
  cmd_install
  cmd_smoke
  ;;
*)
  sed -n '2,/^set -/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
  exit 2
  ;;
esac
