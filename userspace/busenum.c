/*++

Copyright (c) 1990-2000  Microsoft Corporation All Rights Reserved

Module Name:

    BUSENUM.C

Abstract:

    This module contains the entry points for a toaster bus driver.

Author:


Environment:

    kernel mode only

Revision History:

    Cleaned up sample 05/05/99
    Fixed the create_close and ioctl handler to fail the request 
    sent on the child stack - 3/15/04


--*/

#include "busenum.h"


//
// Global Debug Level
//

ULONG BusEnumDebugLevel = BUS_DEFAULT_DEBUG_OUTPUT_LEVEL;


GLOBALS Globals;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, Bus_DriverUnload)
#pragma alloc_text (PAGE, Bus_Create)
#pragma alloc_text (PAGE, Bus_Close)
#pragma alloc_text (PAGE, Bus_Cleanup)
#pragma alloc_text (PAGE, Bus_Read)
#pragma alloc_text (PAGE, Bus_Write)
#pragma alloc_text (PAGE, Bus_IoCtl)
#endif

NTSTATUS
DriverEntry (
    __in  PDRIVER_OBJECT  DriverObject,
    __in  PUNICODE_STRING RegistryPath
    )
/*++
Routine Description:

    Initialize the driver dispatch table.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

  NT Status Code

--*/
{

    Bus_KdPrint_Def (BUS_DBG_SS_TRACE, ("Driver Entry \n"));

    //
    // Save the RegistryPath for WMI.
    //

    Globals.RegistryPath.MaximumLength = RegistryPath->Length +
                                          sizeof(UNICODE_NULL);
    Globals.RegistryPath.Length = RegistryPath->Length;
    Globals.RegistryPath.Buffer = ExAllocatePoolWithTag(
                                       PagedPool,
                                       Globals.RegistryPath.MaximumLength,
                                       BUSENUM_POOL_TAG
                                       );

    if (!Globals.RegistryPath.Buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }


    KdPrint(("ReistryPath %p\r\n", RegistryPath));
    RtlCopyUnicodeString(&Globals.RegistryPath, RegistryPath);

    //
    // Set entry points into the driver
    //
    DriverObject->MajorFunction [IRP_MJ_CREATE] = Bus_Close;
    DriverObject->MajorFunction [IRP_MJ_CLEANUP] = Bus_Cleanup;
    DriverObject->MajorFunction [IRP_MJ_CLOSE] = Bus_Create;
    DriverObject->MajorFunction [IRP_MJ_READ] = Bus_Read;
    DriverObject->MajorFunction [IRP_MJ_WRITE] = Bus_Write;
    DriverObject->MajorFunction [IRP_MJ_PNP] = Bus_PnP;
    DriverObject->MajorFunction [IRP_MJ_POWER] = Bus_Power;
    DriverObject->MajorFunction [IRP_MJ_DEVICE_CONTROL] = Bus_IoCtl;
    DriverObject->MajorFunction [IRP_MJ_INTERNAL_DEVICE_CONTROL] = Bus_Internal_IoCtl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Bus_SystemControl;
    DriverObject->DriverUnload = Bus_DriverUnload;
    DriverObject->DriverExtension->AddDevice = Bus_AddDevice;

    KdPrint(("load ok"));

    return STATUS_SUCCESS;
}

#include "code2name.c"

struct usbip_header_basic {
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004
#define USBIP_RESET_DEV		0xFFFF
	unsigned int command;

	 /* sequencial number which identifies requests.
	  * incremented per connections */
	unsigned int seqnum;

	unsigned int devid;

#define USBIP_DIR_OUT	0
#define USBIP_DIR_IN	1
	unsigned int direction;
	unsigned int ep;     /* endpoint number */
};

/*
 * An additional header for a CMD_SUBMIT packet.
 */
struct usbip_header_cmd_submit {
	/* these values are basically the same as in a URB. */

	/* the same in a URB. */
	unsigned int transfer_flags;

	/* set the following data size (out),
	 * or expected reading data size (in) */
	int transfer_buffer_length;

	/* it is difficult for usbip to sync frames (reserved only?) */
	int start_frame;

	/* the number of iso descriptors that follows this header */
	int number_of_packets;

	/* the maximum time within which this request works in a host
	 * controller of a server side */
	int interval;

	/* set setup packet data for a CTRL request */
	unsigned char setup[8];
};

/*
 * An additional header for a RET_SUBMIT packet.
 */
struct usbip_header_ret_submit {
	int status;
	int actual_length; /* returned data length */
	int start_frame; /* ISO and INT */
	int number_of_packets;  /* ISO only */
	int error_count; /* ISO only */
};

/*
 * An additional header for a CMD_UNLINK packet.
 */
struct usbip_header_cmd_unlink {
	unsigned int seqnum; /* URB's seqnum which will be unlinked */
};


/*
 * An additional header for a RET_UNLINK packet.
 */
struct usbip_header_ret_unlink {
	int status;
};


/* the same as usb_iso_packet_descriptor but packed for pdu */
struct usbip_iso_packet_descriptor {
	unsigned int offset;
	unsigned int length;            /* expected length */
	unsigned int actual_length;
	unsigned int status;
};


/*
 * All usbip packets use a common header to keep code simple.
 */
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
};

void try_save_config(PPDO_DEVICE_DATA pdodata, struct _URB_CONTROL_DESCRIPTOR_REQUEST *req)
{
	PUSB_CONFIGURATION_DESCRIPTOR cfg;
	if(req->Hdr.Function != URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE||
		req->DescriptorType!=USB_CONFIGURATION_DESCRIPTOR_TYPE)
		return;
	cfg=(PUSB_CONFIGURATION_DESCRIPTOR) req->TransferBuffer;
	if(req->TransferBufferLength<sizeof(*cfg))
		return;
	if(cfg->bDescriptorType!=USB_CONFIGURATION_DESCRIPTOR_TYPE||
			cfg->wTotalLength!=req->TransferBufferLength)
		return;
	KdPrint(("save config for using when select config\n"));
	pdodata->dev_config = ExAllocatePoolWithTag(
		NonPagedPool,
		req->TransferBufferLength, BUSENUM_POOL_TAG);
	if(!pdodata->dev_config){
		KdPrint(("Warning, can't malloc %d bytes\n",
					req->TransferBufferLength));
		return;
	}
	RtlCopyMemory(pdodata->dev_config, req->TransferBuffer,
			req->TransferBufferLength);
	return;
}

#define EPIPE 32
#define EOVERFLOW 75
#define EREMOTEIO 121

unsigned int tran_usb_status(int linux_status,int in, int type)
{
	switch(linux_status){
		case 0:
			return USBD_STATUS_SUCCESS;
		/* I guess it */
		case -EPIPE:
			return USBD_STATUS_ENDPOINT_HALTED;
		case -EOVERFLOW:
			return USBD_STATUS_DATA_OVERRUN;
		case -EREMOTEIO:
			return USBD_STATUS_ERROR_SHORT_TRANSFER;
		default:
			KdPrint(("linux status %d in %d type %d\n", linux_status,
				in, type));
			return USBD_STATUS_ERROR;
	}
	return USBD_STATUS_SUCCESS;
}

#define INLINE __inline

