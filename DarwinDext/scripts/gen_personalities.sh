#!/bin/bash
# Generate IOKitPersonalities entries for every USB VID/PID matched by any brltty braille driver.
# Usage: ./gen_personalities.sh <brltty-root> > Personalities.plist
set -euo pipefail

ROOT="${1:-../..}"
DRIVERS="$ROOT/Drivers/Braille"

if [ ! -d "$DRIVERS" ]; then
    echo "error: $DRIVERS not found" >&2
    exit 1
fi

# Extract unique (vendor, product) pairs from all driver sources.
pairs=$(grep -rhE '\.vendor=0X[0-9A-Fa-f]+,[[:space:]]*\.product=0X[0-9A-Fa-f]+' "$DRIVERS" \
  | grep -oE '\.vendor=0X[0-9A-Fa-f]+,[[:space:]]*\.product=0X[0-9A-Fa-f]+' \
  | sed -E 's/\.vendor=0X([0-9A-Fa-f]+),[[:space:]]*\.product=0X([0-9A-Fa-f]+)/\1 \2/' \
  | tr '[:upper:]' '[:lower:]' \
  | sort -u)

cat <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
EOF

while read -r vendor product; do
    [ -z "$vendor" ] && continue
    vdec=$((16#$vendor))
    pdec=$((16#$product))
    key="brltty-${vendor}-${product}"
    cat <<EOF
  <key>${key}</key>
  <dict>
    <key>CFBundleIdentifier</key>
    <string>com.brltty.usb-driver</string>
    <key>CFBundleIdentifierKernel</key>
    <string>com.apple.kpi.iokit</string>
    <key>IOClass</key>
    <string>IOUserService</string>
    <key>IOProviderClass</key>
    <string>IOUSBHostDevice</string>
    <key>IOUserClass</key>
    <string>BrlttyUSBDriver</string>
    <key>IOUserServerName</key>
    <string>com.brltty.usb-driver</string>
    <key>IOUserServerOneProcess</key>
    <true/>
    <key>idVendor</key>
    <integer>${vdec}</integer>
    <key>idProduct</key>
    <integer>${pdec}</integer>
    <key>IOProbeScore</key>
    <integer>100000</integer>
  </dict>
EOF
done <<< "$pairs"

cat <<'EOF'
</dict>
</plist>
EOF
