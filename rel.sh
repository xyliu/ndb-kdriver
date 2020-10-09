mkdir -p /lib/modules/`uname -r`/kernel/drivers/target
cp ndb.ko /lib/modules/`uname -r`/kernel/drivers/target/ndb.ko
md5sum  /lib/modules/`uname -r`/kernel/drivers/target/ndb.ko
# echo ndb >> /etc/modules
depmod
