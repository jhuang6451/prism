# Prism end-to-end test VM

A scripted Fedora 44 + KDE guest (libvirt `qemu:///session`, no root needed) for features that
containers cannot cover: logind, PAM sessions, SDDM, and running with nobody logged in.

```bash
tests/vm/prism-vm.sh create      # download the Fedora Cloud image, build disk and cloud-init seed
tests/vm/prism-vm.sh up          # start and wait for SSH (127.0.0.1:2222)
tests/vm/prism-vm.sh provision   # wait for cloud-init (installs KDE, SDDM, Moonlight flatpak)
PRISM_INSTALL_IMAGE=localhost/prism-fedora44-build:latest tests/vm/prism-vm.sh install
tests/vm/prism-vm.sh smoke       # run smoke.sh in the guest, collect logs
tests/vm/prism-vm.sh all         # all of the above
```

Run with no arguments to see all commands and environment variables. State (images, SSH key, logs)
lives in `~/.cache/prism-vm`. The guest user is `prism` (key-only SSH, passwordless sudo).

`smoke.sh` prints `PASS`, `FAIL` or `SKIP` per stage. `SKIP` stages need the system service and remote
sessions and must become `PASS` as those roadmap tasks land.
