# 功能测试脚本
dd if=/dev/zero of=arco.img bs=1024 count=16
./mkarcofs arco.img

echo "8       8       8       8" > /proc/sys/kernel/printk
md5sum arcofs.ko
rmmod arcofs.ko
insmod arcofs.ko
mkdir mnt
mount -o loop -t arcofs arco.img mnt
