#!/usr/bin/env bash
# Guest-side smoke test, run as root by "prism-vm.sh smoke".
#
# Flow: boot with nobody logged in -> Prism starts -> pair a client -> stream -> a frame arrives.
# Each stage prints PASS, FAIL or SKIP. SKIP marks stages that need features not built yet
# (system service, remote sessions); they must turn into PASS as the roadmap milestones land.
set -u

fails=0
pass() { printf 'PASS  %s\n' "$1"; }
fail() {
  printf 'FAIL  %s\n' "$1"
  fails=$((fails + 1))
}
skip() { printf 'SKIP  %s (%s)\n' "$1" "$2"; }

check() {
  local what="$1"
  shift
  if "$@" >/dev/null 2>&1; then pass "$what"; else fail "$what"; fi
}

# 1. Environment: the guest is a KDE host at the login screen with no local user session.
check "graphical target is the default" test "$(systemctl get-default)" = graphical.target
check "sddm is running" systemctl is-active --quiet sddm
local_users=$(loginctl list-sessions --no-legend | while read -r id _ _ seat _; do
  [ "$seat" = seat0 ] && [ "$(loginctl show-session "$id" -p Class --value)" = user ] && echo "$id"
done)
if [ -n "$local_users" ]; then
  fail "no local user session on seat0"
else
  pass "no local user session on seat0"
fi
check "plasma is installed" test -x /usr/bin/startplasma-wayland
check "moonlight client is installed" flatpak info com.moonlight_stream.Moonlight

# 2. Prism is installed and its binary resolves all libraries.
if [ -x /usr/bin/prism ]; then
  pass "prism binary installed"
  if ldd /usr/bin/prism 2>&1 | grep -q 'not found'; then
    fail "prism libraries resolve"
    ldd /usr/bin/prism 2>&1 | grep 'not found'
  else
    pass "prism libraries resolve"
  fi
else
  fail "prism binary installed"
fi

# 3. Remote connection: needs the system service (T3.x) and remote sessions (M2/M3).
if systemctl cat prismd.service >/dev/null 2>&1; then
  check "prismd is running" systemctl is-active --quiet prismd
  skip "pair client and stream" "client automation lands with the session tasks"
else
  skip "prismd is running" "system service not implemented yet"
  skip "pair client and stream" "system service not implemented yet"
  skip "frame received without local login" "system service not implemented yet"
fi

printf '\n%d failure(s)\n' "$fails"
[ "$fails" -eq 0 ]