static USBD_PIPE_HANDLE INLINE make_pipe(unsigned char ep,
				unsigned char type,
				unsigned char interval)
{
	return   (USBD_PIPE_HANDLE) (ep|(interval<<8)|(type<<16));
}

static unsigned char INLINE pipe2direct(USBD_PIPE_HANDLE handle)
{
	return ((unsigned long)handle & 0x80)?USBIP_DIR_IN:USBIP_DIR_OUT;
}

static unsigned char INLINE pipe2addr(USBD_PIPE_HANDLE handle)
{
	return (unsigned char)((unsigned long)handle & 0x7f);
}

static unsigned char INLINE pipe2type(USBD_PIPE_HANDLE handle)
{
	return (unsigned char)(((unsigned long)handle & 0xff0000)>>16);
}

static unsigned char INLINE pipe2interval(USBD_PIPE_HANDLE handle)
{
	return (unsigned char)(((unsigned long)handle & 0xff00)>>8);
}
int post_select_interface(PPDO_DEVICE_DATA pdodata,
		struct _URB_SELECT_INTERFACE * req);


static void copy_iso_data(char *dest, int dest_len,
		char *src, int src_len, struct _URB_ISOCH_TRANSFER *urb)
{
	int i;
	int offset;
	offset=0;
	for(i=0;i<urb->NumberOfPackets;i++){
		if(!urb->IsoPacket[i].Length)
			continue;

		if(urb->IsoPacket[i].Offset+urb->IsoPacket[i].Length
				> dest_len){
			KdPrint(("Warning, why this?"));
			break;
		}
		RtlCopyMemory(dest + urb->IsoPacket[i].Offset,
				src + offset, urb->IsoPacket[i].Length);
		offset+=urb->IsoPacket[i].Length;
	}
	if(offset!=src_len)
		KdPrint(("Warning, why not equal offset:%d src_len:%d",
					offset,src_len));
	return;
}

