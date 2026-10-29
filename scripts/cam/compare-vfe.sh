#!/usr/bin/env bash
# Extract VFE params from a dmesg capture file and print a comparison table.
# Usage: bash scripts/cam/compare-vfe.sh <dmesg_file>
set -e
FILE="${1:?Usage: compare-vfe.sh <dmesg_file>}"
cd "$(dirname "$0")/../.."

echo "=== VFE Parameter Extraction ==="
echo "File: $FILE"
echo ""

echo "hw_version:"
grep -o "VFE40_DBG: hw_version=0x[0-9a-f]*" "$FILE" | sort -u

echo "vfe_clk_src:"
grep -o "VFE40_DBG: vfe_clk_src=[0-9]*" "$FILE" | sort -u

echo "camss_csi_vfe_clk:"
grep -o "VFE40_DBG: camss_csi_vfe_clk=[0-9]*" "$FILE" | sort -u

echo "WM_IRQ_MASK:"
grep -o "VFE40_DBG: WM_IRQ_MASK [a-z]*=0x[0-9a-f]*" "$FILE" | sort -u

echo "ISP-IF INPUT_SEL:"
grep -o "ISPIF_DBG: INPUT_SEL vfe=[0-9]* intftype=[0-9]* csid=[0-9]* val=0x[0-9a-f]*" "$FILE" | sort -u

echo "Overflow count:"
grep -c "pix0 overflow" "$FILE" || echo "0"

echo "halt timeout:"
grep -c "halt timeout" "$FILE" || echo "0"

echo "NULL stream:"
grep -c "NULL stream" "$FILE" || echo "0"
EOF
chmod +x scripts/cam/compare-vfe.sh
echo "created"