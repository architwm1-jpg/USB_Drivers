# USB_Drivers
Implemented a Linux kernel USB driver for a USB Mass Storage device using the Linux USB subsystem.
# Linux USB Mass Storage Driver

Implemented a Linux kernel USB driver for a USB Mass Storage device using the Linux USB subsystem. The driver performs USB control transfers to retrieve device descriptors and implements the Bulk-Only Transport  protocol by constructing and transmitting Command Block Wrappers , processing SCSI INQUIRY commands, receiving data through bulk endpoints, and validating Command Status Wrappers . Developed and tested as a loadable kernel module on Ubuntu using the Linux kernel USB framework.