int process_write_irp(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
    KIRQL oldirql;
    PLIST_ENTRY le = NULL;
    ULONG len;
    PIO_STACK_LOCATION irpstack;
    struct usbip_header *h;
    PIRP ioctl_irp;
    char *buf, *urb_buf;
    int in, type, i;
    /* This is a quick hack, in windows, the offsets of all types of
     * TansferFlags and TransferBuffer and TransferBufferLength are the same,
     * so we just use _URB_ISOCH_TRANSFER */
    struct _URB_ISOCH_TRANSFER *urb;
    struct usbip_iso_packet_descriptor * ip_desc;
    NTSTATUS ioctl_status = STATUS_INVALID_PARAMETER;
    int found=0, iso_len=0, in_len=0;

    irpstack = IoGetCurrentIrpStackLocation (irp);
    len = irpstack->Parameters.Write.Length;
    if(len<sizeof(*h)){
	    KdPrint(("write, small len %d\n", len));
	    return STATUS_INVALID_PARAMETER;
    }
    h = irp->AssociatedIrp.SystemBuffer;
    KeAcquireSpinLock(&pdodata->wait_q_lock, &oldirql);
    for (le = pdodata->wait_q.Flink;
         le != &pdodata->wait_q;
         le = le->Flink) {
        ioctl_irp = CONTAINING_RECORD (le, IRP, Tail.Overlay.ListEntry);
	if(ioctl_irp->Tail.Overlay.DriverContext[0]==(PVOID)h->base.seqnum){
		found=1;
                RemoveEntryList (&ioctl_irp->Tail.Overlay.ListEntry);
                InitializeListHead(&ioctl_irp->Tail.Overlay.ListEntry);
		break;
	}
    }
    KeReleaseSpinLock(&pdodata->wait_q_lock, oldirql);
    if(!found){
	    KdPrint(("can't found %d\n", h->base.seqnum));
	    return STATUS_INVALID_PARAMETER;
    }
    irp->IoStatus.Information = len;
    irpstack = IoGetCurrentIrpStackLocation(ioctl_irp);

    switch ( irpstack->Parameters.DeviceIoControl.IoControlCode ){
	    case IOCTL_INTERNAL_USB_SUBMIT_URB:
		    break;
	    case IOCTL_INTERNAL_USB_RESET_PORT:
		    ioctl_status = STATUS_SUCCESS;
		    /* pass through */
	    default:
		    goto end;
    }

    urb =  irpstack->Parameters.Others.Argument1;
    if(NULL == urb)
	    goto end;
    switch(urb->Hdr.Function){
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		in=1;
		type=USB_ENDPOINT_TYPE_CONTROL;
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		in=urb->TransferFlags & USBD_TRANSFER_DIRECTION_IN;
		type=USB_ENDPOINT_TYPE_CONTROL;
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		in=pipe2direct(urb->PipeHandle);
		type=USB_ENDPOINT_TYPE_BULK;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		in=pipe2direct(urb->PipeHandle);
		type=USB_ENDPOINT_TYPE_ISOCHRONOUS;
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		in=0;
		type=USB_ENDPOINT_TYPE_CONTROL;
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		in=0;
		type=USB_ENDPOINT_TYPE_CONTROL;
		if(post_select_interface(pdodata, urb)!=STATUS_SUCCESS)
			goto end;
		break;
	default:
		KdPrint(("Warning, not supported func:%d\n",
					urb->Hdr.Function));
		goto end;
    }
    if(h->u.ret_submit.actual_length > urb->TransferBufferLength){
	KdPrint(("Warning, ret too big %d %d!\n",
				h->u.ret_submit.actual_length,
				urb->TransferBufferLength));
	goto end;
    }
    if(in)
	in_len= h->u.ret_submit.actual_length;
    if(type == USB_ENDPOINT_TYPE_ISOCHRONOUS){
	    if(h->u.ret_submit.number_of_packets !=
		urb->NumberOfPackets){
		    KdPrint(("Warning, number of packets not same:%d %d\n",
			h->u.ret_submit.number_of_packets,
			urb->NumberOfPackets));
		    goto end;
	    }
	    iso_len = urb->NumberOfPackets * sizeof(*ip_desc);
    }
    if(len!=sizeof(*h)+in_len+iso_len){
	KdPrint(("Warning, ret is not enough %d %d %d!\n",
				h->u.ret_submit.actual_length,
				urb->TransferBufferLength,
				urb->NumberOfPackets));
	goto end;
    }
    if(iso_len){
	    ip_desc = (struct usbip_iso_packet_descriptor *)
		    ((char *)(h+1) + in_len);
	    for(i=0; i<urb->NumberOfPackets; i++){
		    if(ip_desc->offset != urb->IsoPacket[i].Offset){
			    KdPrint(("Warning, why offset changed?%d %d %d %d\n",
				i,
				ip_desc->offset,
				ip_desc->actual_length,
				urb->IsoPacket[i].Offset));
			    goto end;
		    }
		    urb->IsoPacket[i].Length = ip_desc->actual_length;
		    urb->IsoPacket[i].Status = tran_usb_status(
				    ip_desc->status, in_len,
				    USB_ENDPOINT_TYPE_ISOCHRONOUS);
		    ip_desc++;
	    }
	    urb->ErrorCount = h->u.ret_submit.error_count;
    }
    if(in_len){
	buf=NULL;
	if(urb->TransferBuffer)
		buf=urb->TransferBuffer;
	else if (urb->TransferBufferMDL){
		buf=MmGetSystemAddressForMdlSafe(
		urb->TransferBufferMDL,
		LowPagePriority);
	} else
		KdPrint(("No transferbuffer fo in\n"));
	if(NULL==buf){
		ioctl_status = STATUS_INSUFFICIENT_RESOURCES;
		goto end;
	}
	if(in_len > urb->TransferBufferLength){
		KdPrint(("too big in, why"));
		ioctl_status = STATUS_INVALID_PARAMETER;
		goto end;
	}
	if(iso_len && in_len != urb->TransferBufferLength)
		copy_iso_data(buf, urb->TransferBufferLength, h+1, in_len, urb);
	else
		RtlCopyMemory(buf, h+1, in_len);
	if(NULL==pdodata->dev_config)
		try_save_config(pdodata, urb);
    }
    urb->Hdr.Status = tran_usb_status(h->u.ret_submit.status, in, type);
    KdPrint(("Sucess Finish URB FUNC:%d %s %s len:%d ret:%d\n", urb->Hdr.Function,
				func2name(urb->Hdr.Function),
				in?"in":"out", urb->TransferBufferLength,
				len));
    urb->TransferBufferLength = h->u.ret_submit.actual_length;
    ioctl_status=STATUS_SUCCESS;
end:
    ioctl_irp->IoStatus.Status = ioctl_status;
    IoCompleteRequest(ioctl_irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
Bus_Write (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  stackirp;
    NTSTATUS            status;
    PFDO_DEVICE_DATA    fdoData;
    PCOMMON_DEVICE_DATA     commonData;
    PPDO_DEVICE_DATA pdodata;

    PAGED_CODE ();

    KdPrint(("enter Write func\n"));
    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (!commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
	KdPrint(("Write return not fd\n"));
        return status;
    }

    fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    Bus_IncIoCount (fdoData);

    if (fdoData->DevicePnPState == Deleted){
        status = STATUS_NO_SUCH_DEVICE;
	goto END;
    }
    stackirp = IoGetCurrentIrpStackLocation(Irp);
    pdodata = stackirp->FileObject->FsContext;
    if(NULL==pdodata||pdodata->Present == FALSE){
	status = STATUS_INVALID_DEVICE_REQUEST;
	goto END;
    }
    Irp->IoStatus.Information = 0;
    status = process_write_irp(pdodata, Irp);
END:
    KdPrint(("Write return:%08lx\n", status));
    Irp->IoStatus.Status = status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);
    Bus_DecIoCount (fdoData);
    return status;
}

#define USBDEVFS_URB_SHORT_NOT_OK	0x01
#define USBDEVFS_URB_ISO_ASAP		0x02
#define USBDEVFS_URB_NO_FSBR		0x20
#define USBDEVFS_URB_ZERO_PACKET	0x40
#define USBDEVFS_URB_NO_INTERRUPT	0x80

unsigned int transflag(unsigned int flags)
{
	unsigned int linux_flags=0;
	if(!(flags&USBD_SHORT_TRANSFER_OK))
		linux_flags|=USBDEVFS_URB_SHORT_NOT_OK;
	if(flags&USBD_START_ISO_TRANSFER_ASAP)
		linux_flags|=USBDEVFS_URB_ISO_ASAP;
	return RtlUlongByteSwap(linux_flags);
}

void set_cmd_submit_usbip_header(struct usbip_header *h,
		unsigned long seqnum, unsigned int devid,
		unsigned int direct, USBD_PIPE_HANDLE pipe,
		unsigned int flags,  unsigned int len)
{
	h->base.command   = RtlUlongByteSwap(USBIP_CMD_SUBMIT);
	h->base.seqnum    = RtlUlongByteSwap(seqnum);
        h->base.devid     = RtlUlongByteSwap(devid);
	h->base.direction = RtlUlongByteSwap(direct?USBIP_DIR_IN:USBIP_DIR_OUT);
	h->base.ep        = RtlUlongByteSwap(pipe2addr(pipe));
	h->u.cmd_submit.transfer_flags = transflag(flags);
        h->u.cmd_submit.transfer_buffer_length = RtlUlongByteSwap(len);
        h->u.cmd_submit.start_frame = 0;
        h->u.cmd_submit.number_of_packets = 0;
        h->u.cmd_submit.interval = pipe2interval(pipe);
}

struct usb_ctrl_setup {
    unsigned char bRequestType;
    unsigned char  bRequest;
    unsigned short wValue;
    unsigned short wIndex;
    unsigned short wLength;
};

static void build_setup_packet(struct usb_ctrl_setup *setup,
		unsigned char direct_in,
		unsigned char type,
		unsigned char recip,
		unsigned char request)
{
	setup->bRequestType = type<<5;
	if(direct_in)
		setup->bRequestType|=USB_ENDPOINT_DIRECTION_MASK;
	setup->bRequestType|=recip;
	setup->bRequest = request;
}

#define CHECK_SIZE_RW \
if(NULL==buf) \
	return STATUS_BUFFER_TOO_SMALL; \
if(req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) { \
	if (len < sizeof(*h)) \
		return STATUS_BUFFER_TOO_SMALL; \
} else { \
	if (len < sizeof(*h) + req->TransferBufferLength) \
		return STATUS_BUFFER_TOO_SMALL; \
}

#define CHECK_SIZE_READ \
if (len < sizeof(*h) || NULL == buf) \
	return STATUS_BUFFER_TOO_SMALL; \


int prepare_reset_dev(char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		0, 0,
		0, 0);
	h->base.command = RtlUlongByteSwap(USBIP_RESET_DEV);
	*copied=sizeof(*h);
	return  STATUS_SUCCESS;
}

int prepare_select_config_urb(struct _URB_SELECT_CONFIGURATION  *req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usb_ctrl_setup * setup=h->u.cmd_submit.setup;
	int in=0;
	*copied = 0;

	CHECK_SIZE_READ

	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		0, 0,
		0, 0);
	build_setup_packet(setup,
	0,
	BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	setup->wLength = 0;
	setup->wValue = 1;
	setup->wIndex = 0;

	*copied=sizeof(*h);
	return  STATUS_SUCCESS;
}

int prepare_select_interface_urb(struct _URB_SELECT_INTERFACE  *req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usb_ctrl_setup * setup=h->u.cmd_submit.setup;
	int in=0;
	*copied = 0;

	CHECK_SIZE_READ

	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		0, 0,
		0, 0);
	build_setup_packet(setup,
	0,
	BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	setup->wLength = 0;
	setup->wValue = req->Interface.AlternateSetting;
	setup->wIndex = req->Interface.InterfaceNumber;

	*copied=sizeof(*h);
	return  STATUS_SUCCESS;
}

