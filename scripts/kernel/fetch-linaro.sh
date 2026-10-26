#!/bin/bash
set -uo pipefail
DEST=/home/user/Develop/src/android-air-gapped/standalone/vendor/toolchain
URL="https://releases.linaro.org/components/toolchain/binaries/15.2-2023.12/arm-none-eabi/gcc-arm-none-eabi-15.2.rel1-x86_64-arm-none-eabi.tar.xz"
cd "$DEST"
for try in 1 2 3; do
  echo "attempt $try: $URL"
  if curl -fL --retry 5 --retry-delay 5 --connect-timeout 30 --max-time 1800 \
        -o linaro.tar.xz "$URL"; then
    sz=$(stat -c%s linaro.tar.xz)
    echo "downloaded $sz bytes"
    if [ "$sz" -gt 10000000 ] && tar tf linaro.tar.xz >/dev/null 2>&1; then
      tar xf linaro.tar.xz && rm -f linaro.tar.xz
      d=$(ls -d gcc-arm-none-eabi-15.2.rel1* 2>/dev/null | head -1)
      echo "unpacked: $d"
      "$d/bin/arm-none-eabi-gcc" --version | head -1
      exit 0
    fi
    echo "  got a bad file ($sz bytes), retrying"
  fi
  rm -f linaro.tar.xz
  sleep 10
done
echo "!! Linaro toolchain could not be fetched" >&2
