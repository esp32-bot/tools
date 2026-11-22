#!/usr/bin/env bash
# esp32-bot one-liner developer bootstrap:
#   curl -fsSL https://esp32-bot.github.io/tools/install.sh | bash
set -euo pipefail

DEST="$HOME/gh/esp32-bot"
mkdir -p "$DEST"

clone_or_pull() {
  local repo="$1"
  local dir="$2"
  if [[ -d "$DEST/$dir/.git" ]]; then
    echo "Updating $DEST/$dir..."
    git -C "$DEST/$dir" pull --ff-only || true
  else
    echo "Cloning esp32-bot/$repo -> $DEST/$dir..."
    git clone "https://github.com/esp32-bot/$repo.git" "$DEST/$dir"
  fi
}

clone_or_pull "tools" "tools"
clone_or_pull "BalaX" "balax"
clone_or_pull "JoyX" "joyx"
clone_or_pull "balax-remote" "balax-remote"

echo ""
echo "✓ esp32-bot workspace ready in $DEST"
echo "Launching BalaX Studio GUI..."
python3 "$DEST/balax/tools/gui.py"