int prepare_class_vendor_urb(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST  *req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usb_ctrl_setup * setup=h->u.cmd_submit.setup;
	int in=req->TransferFlags & USBD_TRANSFER_DIRECTION_IN, type, recip;
	*copied = 0;

	KdPrint(("flag:%d pbuf:%p len:%d RequestTypeReservedBits:%02x"
		"Request:%02x Value:%02x Index:%02x\r\n",
		req->TransferFlags, req->TransferBuffer,
		req->TransferBufferLength,
		req->RequestTypeReservedBits, req->Request,
		req->Value, req->Index));

	switch(req->Hdr.Function){
		case URB_FUNCTION_CLASS_DEVICE:
			type=BMREQUEST_CLASS;
			recip=BMREQUEST_TO_DEVICE;
			break;
		case URB_FUNCTION_CLASS_INTERFACE:
			type=BMREQUEST_CLASS;
			recip=BMREQUEST_TO_INTERFACE;
			break;
		case URB_FUNCTION_CLASS_ENDPOINT:
			type=BMREQUEST_CLASS;
			recip=BMREQUEST_TO_ENDPOINT;
			break;
		case URB_FUNCTION_CLASS_OTHER:
			type=BMREQUEST_CLASS;
			recip=BMREQUEST_TO_OTHER;
			break;
		case URB_FUNCTION_VENDOR_DEVICE:
			type=BMREQUEST_VENDOR;
			recip=BMREQUEST_TO_DEVICE;
			break;
		case URB_FUNCTION_VENDOR_INTERFACE:
			type=BMREQUEST_VENDOR;
			recip=BMREQUEST_TO_INTERFACE;
			break;
		case URB_FUNCTION_VENDOR_ENDPOINT:
			type=BMREQUEST_VENDOR;
			recip=BMREQUEST_TO_ENDPOINT;
			break;
		case URB_FUNCTION_VENDOR_OTHER:
			type=BMREQUEST_VENDOR;
			recip=BMREQUEST_TO_OTHER;
			break;
	}

	CHECK_SIZE_RW

	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		in, 0,
		req->TransferFlags|USBD_SHORT_TRANSFER_OK, req->TransferBufferLength);
	build_setup_packet(setup,
	in,
	type, recip, req->Request);
//FIXME what is the usage of RequestTypeReservedBits?
	setup->wLength = req->TransferBufferLength;
	setup->wValue = req->Value;
	setup->wIndex = req->Index;

	*copied=sizeof(*h);
	if(!in){
		RtlCopyMemory(h+1, req->TransferBuffer,
				req->TransferBufferLength);
		(*copied)+=req->TransferBufferLength;
	}
	return  STATUS_SUCCESS;
}

int prepare_get_intf_descriptor_urb(struct _URB_CONTROL_DESCRIPTOR_REQUEST * req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usb_ctrl_setup * setup=h->u.cmd_submit.setup;
	*copied = 0;

	CHECK_SIZE_READ

	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		USBIP_DIR_IN, 0,
		USBD_SHORT_TRANSFER_OK, req->TransferBufferLength);
	build_setup_packet(setup,
	USBIP_DIR_IN,
	BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_DESCRIPTOR);

	setup->wLength = req->TransferBufferLength;
	setup->wValue = (req->DescriptorType<<8)|req->Index;

	KdPrint(("pbuf:%p len:%d Index:%02x"
	"DescriptorType:%02x LanguageId:%02x\r\n",
	req->TransferBuffer,
	req->TransferBufferLength,
	req->Index,
	req->DescriptorType,
	req->LanguageId));

	*copied=sizeof(*h);
	return STATUS_SUCCESS;
}

int prepare_get_dev_descriptor_urb( struct _URB_CONTROL_DESCRIPTOR_REQUEST * req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usb_ctrl_setup * setup=h->u.cmd_submit.setup;
	*copied = 0;

	CHECK_SIZE_READ

	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		USBIP_DIR_IN, 0,
		USBD_SHORT_TRANSFER_OK, req->TransferBufferLength);
	build_setup_packet(setup,
	USBIP_DIR_IN,
	BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);

	setup->wLength = req->TransferBufferLength;
	setup->wValue = (req->DescriptorType<<8) |req->Index<<8;

	switch(req->DescriptorType){
		case USB_DEVICE_DESCRIPTOR_TYPE:
		case USB_CONFIGURATION_DESCRIPTOR_TYPE:
			setup->wIndex = 0;
			break;
		case USB_STRING_DESCRIPTOR_TYPE:
			setup->wIndex = req->LanguageId;
			break;
		default:
			return STATUS_INVALID_PARAMETER;
	}
	*copied=sizeof(*h);
	return STATUS_SUCCESS;
}

int prepare_iso_urb(struct _URB_ISOCH_TRANSFER * req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	struct usbip_iso_packet_descriptor * ip_desc;
	int in = pipe2direct(req->PipeHandle);
	int type = pipe2type(req->PipeHandle);
	int i, offset, last_len;

	*copied = 0;

	KdPrint(("PipeHandle %08x\n", (unsigned long)req->PipeHandle));

	if(type!=USB_ENDPOINT_TYPE_ISOCHRONOUS){
		KdPrint(("Error, not a iso pipe\n"));
		return STATUS_INVALID_PARAMETER;
	}
	if(NULL==buf)
		return STATUS_BUFFER_TOO_SMALL; \
	if(req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) {
		if (len < sizeof(*h) + req->NumberOfPackets *
			sizeof(*ip_desc))
			return STATUS_BUFFER_TOO_SMALL;
	} else {
		if (len < sizeof(*h) + req->TransferBufferLength +
			req->NumberOfPackets *
			sizeof(*ip_desc))
			return STATUS_BUFFER_TOO_SMALL;
	}
	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		in, req->PipeHandle,
		req->TransferFlags|USBD_SHORT_TRANSFER_OK,
		req->TransferBufferLength);
        h->u.cmd_submit.start_frame = RtlUlongByteSwap(req->StartFrame);
        h->u.cmd_submit.number_of_packets = RtlUlongByteSwap(req->NumberOfPackets);
	*copied=sizeof(*h);
	if(!in){
		buf=NULL;
		if(req->TransferBuffer)
			buf=req->TransferBuffer;
		else if (req->TransferBufferMDL){
			buf=MmGetSystemAddressForMdlSafe(
			req->TransferBufferMDL,
			LowPagePriority);
		} else
			KdPrint(("No transferbuffer for out\n"));
		if(NULL==buf)
			return STATUS_INSUFFICIENT_RESOURCES;
		RtlCopyMemory(h+1, buf, req->TransferBufferLength);
		(*copied)+=req->TransferBufferLength;
	}
	ip_desc = (struct usbip_iso_packet_descriptor *)(buf + (*copied));
	if(in){
		offset=0;
		for(i=0;i<req->NumberOfPackets;i++){
			if(req->IsoPacket[i].Offset<offset){
				KdPrint(("Warning strange iso packet offset:%d %d",
				offset,	req->IsoPacket[i].Offset));

				return STATUS_INVALID_PARAMETER;
			}
			KdPrint(("iso packet offset:%d len:%d\n",
			req->IsoPacket[i].Offset,
			req->IsoPacket[i].Length));
			ip_desc->offset = RtlUlongByteSwap(
					req->IsoPacket[i].Offset);
			if(i>0)
				(ip_desc-1)->length = RtlUlongByteSwap(
					req->IsoPacket[i].Offset -
					offset);
			offset = req->IsoPacket[i].Offset;
			ip_desc->actual_length = 0;
			ip_desc->status = 0;
			ip_desc++;
		}
		(ip_desc-1)->length = RtlUlongByteSwap(
				req->TransferBufferLength - offset);
	} else {
		offset = 0;
		for(i=0;i<req->NumberOfPackets;i++){
			if(req->IsoPacket[i].Offset!=offset){
				KdPrint(("Warning strange iso packet offset:%d %d",
				offset,	req->IsoPacket[i].Offset));

				return STATUS_INVALID_PARAMETER;
			}
			KdPrint(("iso packet offset:%d len:%d\n",
			req->IsoPacket[i].Offset,
			req->IsoPacket[i].Length));
			ip_desc->offset = RtlUlongByteSwap(offset);
			ip_desc->length = RtlUlongByteSwap(
					req->IsoPacket[i].Length);
			offset += req->IsoPacket[i].Length;
			ip_desc->actual_length = 0;
			ip_desc->status = 0;
			ip_desc++;
		}
	}
	(*copied)+=req->NumberOfPackets * sizeof(*ip_desc);
	return STATUS_SUCCESS;
}

