echo "=== gadget-side ACM (g_acm) ==="
grep -E ' (g_acm|acm_alloc|acm_bind|acm_set_alt)' /proc/kallsyms | head -8
echo "  count: $(grep -cE ' (g_acm|acm_alloc|acm_bind|acm_set_alt)' /proc/kallsyms)"
echo
echo "=== adb gadget (f_adb) ==="
grep -E ' (adb_function_init|adb_android_function_enable|adb_open|adb_ep0)' /proc/kallsyms | head -6
echo "  count: $(grep -cE ' adb_' /proc/kallsyms)"
echo
echo "=== FunctionFS (f_ffs) ==="
echo "  count: $(grep -cE ' ffs_' /proc/kallsyms)"
echo
echo "=== android_usb configfs driver ==="
grep -E ' (android_usb|g_android)' /proc/kallsyms | head -5
echo "  count: $(grep -cE ' android_usb' /proc/kallsyms)"
echo
echo "=== f_hid / f_serial / f_ccid backing? ==="
echo "  hid:   $(grep -cE ' f_hid' /proc/kallsyms)"
echo "  serial:$(grep -cE ' g_serial' /proc/kallsyms)"
echo "  ccid:  $(grep -cE ' (ccid|usb_ccid)' /proc/kallsyms)"
