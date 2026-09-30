#!/usr/bin/env bash
# Boots an iPhone simulator, installs the built Seqwenser.app and captures screenshots of the key screens.
# usage: ci_screenshots.sh <UDID> <path/to/Seqwenser.app> <out-dir>
set -euo pipefail
UDID="$1"; APP="$2"; OUT="$3"
BUNDLE=com.dkimoto.seqwenser
mkdir -p "$OUT"
xcrun simctl bootstatus "$UDID" -b >/dev/null 2>&1 || xcrun simctl boot "$UDID"
xcrun simctl ui "$UDID" appearance dark || true
xcrun simctl status_bar "$UDID" override --time "9:41" --batteryState charged --batteryLevel 100 --cellularBars 4 || true
xcrun simctl install "$UDID" "$APP"
xcrun simctl privacy "$UDID" grant microphone "$BUNDLE" || true

shot() {   # name, args...
  local name="$1"; shift
  xcrun simctl terminate "$UDID" "$BUNDLE" >/dev/null 2>&1 || true
  xcrun simctl launch "$UDID" "$BUNDLE" "$@" >/dev/null
  sleep 4
  xcrun simctl io "$UDID" screenshot "$OUT/seqwenser-$name.png" >/dev/null
  echo "captured $name"
}
shot main
shot main-playing -seqw-play
shot sample   -seqw-sheet sample
shot step-fx  -seqw-sheet step
shot pattern  -seqw-sheet pattern
shot projects -seqw-sheet projects
shot export   -seqw-sheet export
ls -la "$OUT"
