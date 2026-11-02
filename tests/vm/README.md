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

## Known guest quirks

These affect experiments that run a full Plasma session in the guest (nested Plasma, remote sessions).

- **Enable linger.** Run `sudo loginctl enable-linger prism`. Without it logind stops `user@1000` ten
  seconds after the last SSH session closes, and Plasma's `exit.target` takes every user unit down
  with it. This looks like a Plasma crash but is not.
- **SELinux and UPower.** With SELinux enforcing, polkit is denied D-Bus access to the `ssh` login's
  processes, `upowerd` aborts and `plasmashell` hangs on its synchronous UPower calls (no panel
  appears). The experiments used `sudo setenforce 0` (runtime only, reverts on reboot) and started
  `upowerd` with `sudo systemd-run /usr/libexec/upowerd`, because `upower.service` itself fails with
  `203/EXEC`. The root cause is not identified yet.
- **Nested KWin size.** Started under another Wayland compositor, KWin opens a 1024x768 window.
  `kwin_wayland --width W --height H --fullscreen true` fixes that, but `startplasma-wayland` does not
  forward those options.
- **No `wtype` for keyboard tests.** It swaps the virtual keyboard keymap for every character, which
  garbles input once a nested compositor forwards it. Use Prism's input bridge instead.

Results and details: `docs/roadmap/spikes/S1-nested-plasma.md`.
