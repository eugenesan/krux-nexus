# rootfs/

Root filesystem build and boot image packing. build.sh builds buildroot;
make-boot-image.sh packs kernel + ramdisk into a boot image. stock/ was
dropped; built images are in artifacts/built/.