int prepare_bulk_urb(struct _URB_BULK_OR_INTERRUPT_TRANSFER * req,
		char *buf, int len,  int *copied, unsigned long seqnum,
		unsigned int devid)
{
	struct usbip_header * h = (struct usbip_header * ) buf;
	int in = pipe2direct(req->PipeHandle);
	int type = pipe2type(req->PipeHandle);

	*copied = 0;

	CHECK_SIZE_RW


	KdPrint(("PipeHandle %08x\n", (unsigned long)req->PipeHandle));
	if(type!=USB_ENDPOINT_TYPE_BULK&&type!=USB_ENDPOINT_TYPE_INTERRUPT){
		KdPrint(("Error, not a bulk pipe\n"));
		return STATUS_INVALID_PARAMETER;
	}
	set_cmd_submit_usbip_header (h,
		seqnum, devid,
		in, req->PipeHandle,
		req->TransferFlags, req->TransferBufferLength);

	*copied=sizeof(*h);
	if(!in){
		buf=NULL;
		if(req->TransferBuffer)
			buf=req->TransferBuffer;
		else if (req->TransferBufferMDL){
			buf=MmGetSystemAddressForMdlSafe(
			req->TransferBufferMDL,
			LowPagePriority);
		} else
			KdPrint(("No transferbuffer for out\n"));
		if(NULL==buf)
			return STATUS_INSUFFICIENT_RESOURCES;
		RtlCopyMemory(h+1, buf, req->TransferBufferLength);
		(*copied)+=req->TransferBufferLength;
	}
	return STATUS_SUCCESS;
}

