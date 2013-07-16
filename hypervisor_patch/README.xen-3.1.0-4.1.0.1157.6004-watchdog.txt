watchdog multiple instances patch

1. Summary
==============

This is a patch for xen-3.1.0-4.1.0.1157.6004.src.rpm
This patch is implementation of 2 watchdog timer instances per domain. 
This patch can be apply to xen-3.1.0-4.1.0.1168.6013.src.rpm

2. How to apply this patch
==========================
on the top directory of the xen-3.1.0 source tree
$ patch -p1 < xen-3.1.0-4.1.0.1157.6004-watchdog-multi-instances.patch
