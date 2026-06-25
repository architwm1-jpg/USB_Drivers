#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>

//USB init IDS
#define MY_VENDOR_ID 0x03f0
#define MY_PRODUCT_ID 0x5a07


//Data Read Macros
#define BULK_IN_EP 0x81
#define BULK_OUT_EP 0x02

#define BULK_BUF_SIZE 512


#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355


struct usb_device_id usbdrv_table[] = {
	{ USB_DEVICE(MY_VENDOR_ID, MY_PRODUCT_ID)},
	{}
};
MODULE_DEVICE_TABLE(usb, usbdrv_table);

struct ctrl_ctx{
	struct usb_ctrlrequest *req;
	unsigned char *buf;
};


//Data Read
static void do_bulk_read(struct usb_device *udev);

//command block wrapper
struct __attribute__((packed)) cbw {
	__le32 signature;		// must be CBW_SIGNATURE
	__le32 tag;			// match request/response
	__le32 data_transfer_length;	// many bytes of data in the data stage
	u8     flags;			//bit 7: 1=IN 
	u8     lun;			//logical unit number, 0 for simple devices
	u8     cb_length;		// length of the actual SCSI command below
	u8     cb[16];			//the actual SCSI Command Descriptor Block
};

//Command Status Wrapper 

struct __attribute__((packed)) csw {
    __le32 signature;		//must be CSW_SIGNATURE
    __le32 tag;			//should match the CBW tag we sent
    __le32 data_residue;	//bytes not transferred 
    u8 status;			// 0 =success, 1=failed, 2= phase error
};

static void do_scsi_inquiry(struct usb_device *udev)
{
	struct cbw *cbw_buf;
	struct csw *csw_buf;
	unsigned char *data_buf;
	int actual, ret;
	static u32 tag_counter = 1;

	cbw_buf = kmalloc(sizeof(struct cbw), GFP_KERNEL);
	csw_buf = kmalloc(sizeof(struct csw), GFP_KERNEL);

	data_buf = kmalloc(36, GFP_KERNEL);

	if(!cbw_buf || !csw_buf || !data_buf) goto cleanup;

	memset(cbw_buf, 0, sizeof(struct cbw));
	cbw_buf->signature = cpu_to_le32(CBW_SIGNATURE);
	cbw_buf->tag = cpu_to_le32(tag_counter++);
    	cbw_buf->data_transfer_length = cpu_to_le32(36);
    	cbw_buf->flags = 0x80;        // IN: device will send us data
    	cbw_buf->lun = 0;
    	cbw_buf->cb_length = 6;       // INQUIRY CDB is 6 bytes

    // SCSI INQUIRY command: opcode 0x12, then mostly zeros, last byte = allocation length
    	cbw_buf->cb[0] = 0x12;        // INQUIRY opcode
    	cbw_buf->cb[4] = 36;          // allocation length = 36 bytes

    // --- Stage 1: send CBW out ---
    	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_EP),
                        cbw_buf, sizeof(struct cbw), &actual, 5000);
    	printk(KERN_INFO "usbdrv: CBW send ret=%d actual=%d\n", ret, actual);
    	if (ret) goto cleanup;

    // --- Stage 2: read data IN ---
    	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP),
                        data_buf, 36, &actual, 5000);
    	printk(KERN_INFO "usbdrv: data stage ret=%d actual=%d\n", ret, actual);
    	if (ret == 0) {
        	print_hex_dump(KERN_INFO, "usbdrv inquiry: ", DUMP_PREFIX_OFFSET,
                       16, 1, data_buf, actual, true);
    	}

    // --- Stage 3: read CSW (status) ---
    	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP),
                        csw_buf, sizeof(struct csw), &actual, 5000);
    	printk(KERN_INFO "usbdrv: CSW ret=%d actual=%d status=%d residue=%u\n",
           ret, actual, csw_buf->status, le32_to_cpu(csw_buf->data_residue));

cleanup:
    kfree(cbw_buf);
    kfree(csw_buf);
    kfree(data_buf);

}