int set_read_irp_data(PIRP read_irp, PIRP ioctl_irp, unsigned long seq_num,
		unsigned int devid)
{
    PIO_STACK_LOCATION iostack_irp;
    char *buf;
    int len;
    PURB urb;
    iostack_irp = IoGetCurrentIrpStackLocation(read_irp);

    buf = read_irp->AssociatedIrp.SystemBuffer;
    len = iostack_irp->Parameters.Read.Length;

    iostack_irp = IoGetCurrentIrpStackLocation(ioctl_irp);

    switch(iostack_irp->Parameters.DeviceIoControl.IoControlCode){
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		return prepare_reset_dev(buf, len, &read_irp->IoStatus.Information,			seq_num,devid);
	default:
		read_irp->IoStatus.Information = 0;
		return STATUS_INVALID_PARAMETER;
    }
    urb =  iostack_irp->Parameters.Others.Argument1;
    if(NULL == urb){
	    read_irp->IoStatus.Information = 0;
	    return  STATUS_INVALID_DEVICE_REQUEST;
    }
    switch (urb->UrbHeader.Function){
		case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
			return prepare_bulk_urb((struct _URB_BULK_OR_INTERRUPT_TRANSFER *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_ISOCH_TRANSFER:
			return prepare_iso_urb((struct _URB_ISOCH_TRANSFER *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
			return prepare_get_dev_descriptor_urb((struct _URB_CONTROL_DESCRIPTOR_REQUEST *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
			return prepare_get_intf_descriptor_urb((struct _URB_CONTROL_DESCRIPTOR_REQUEST *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_CLASS_DEVICE:
		case URB_FUNCTION_CLASS_INTERFACE:
		case URB_FUNCTION_CLASS_ENDPOINT:
		case URB_FUNCTION_CLASS_OTHER:
		case URB_FUNCTION_VENDOR_DEVICE:
		case URB_FUNCTION_VENDOR_INTERFACE:
		case URB_FUNCTION_VENDOR_ENDPOINT:
			return prepare_class_vendor_urb((struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_SELECT_CONFIGURATION:
			return prepare_select_config_urb((struct _URB_SELECT_CONFIGURATION *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		case URB_FUNCTION_SELECT_INTERFACE:
			return prepare_select_interface_urb((struct _URB_SELECT_INTERFACE *)urb, buf, len,
			&read_irp->IoStatus.Information, seq_num, devid);
		default:
			break;
     }
     read_irp->IoStatus.Information = 0;
     KdPrint(("Warning function:%x %d\n", urb->UrbHeader.Function,
				urb->UrbHeader.Length));
     return STATUS_INVALID_PARAMETER;
}

void add_wait_q(PPDO_DEVICE_DATA pdodata, PIRP Irp);

int process_read_irp(PPDO_DEVICE_DATA pdodata, PIRP read_irp)
{
    NTSTATUS status = STATUS_PENDING;
    KIRQL oldirql;
    PIRP ioctl_irp;
    PLIST_ENTRY le = NULL;
    unsigned long seq_num;
    KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
    if (!IsListEmpty(&pdodata->ioctl_q)){
	seq_num=++(pdodata->seq_num);
	le = RemoveHeadList(&pdodata->ioctl_q);
    } else {
	    if(pdodata->pending_read_irp)
		    status = STATUS_INVALID_DEVICE_REQUEST;
	    else{
		IoMarkIrpPending(read_irp);
		pdodata->pending_read_irp = read_irp;
	    }
    }
    KeReleaseSpinLock(&pdodata->q_lock, oldirql);
    if(le){
	ioctl_irp = CONTAINING_RECORD(le, IRP, Tail.Overlay.ListEntry);
	KdPrint(("get a ioctl_irp %p\n", ioctl_irp));
	ioctl_irp->Tail.Overlay.DriverContext[0]=(PVOID)seq_num;
	status = set_read_irp_data(read_irp, ioctl_irp, seq_num, pdodata->devid);
       if(status == STATUS_SUCCESS)
		add_wait_q(pdodata, ioctl_irp);
       else{
	       ioctl_irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
               IoCompleteRequest (ioctl_irp, IO_NO_INCREMENT);
       }
    }
    return status;
}

NTSTATUS
Bus_Read (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status;
    PFDO_DEVICE_DATA    fdoData;
    PPDO_DEVICE_DATA	pdodata;
    PCOMMON_DEVICE_DATA     commonData;
    PIO_STACK_LOCATION stackirp;

    PAGED_CODE ();

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (!commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    Bus_IncIoCount (fdoData);

    //
    // Check to see whether the bus is removed
    //

    if (fdoData->DevicePnPState == Deleted){
        status = STATUS_NO_SUCH_DEVICE;
	goto END;
    }
    stackirp = IoGetCurrentIrpStackLocation(Irp);
    pdodata = stackirp->FileObject->FsContext;
    if(NULL==pdodata||pdodata->Present == FALSE){
	status = STATUS_INVALID_DEVICE_REQUEST;
	goto END;
    }
    if(pdodata->pending_read_irp){
	status = STATUS_INVALID_PARAMETER;
	goto END;
    }
    status = process_read_irp(pdodata, Irp);
END:
    if(status != STATUS_PENDING){
	Irp->IoStatus.Status = status;
	IoCompleteRequest (Irp, IO_NO_INCREMENT);
    }
    Bus_DecIoCount (fdoData);
    return status;
}

NTSTATUS
Bus_Create (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status;
    PFDO_DEVICE_DATA    fdoData;
    PCOMMON_DEVICE_DATA     commonData;
    PIO_STACK_LOCATION iostackirp = NULL;

    PAGED_CODE ();

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (!commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    Bus_IncIoCount (fdoData);

    //
    // Check to see whether the bus is removed
    //

    if (fdoData->DevicePnPState == Deleted) {
        Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }
    status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);
    Bus_DecIoCount (fdoData);
    return status;
}

NTSTATUS
Bus_Cleanup (
    __in  PDEVICE_OBJECT  dev,
    __in  PIRP            irp
    )
{
    PIO_STACK_LOCATION  irpstack;
    NTSTATUS            status;
    PFDO_DEVICE_DATA    fdodata;
    PPDO_DEVICE_DATA	pdodata;
    PCOMMON_DEVICE_DATA     commondata;

    PAGED_CODE ();

    commondata = (PCOMMON_DEVICE_DATA) dev->DeviceExtension;
    //
    // We only allow create/close requests for the FDO.
    // That is the bus itself.
    //

    if (!commondata->IsFDO) {
        irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (irp, IO_NO_INCREMENT);
        return status;
    }

    fdodata = (PFDO_DEVICE_DATA) dev->DeviceExtension;

    Bus_IncIoCount (fdodata);

    //
    // Check to see whether the bus is removed
    //

    if (fdodata->DevicePnPState == Deleted) {
        irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
        IoCompleteRequest (irp, IO_NO_INCREMENT);
        return status;
    }
    irpstack = IoGetCurrentIrpStackLocation(irp);
    pdodata = irpstack->FileObject->FsContext;
    if(pdodata){
	    pdodata->fo=NULL;
	    irpstack->FileObject->FsContext=NULL;
	    bus_unplug_dev(pdodata->SerialNo, fdodata);
    }
    status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    IoCompleteRequest (irp, IO_NO_INCREMENT);
    Bus_DecIoCount (fdodata);
    return status;
}

NTSTATUS
Bus_Close (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status;
    PFDO_DEVICE_DATA    fdoData;
    PCOMMON_DEVICE_DATA     commonData;

    PAGED_CODE ();

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;
    //
    // We only allow create/close requests for the FDO.
    // That is the bus itself.
    //

    if (!commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    Bus_IncIoCount (fdoData);

    //
    // Check to see whether the bus is removed
    //

    if (fdoData->DevicePnPState == Deleted) {
        Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }
    status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);
    Bus_DecIoCount (fdoData);
    return status;
}

static void * seek_to_next_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int * offset,
		unsigned char type)
{
	unsigned int o=*offset;
	PUSB_COMMON_DESCRIPTOR desc;
	if(o>=config->wTotalLength)
		return NULL;
	do {
		if(o + sizeof(*desc) > config->wTotalLength)
			return NULL;
		desc=(char *)config + o;
		if(desc->bLength + o > config->wTotalLength)
			return NULL;
		o+=desc->bLength;
		if(desc->bDescriptorType == type){
			*offset = o;
			return desc;
		}
	}while(1);
}

static void * seek_to_one_intf_desc(PUSB_CONFIGURATION_DESCRIPTOR config, unsigned int * offset, unsigned int num, unsigned int alternatesetting)
{
	PUSB_INTERFACE_DESCRIPTOR intf_desc;

	do {
		intf_desc = seek_to_next_desc(config, offset,
			USB_INTERFACE_DESCRIPTOR_TYPE);
		if(NULL==intf_desc)
			break;
		if(intf_desc->bInterfaceNumber<num)
			continue;
		if(intf_desc->bInterfaceNumber>num)
			break;
		if(intf_desc->bAlternateSetting<alternatesetting)
			continue;
		if(intf_desc->bAlternateSetting>alternatesetting)
			break;
		return intf_desc;
	}while(1);
	return NULL;
}

void show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe)
{
	KdPrint(("pipe num %d:\n"
	    "MaximumPacketSize: %d\n"
	    "EndpointAddress: 0x%02x\n"
	    "Interval: %d\n"
	    "PipeType: %d\n"
	    "PiPeHandle: 0x%08x\n"
	    "MaximumTransferSize %d\n"
	    "PipeFlags 0x%08x\n", num,
	    pipe->MaximumPacketSize,
	    pipe->EndpointAddress,
	    pipe->Interval,
	    pipe->PipeType,
	    pipe->PipeHandle,
	    pipe->MaximumTransferSize,
	    pipe->PipeFlags));
}

void set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc)
{
	pipe->MaximumPacketSize = ep_desc->wMaxPacketSize;
	pipe->EndpointAddress = ep_desc->bEndpointAddress;
	pipe->Interval = ep_desc->bInterval;
	pipe->PipeType = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
	pipe->PipeHandle = make_pipe(ep_desc->bEndpointAddress,
				pipe->PipeType,
				ep_desc->bInterval);
}

int post_select_interface(PPDO_DEVICE_DATA pdodata,
		struct _URB_SELECT_INTERFACE * req)
{
	unsigned int i;
	unsigned int offset=0;
	USBD_INTERFACE_INFORMATION *intf= &req->Interface;
	PUSB_INTERFACE_DESCRIPTOR intf_desc;
	PUSB_ENDPOINT_DESCRIPTOR ep_desc;

	if(NULL==pdodata->dev_config){
		KdPrint(("Warning, select interface when have no get config\n"));
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if(intf->Length < sizeof(*intf) - sizeof(intf->Pipes[0])){
		KdPrint(("Warning, intf is too small\n"));
		return STATUS_INVALID_PARAMETER;
	}
	KdPrint(("config handle:%08x\n", req->ConfigurationHandle));
	KdPrint(("interface: len:%d int num:%d"
	         "AlternateSetting:%d"
		  "class:%d subclass:%d"
		"protocol:%d handle:%08x numerofpipes:%d",
		intf->Length,
		intf->InterfaceNumber,
		intf->AlternateSetting,
		intf->Class,
		intf->SubClass,
		intf->Protocol,
		intf->InterfaceHandle,
		intf->NumberOfPipes));

	i=(intf->Length +sizeof(intf->Pipes[0]) - sizeof(*intf))/
			sizeof(intf->Pipes[0]);
	if(i<intf->NumberOfPipes){
		KdPrint(("Warning, why space is so small?"));
		return STATUS_INVALID_PARAMETER;
	}
	/* FIXME  do we need set the other info in intf ? */
	intf->NumberOfPipes = i;

	intf_desc = seek_to_one_intf_desc(
			pdodata->dev_config,
			&offset, intf->InterfaceNumber,
			intf->AlternateSetting);
	/* FIXME if alternatesetting, we sound send out a ctrl urb ? */
	if(NULL==intf_desc){
		KdPrint(("Warning, can't select this interface\n"));
		return STATUS_INVALID_PARAMETER;
	}
	if(intf->NumberOfPipes != intf_desc->bNumEndpoints){
		KdPrint(("Warning, endpoints num no same: can hold:%d have %d\n",
				intf->NumberOfPipes,
				intf_desc->bNumEndpoints));
		return STATUS_INVALID_PARAMETER;
	}
	for(i=0; i<intf->NumberOfPipes;i++){
		show_pipe(i, &intf->Pipes[i]);
		if(intf->Pipes[i].MaximumTransferSize > 65536)
			return STATUS_INVALID_PARAMETER;
		ep_desc = seek_to_next_desc(
			pdodata->dev_config,
			&offset, USB_ENDPOINT_DESCRIPTOR_TYPE);
		if(NULL==ep_desc){
			KdPrint(("Warning, no ep desc\n"));
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		set_pipe(&intf->Pipes[i], ep_desc);
		show_pipe(i, &intf->Pipes[i]);
	}
	return STATUS_SUCCESS;
}

int proc_select_config(PPDO_DEVICE_DATA pdodata,
		struct _URB_SELECT_CONFIGURATION * req)
{
	unsigned int i, j;
	unsigned int offset=0;
	USBD_INTERFACE_INFORMATION *intf;
	PUSB_INTERFACE_DESCRIPTOR intf_desc;
	PUSB_ENDPOINT_DESCRIPTOR ep_desc;

	if(NULL==pdodata->dev_config){
		KdPrint(("Warning, select config when have no get config\n"));
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if(!RtlEqualMemory(pdodata->dev_config,
				req->ConfigurationDescriptor,
				sizeof(*req->ConfigurationDescriptor))){
		KdPrint(("Warning, not the same config desc\n"));
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	/* it has no means */
	req->ConfigurationHandle=(USBD_CONFIGURATION_HANDLE) 0x12345678;
	intf = &req->Interface;
	for(i=0; i<req->ConfigurationDescriptor->bNumInterfaces; i++){
		if((char *)intf + sizeof(*intf) - sizeof(intf->Pipes[0])
				- (char *)req
				>req->Hdr.Length){
			KdPrint(("Warning, too small urb for select config\n"));
			return STATUS_INVALID_PARAMETER;
		}
		if(intf->NumberOfPipes>0){
			if((char *)intf + sizeof(*intf) +
				(intf->NumberOfPipes-1)*sizeof(intf->Pipes[0])
				- (char *)req
				> req->Hdr.Length){
				KdPrint(("Warning, too small urb for select config\n"));
				return STATUS_INVALID_PARAMETER;
			}
		}
		intf_desc = seek_to_next_desc(
				pdodata->dev_config,
				&offset, USB_INTERFACE_DESCRIPTOR_TYPE);
		if(NULL==intf_desc){
			KdPrint(("Warning, no interface desc\n"));
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		if(intf_desc->bNumEndpoints!=
				intf->NumberOfPipes){
			KdPrint(("Warning, number of pipes is no same%d %d\n",
					intf_desc->bNumEndpoints,
					intf->NumberOfPipes));
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		intf->Class=intf_desc->bInterfaceClass;
		intf->SubClass=intf_desc->bInterfaceSubClass;
		intf->Protocol=intf_desc->bInterfaceProtocol;
		/* it has no means */
		intf->InterfaceHandle = (USBD_INTERFACE_HANDLE) 0x12345678;
		KdPrint(("the %d interface\n", i));
		for(j=0; j<intf->NumberOfPipes;j++){
			show_pipe(j, &intf->Pipes[j]);

			ep_desc = seek_to_next_desc(
				pdodata->dev_config,
				&offset, USB_ENDPOINT_DESCRIPTOR_TYPE);

			if(NULL==ep_desc){
				KdPrint(("Warning, no ep desc\n"));
				return STATUS_INVALID_DEVICE_REQUEST;
			}

			set_pipe(&intf->Pipes[j], ep_desc);
			show_pipe(j, &intf->Pipes[j]);
		}
		intf=(char *)intf  + sizeof(*intf) + (intf->NumberOfPipes - 1)*
			sizeof(intf->Pipes[0]);
	}
	/* it seems we must return now */
	return STATUS_SUCCESS;
}

int proc_urb(PPDO_DEVICE_DATA pdodata, void *arg)
{
	PURB urb=(PURB) arg;
	if(urb==NULL){
		KdPrint(("null arg"));
		return STATUS_INVALID_PARAMETER;
	}
	KdPrint(("URB FUNC:%d %s\n", urb->UrbHeader.Function,
				func2name(urb->UrbHeader.Function)));
	switch (urb->UrbHeader.Function){
		case URB_FUNCTION_SELECT_CONFIGURATION:
			KdPrint(("select configuration\n"));
			return proc_select_config(pdodata, arg);
		case URB_FUNCTION_CLASS_DEVICE:
		case URB_FUNCTION_CLASS_INTERFACE:
		case URB_FUNCTION_CLASS_ENDPOINT:
		case URB_FUNCTION_CLASS_OTHER:
		case URB_FUNCTION_VENDOR_DEVICE:
		case URB_FUNCTION_VENDOR_INTERFACE:
		case URB_FUNCTION_VENDOR_ENDPOINT:
		case URB_FUNCTION_VENDOR_OTHER:
		case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		case URB_FUNCTION_ISOCH_TRANSFER:
		case URB_FUNCTION_SELECT_INTERFACE:
			return STATUS_PENDING;
		default:
			break;
	}
	KdPrint(("Warning function:%x %d\n", urb->UrbHeader.Function,
				urb->UrbHeader.Length));
	return STATUS_INVALID_PARAMETER;
}

void add_wait_q(PPDO_DEVICE_DATA pdodata, PIRP Irp)
{
    KIRQL oldirql;

    KeAcquireSpinLock(&pdodata->wait_q_lock, &oldirql);
	InsertTailList(&pdodata->wait_q, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&pdodata->wait_q_lock, oldirql);

    return;
}



int try_addq(PPDO_DEVICE_DATA pdodata, PIRP Irp)
{
    KIRQL oldirql;
    PIRP read_irp;
    NTSTATUS status=STATUS_PENDING;
    unsigned long seq_num;

    KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
    read_irp=pdodata->pending_read_irp;
    pdodata->pending_read_irp=NULL;
    if(NULL==read_irp){
           IoMarkIrpPending(Irp);
	   InsertTailList(&pdodata->ioctl_q, &Irp->Tail.Overlay.ListEntry);\
    } else
	    seq_num = ++(pdodata->seq_num);
    KeReleaseSpinLock(&pdodata->q_lock, oldirql);
    if(NULL==read_irp)
	    return status;

    read_irp->IoStatus.Status = set_read_irp_data(read_irp, Irp, seq_num,
		    pdodata->devid);
    if(read_irp->IoStatus.Status == STATUS_SUCCESS){
	Irp->Tail.Overlay.DriverContext[0]=(PVOID)seq_num;
	add_wait_q(pdodata, Irp);
    }
    else
	status = STATUS_INVALID_PARAMETER;
    IoCompleteRequest(read_irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS
Bus_Internal_IoCtl (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
{
    PIO_STACK_LOCATION      irpStack;
    NTSTATUS                status;
    ULONG                   inlen;
    PPDO_DEVICE_DATA        pdoData;
    PVOID                   buffer;
    PCOMMON_DEVICE_DATA     commonData;

    PAGED_CODE ();

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;
    KdPrint(("Enter internal control %d\r\n", commonData->IsFDO));

    irpStack = IoGetCurrentIrpStackLocation (Irp);
    KdPrint(("internal control:%d %s\r\n", irpStack->Parameters.DeviceIoControl.IoControlCode,code2name(irpStack->Parameters.DeviceIoControl.IoControlCode)));

    if (commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    pdoData = (PPDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (pdoData->Present==FALSE) {
        Irp->IoStatus.Status = status = STATUS_DEVICE_OFF_LINE;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    buffer = Irp->AssociatedIrp.SystemBuffer;
    inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;

    status = STATUS_INVALID_PARAMETER;

    switch(irpStack->Parameters.DeviceIoControl.IoControlCode){
        case IOCTL_INTERNAL_USB_SUBMIT_URB:
		    status=proc_urb(pdoData,
				    irpStack->Parameters.Others.Argument1);
		    break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		    status=STATUS_SUCCESS;
		    *(unsigned long *)irpStack->Parameters.Others.Argument1=
			    USBD_PORT_ENABLED|USBD_PORT_CONNECTED;
		    break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		    status=STATUS_PENDING;
		    break;
	default:
		    KdPrint(("Unknown Ioctrl code\n"));
		    break;
    }
    Irp->IoStatus.Information = 0;

    if(status == STATUS_PENDING)
	 status = try_addq(pdoData,Irp);
    if(status!=STATUS_PENDING) {
	Irp->IoStatus.Status = status;
	IoCompleteRequest (Irp, IO_NO_INCREMENT);
    }
    return status;
}



NTSTATUS
Bus_IoCtl (
    __in  PDEVICE_OBJECT  DeviceObject,
    __in  PIRP            Irp
    )
/*++
Routine Description:

    Handle user mode PlugIn, UnPlug and device Eject requests.

Arguments:

   DeviceObject - pointer to a device object.

   Irp - pointer to an I/O Request Packet.

Return Value:

   NT status code

--*/
{
    PIO_STACK_LOCATION      irpStack;
    NTSTATUS                status;
    ULONG                   inlen;
    ULONG                   outlen;
    ULONG		    info = 0;
    PFDO_DEVICE_DATA        fdoData;
    PVOID                   buffer;
    PCOMMON_DEVICE_DATA     commonData;

    PAGED_CODE ();

    KdPrint(("Enter control\r\n"));

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;
    //
    // We only allow create/close requests for the FDO.
    // That is the bus itself.
    //
    if (!commonData->IsFDO) {
        Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

    Bus_IncIoCount (fdoData);

    //
    // Check to see whether the bus is removed
    //

    if (fdoData->DevicePnPState == Deleted) {
        status = STATUS_NO_SUCH_DEVICE;
        goto END;
    }

    irpStack = IoGetCurrentIrpStackLocation (Irp);

    buffer = Irp->AssociatedIrp.SystemBuffer;
    inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    status = STATUS_INVALID_PARAMETER;

    switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_USBVBUS_PLUGIN_HARDWARE:
        if (sizeof(ioctl_usbvbus_plugin) == inlen ) {
            Bus_KdPrint(fdoData, BUS_DBG_IOCTL_TRACE, ("PlugIn called\n"));

            status= bus_plugin_dev((ioctl_usbvbus_plugin *)buffer, fdoData,
			    irpStack->FileObject);
        }
        break;

   case IOCTL_USBVBUS_GET_PORTS_STATUS:
        if (sizeof(ioctl_usbvbus_get_ports_status) == outlen ) {
            Bus_KdPrint(fdoData, BUS_DBG_IOCTL_TRACE, ("get ports status called\n"));

            status= bus_get_ports_status((ioctl_usbvbus_get_ports_status *)buffer, fdoData, &info);
        }
        break;

    case IOCTL_USBVBUS_UNPLUG_HARDWARE:

        if (sizeof (ioctl_usbvbus_unplug) == inlen){

            Bus_KdPrint(fdoData, BUS_DBG_IOCTL_TRACE, ("UnPlug called\n"));

            status= bus_unplug_dev(((ioctl_usbvbus_unplug *)buffer)->addr, fdoData);

        }
        break;

    case IOCTL_USBVBUS_EJECT_HARDWARE:

        if ((sizeof (BUSENUM_EJECT_HARDWARE) == inlen) &&
            (((PBUSENUM_EJECT_HARDWARE)buffer)->Size == inlen)) {

            Bus_KdPrint(fdoData, BUS_DBG_IOCTL_TRACE, ("Eject called\n"));

            status= Bus_EjectDevice((PBUSENUM_EJECT_HARDWARE)buffer, fdoData);

        }
        break;

    default:
        break; // default status is STATUS_INVALID_PARAMETER
    }

    Irp->IoStatus.Information = info;
END:
    Irp->IoStatus.Status = status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);
    Bus_DecIoCount (fdoData);
    return status;
}


VOID
Bus_DriverUnload (
    __in PDRIVER_OBJECT DriverObject
    )
/*++
Routine Description:
    Clean up everything we did in driver entry.

Arguments:

   DriverObject - pointer to this driverObject.


Return Value:

--*/
{
    PAGED_CODE ();

    Bus_KdPrint_Def (BUS_DBG_SS_TRACE, ("Unload\n"));

    //
    // All the device objects should be gone.
    //

    ASSERT (NULL == DriverObject->DeviceObject);

    //
    // Here we free all the resources allocated in the DriverEntry
    //

    if (Globals.RegistryPath.Buffer)
        ExFreePool(Globals.RegistryPath.Buffer);

    return;
}

VOID
Bus_IncIoCount (
    __in  PFDO_DEVICE_DATA   FdoData
    )

/*++

Routine Description:

    This routine increments the number of requests the device receives


Arguments:

    FdoData - pointer to the FDO device extension.

Return Value:

    VOID

--*/

{

    LONG            result;


    result = InterlockedIncrement(&FdoData->OutstandingIO);

    ASSERT(result > 0);
    //
    // Need to clear StopEvent (when OutstandingIO bumps from 1 to 2)
    //
    if (result == 2) {
        //
        // We need to clear the event
        //
        KeClearEvent(&FdoData->StopEvent);
    }

    return;
}

VOID
Bus_DecIoCount(
    __in  PFDO_DEVICE_DATA  FdoData
    )

/*++

Routine Description:

    This routine decrements as it complete the request it receives

Arguments:

    FdoData - pointer to the FDO device extension.

Return Value:

    VOID

--*/
{

    LONG            result;

    result = InterlockedDecrement(&FdoData->OutstandingIO);

    ASSERT(result >= 0);

    if (result == 1) {
        //
        // Set the stop event. Note that when this happens
        // (i.e. a transition from 2 to 1), the type of requests we
        // want to be processed are already held instead of being
        // passed away, so that we can't "miss" a request that
        // will appear between the decrement and the moment when
        // the value is actually used.
        //

        KeSetEvent (&FdoData->StopEvent, IO_NO_INCREMENT, FALSE);

    }

    if (result == 0) {

        //
        // The count is 1-biased, so it can be zero only if an
        // extra decrement is done when a remove Irp is received
        //

        ASSERT(FdoData->DevicePnPState == Deleted);

        //
        // Set the remove event, so the device object can be deleted
        //

        KeSetEvent (&FdoData->RemoveEvent, IO_NO_INCREMENT, FALSE);

    }

    return;
}

