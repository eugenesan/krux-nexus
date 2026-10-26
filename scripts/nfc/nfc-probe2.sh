echo "=== shell paths to NFC ==="
echo "  cmd nfc:"; cmd nfc 2>&1 | head -5 | sed 's/^/    /'
echo "  nfc binder services:"; service list 2>/dev/null | grep -i nfc | sed 's/^/    /'
echo
echo "=== getevent available (fix for the 0-byte touch capture)? ==="
echo "  $(command -v getevent)"
echo
echo "=== what actually claimed/dropped the tag ==="
logcat -b all -d 2>/dev/null | grep -iE 'dispatchTag|Tag lost|techList|ActivityNfc|dispatchDisable' | tail -8
echo
echo "=== full picture: every distinct NFC log string seen ==="
logcat -b all -d 2>/dev/null | grep -E 'NfcNciHal|libnfc_nci|NfcDispatcher|NativeNfcTag' \
  | sed -E 's/.*: //' | sed 's/[0-9]\+//g' | sort -u | head -30