static void do_scsi_read10(struct usb_device *udev, u32 lba, u16 num_blocks)
{
  struct cbw *cbw_buf;
  struct csw *csw_buf;
  unsigned char *data_buf;
  int actual, ret;
  static u32 tag_counter = 100;
  u32 data_len = num_blocks*512;

  cbw_buf = kmalloc(sizeof(struct cbw), GFP_KERNEL);
  csw_buf = kmalloc(sizeof(struct csw), GFP_KERNEL);
  data_buf =kmalloc(data_len,GFP_KERNEL);
  if(!cbw_buf || !csw_buf || !data_buf) goto cleanup;

  memset(cbw_buf, 0, sizeof(struct cbw));
  cbw_buf->signature = cpu_to_le32(CBW_SIGNATURE);
  cbw_buf->tag = cpu_to_le32(tag_counter++);
  cbw_buf->data_transfer_length = cpu_to_le32(data_len);
  cbw_buf->flags = 0x80;
  cbw_buf->lun = 0;
  cbw_buf->cb_length = 10;

  cbw_buf->cb[0] = 0x28;
  cbw_buf->cb[1] = 0x00;
  cbw_buf->cb[2] = (lba >> 24) & 0xff;
  cbw_buf->cb[3] = (lba >> 16) & 0xff;
  cbw_buf->cb[4] = (lba >> 8) & 0xff;
  cbw_buf->cb[5] = lba & 0xff;
  cbw_buf->cb[6] = 0x00;
  cbw_buf->cb[7] = (num_blocks >> 8) & 0xff;
  cbw_buf->cb[8] = num_blocks & 0xff;
  cbw_buf->cb[9] = 0x00;
  
  ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_EP), cbw_buf, sizeof(struct cbw), &actual, 5000);
  printk(KERN_INFO "usrdev: READ10 CBW ret=%d actual=%d\n", ret, actual);

  if (ret) goto cleanup;

  ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP), data_buf, data_len, &actual, 5000);
  printk(KERN_INFO "usbdrv: READ10 data ret=%d actual=%d\n", ret, actual);
  if(ret == 0)
  {
  	print_hex_dump(KERN_INFO, "usbdrv sector: ", DUMP_PREFIX_OFFSET,16,1,data_buf, min(actual,64), true);
  }

  ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP), csw_buf, sizeof(struct csw), &actual, 5000);
  printk(KERN_INFO "usbdrv: READ10 CSW ret=%d status=%d residue=%u\n", ret,csw_buf->status, le32_to_cpu(csw_buf->data_residue));
cleanup:
  kfree(cbw_buf);
  kfree(csw_buf);
  kfree(data_buf);



}

static void do_scsi_write10(struct usb_device *udev, u32 lba, u16 num_blocks, unsigned char *pattern)
{
 struct cbw *cbw_buf;
 struct csw *csw_buf;
 unsigned char *data_buf;
 int actual, ret;
 static u32 tag_counter = 200;
 u32 data_len = num_blocks * 512;

 cbw_buf = kmalloc(sizeof(struct cbw), GFP_KERNEL);
 csw_buf = kmalloc(sizeof(struct csw), GFP_KERNEL);
 data_buf = kmalloc(data_len, GFP_KERNEL);
 if(!cbw_buf || !csw_buf || !data_buf) goto cleanup;

 memset(data_buf,0,data_len);
 memcpy(data_buf, pattern, min_t(u32, data_len, 512));

 memset(cbw_buf, 0, sizeof(struct cbw));
 cbw_buf->signature = cpu_to_le32(CBW_SIGNATURE);
 cbw_buf->tag = cpu_to_le32(tag_counter++);
 cbw_buf->data_transfer_length = cpu_to_le32(data_len);
 cbw_buf->flags =0x00;
 cbw_buf->lun = 0;
 cbw_buf->cb_length =10;

 cbw_buf->cb[0] = 0x2A;
 cbw_buf->cb[1] = 0x00;
 cbw_buf->cb[2] = (lba >> 24) & 0xff;
 cbw_buf->cb[3] = (lba >> 16) & 0xff;
 cbw_buf->cb[4] = (lba >> 8) & 0xff;
 cbw_buf->cb[5] = lba & 0xff;
 cbw_buf->cb[6] = 0x00;
 cbw_buf->cb[7] = (num_blocks >> 8) & 0xff;
 cbw_buf->cb[8] = num_blocks & 0xff;
 cbw_buf->cb[9] = 0x00;

 ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_EP), cbw_buf,sizeof(struct cbw), &actual, 5000);
 printk(KERN_INFO "usbdrv:Write10 CBW ret=%d actual=%d\n", ret,actual);
 if(ret) goto cleanup;
 
    ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_EP),
                        data_buf, data_len, &actual, 5000);
    printk(KERN_INFO "usbdrv: WRITE10 data ret=%d actual=%d\n", ret, actual);
    if (ret) goto cleanup;

    
    ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP),
                        csw_buf, sizeof(struct csw), &actual, 5000);
    printk(KERN_INFO "usbdrv: WRITE10 CSW ret=%d status=%d residue=%u\n",
           ret, csw_buf->status, le32_to_cpu(csw_buf->data_residue));

