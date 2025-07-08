#!/usr/bin/env bash
# ------------------------------------------------------------
# create-dmg.sh : wrap an .app bundle into a pretty DMG
# ------------------------------------------------------------
set -euo pipefail

APP_NAME="KRStudio.app"   # <<<EDIT>>> bundle name if different
VOL_NAME="KRStudio"
DMG_NAME="${VOL_NAME}-mac.dmg"
SOURCE_DIR="$(pwd)/build/bin"     # wherever macdeployqt placed the .app
STAGING_DIR="$(mktemp -d)"

# 1) copy the bundle into a staging folder
cp -R "${SOURCE_DIR}/${APP_NAME}" "${STAGING_DIR}/"

# 2) create DMG
hdiutil create "${DMG_NAME}" \
  -volname "${VOL_NAME}" \
  -fs APFS \
  -srcfolder "${STAGING_DIR}" \
  -ov

echo "Created ${DMG_NAME}"
