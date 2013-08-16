/*
 * Linux driver for Mytek Digital Stereo192-DSD DAC USB2
 *
 * Device communications
 *
 * Based on 6fire usb driver
 *
 * Adapted for Mytek by	: Jurgen Kramer
 * Last updated		: August 16, 2013
 * Copyright		: (C) Jurgen Kramer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "comm.h"
#include "chip.h"

enum {
	COMM_EP = 1,
	COMM_FPGA_EP = 2
};

static void mytek_comm_init_urb(struct comm_runtime *rt, struct urb *urb,
		u8 *buffer, void *context, void(*handler)(struct urb *urb))
{
	usb_init_urb(urb);
	urb->transfer_buffer = buffer;
	urb->pipe = usb_sndintpipe(rt->chip->dev, COMM_EP);
	urb->complete = handler;
	urb->context = context;
	urb->interval = 1;
	urb->dev = rt->chip->dev;
}

static void mytek_comm_receiver_handler(struct urb *urb)
{
	struct comm_runtime *rt = urb->context;

	if (!rt->chip->shutdown) {
		urb->status = 0;
		urb->actual_length = 0;
		if (usb_submit_urb(urb, GFP_ATOMIC) < 0)
			snd_printk(KERN_WARNING PREFIX
					"comm data receiver aborted.\n");
	}
}

static void mytek_comm_init_buffer(u8 *buffer, u8 id, u8 request,
		u8 reg, u8 vl, u8 vh)
{
	buffer[0] = 0x01;
	buffer[2] = request;
	buffer[3] = id;
	switch (request) {
	case 0x02:
		buffer[1] = 0x05;	/* length (starting at buffer[2]) */
		buffer[4] = reg;
		buffer[5] = vl;
		buffer[6] = vh;
		break;

	case 0x12:
		buffer[1] = 0x0b;	/* length (starting at buffer[2]) */
		buffer[4] = 0x00;
		buffer[5] = 0x18;
		buffer[6] = 0x05;
		buffer[7] = 0x00;
		buffer[8] = 0x01;
		buffer[9] = 0x00;
		buffer[10] = 0x9e;
		buffer[11] = reg;
		buffer[12] = vl;
		break;

	case 0x20:
	case 0x21:
	case 0x22:
		buffer[1] = 0x04;
		buffer[4] = reg;
		buffer[5] = vl;
		break;
	}
}

static int mytek_comm_send_buffer(u8 *buffer, struct usb_device *dev)
{
	int ret;
	int actual_len;

	ret = usb_interrupt_msg(dev, usb_sndintpipe(dev, COMM_EP),
			buffer, buffer[1] + 2, &actual_len, HZ);
	if (ret < 0)
		return ret;
	else if (actual_len != buffer[1] + 2)
		return -EIO;

	return 0;
}

static int mytek_comm_write8(struct comm_runtime *rt, u8 request,
		u8 reg, u8 value)
{
	u8 *buffer;
	int ret;

	/* 13: maximum length of message */
	buffer = kmalloc(13, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	mytek_comm_init_buffer(buffer, rt->cmdid, request, reg, value, 0x00);

	if (rt->cmdid == 255) 
		rt->cmdid = 0;
	else
		rt->cmdid = rt->cmdid+1;

	ret = mytek_comm_send_buffer(buffer, rt->chip->dev);

	kfree(buffer);
	return ret;
}

static int mytek_comm_write16(struct comm_runtime *rt, u8 request,
		u8 reg, u8 vl, u8 vh)
{
	u8 *buffer;
	int ret;

	/* 13: maximum length of message */
	buffer = kmalloc(13, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	mytek_comm_init_buffer(buffer, rt->cmdid, request, reg, vl, vh);

	if (rt->cmdid == 255) 
		rt->cmdid = 0;
	else
		rt->cmdid = rt->cmdid+1;

	ret = mytek_comm_send_buffer(buffer, rt->chip->dev);

	kfree(buffer);
	return ret;
}

int mytek_comm_init(struct mytek_chip *chip)
{
	struct comm_runtime *rt = kzalloc(sizeof(struct comm_runtime),
			GFP_KERNEL);
	struct urb *urb;
	int ret;

	if (!rt)
		return -ENOMEM;

	rt->receiver_buffer = kzalloc(COMM_RECEIVER_BUFSIZE, GFP_KERNEL);
	if (!rt->receiver_buffer) {
		kfree(rt);
		return -ENOMEM;
	}

	urb = &rt->receiver;
	rt->serial = 1;
	rt->chip = chip;
	usb_init_urb(urb);
	rt->init_urb = mytek_comm_init_urb;
	rt->write8 = mytek_comm_write8;
	rt->write16 = mytek_comm_write16;

	/* Initialise unique ID for transmission to and from USBPAL.
	 * Can be used to track responses from USBPAL to issued cmd's
	 * will be increased by one for each cmd sent
	 */
	rt->cmdid = 0;

	/* submit an urb that receives communication data from device */
	urb->transfer_buffer = rt->receiver_buffer;
	urb->transfer_buffer_length = COMM_RECEIVER_BUFSIZE;
	urb->pipe = usb_rcvintpipe(chip->dev, COMM_EP);
	urb->dev = chip->dev;
	urb->complete = mytek_comm_receiver_handler;
	urb->context = rt;
	urb->interval = 1;
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret < 0) {
		kfree(rt->receiver_buffer);
		kfree(rt);
		snd_printk(KERN_ERR PREFIX "cannot create comm data receiver.");
		return ret;
	}
	chip->comm = rt;
	return 0;
}

void mytek_comm_abort(struct mytek_chip *chip)
{
	struct comm_runtime *rt = chip->comm;

	if (rt)
		usb_poison_urb(&rt->receiver);
}

void mytek_comm_destroy(struct mytek_chip *chip)
{
	struct comm_runtime *rt = chip->comm;

	kfree(rt->receiver_buffer);
	kfree(rt);
	chip->comm = NULL;
}