cleanup:
    kfree(cbw_buf);
    kfree(csw_buf);
    kfree(data_buf);

}

static void read_complete(struct urb *urb)
{
   struct ctrl_ctx *ctx = urb->context;

   if (urb->status == 0)
   {
      printk(KERN_INFO "usbdrv: got %d bytes\n", urb->actual_length);
		      print_hex_dump(KERN_INFO, "usbdrv data: ", DUMP_PREFIX_OFFSET, 16, 1, ctx->buf, urb->actual_length, true);
   }

   kfree(ctx->buf);
   kfree(ctx->req);
   kfree(ctx);
   usb_free_urb(urb);
}


static void do_bulk_read(struct usb_device *udev)
{
	unsigned char *bulk_buf;
	int actual_length;
	int ret;

	bulk_buf = kmalloc(BULK_BUF_SIZE, GFP_KERNEL);
	if (!bulk_buf)
	{
	printk(KERN_ERR "usbdrv: bulk buf alloc failed\n");
	return;
	}

	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_EP),
			bulk_buf,
			BULK_BUF_SIZE,
			&actual_length,5000);
	
	if (ret)
	{
	printk(KERN_ERR "usbdrv: bulk read failed, ret=%d\n", ret);
	}
	else {
		printk(KERN_INFO "usbdrv: bulk read got %d bytes\n", actual_length);
		print_hex_dump(KERN_INFO, "usbdrv bulk: ", DUMP_PREFIX_OFFSET, 16, 1, bulk_buf, actual_length, true);
	}
	kfree(bulk_buf);
}


static int usbdrv_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct urb *urb;
	struct ctrl_ctx *ctx;
	int ret;

	printk(KERN_INFO "usbdrv: device connected (vid=%04x pid=%04x)\n",
			id->idVendor, id->idProduct);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
	   return -ENOMEM;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx){
		usb_free_urb(urb);
		return -ENOMEM;
	}
	ctx->buf = kmalloc(18, GFP_KERNEL);
	ctx->req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);

	if (!ctx || !ctx->buf || !ctx->req)
	{
	kfree(ctx->buf);
	kfree(ctx->req);
	kfree(ctx);
	usb_free_urb(urb);
	return -ENOMEM;
	}

	ctx->req->bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        ctx->req->bRequest = USB_REQ_GET_DESCRIPTOR;
	ctx->req->wValue = cpu_to_le16(USB_DT_DEVICE << 8);
	ctx->req->wIndex = cpu_to_le16(0);
	ctx->req->wLength = cpu_to_le16(18);
	
	usb_fill_control_urb(urb, udev, usb_rcvctrlpipe(udev, 0),
	(unsigned char *)ctx->req, ctx->buf, 18,read_complete, ctx);

	ret = usb_submit_urb(urb, GFP_KERNEL);

	if (ret)
	{
	    printk(KERN_ERR "usbdrv: submit failed, ret=%d\n", ret);
	    kfree(ctx->buf);
	    kfree(ctx->req);
	    kfree(ctx);
	    usb_free_urb(urb);
	    return ret;
	}
	do_scsi_inquiry(udev);
	do_scsi_read10(udev, 0,1);

	unsigned char pattern[512];

	memset(pattern, 'A', sizeof(pattern));

	do_scsi_write10(udev, 1000, 1, pattern);
	do_scsi_read10(udev, 1000, 1);
	//do_bulk_read(udev);	
	return 0;
}



static void usbdrv_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "usbdrv: device disconnected\n");

}

static struct usb_driver usbdrv = {
	.name = "usbdrv",
	.id_table = usbdrv_table,
	.probe = usbdrv_probe,
	.disconnect = usbdrv_disconnect,
};



module_usb_driver(usbdrv);
MODULE_LICENSE("GPL");
