/*
 * byd.c --- Driver for BYD BTP-10463
 *
 * Copyright (C) 2015, Tai Chi Minh Ralph Eastwood
 * Copyright (C) 2015, Martin Wimpress
 * Copyright (C) 2015, Richard Pospesel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Protocol of BYD Touch Pad reverse-engineered.
 * Datasheet: http://bydit.com/userfiles/file/BTP10463-XXX.pdf
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/input.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/libps2.h>

#include "psmouse.h"
#include "byd.h"

#define DEBUG 1

#define BYD_MODEL_ID_LEN        2
#define BYD_CMD_PAIR(c)		((1 << 12) | (c))
#define BYD_CMD_PAIR_R(r,c)	((1 << 12) | (r << 8) | (c))

/* BYD commands reverse engineered from windows driver */

/* 
 * swipe gesture from off-pad to on-pad
 *  0 : disable
 *  1 : enable
 */
#define BYD_CMD_SET_OFFSCREEN_SWIPE             BYD_CMD_PAIR(0xcc)
/*
 * tap and drag delay time
 *  0 : disable
 *  1 - 8 : least to most delay
 */
#define BYD_CMD_SET_TAP_DRAG_DELAY_TIME         BYD_CMD_PAIR(0xcf)
/*
 * physical buttons function mapping
 *  0 : enable
 *  4 : normal
 *  5 : left button custom command
 *  6 : right button custom command
 *  8 : disable
 */
#define BYD_CMD_SET_PHYSICAL_BUTTONS            BYD_CMD_PAIR(0xd0)
/*
 * absolute mode (1 byte X/Y resolution)
 *  0 : disable
 *  2 : enable
 */
#define BYD_CMD_SET_ABSOLUTE_MODE               BYD_CMD_PAIR(0xd1)
/*
 * two finger scrolling
 *  1 : vertical
 *  2 : horizontal
 *  3 : vertical + horizontal
 *  4 : disable
 */
#define BYD_CMD_SET_TWO_FINGER_SCROLL           BYD_CMD_PAIR(0xd2)
/*
 * handedness
 *  1 : right handed
 *  2 : left handed
 */
#define BYD_CMD_SET_HANDEDNESS                  BYD_CMD_PAIR(0xd3)
/*
 * tap to click
 *  1 : enable
 *  2 : disable
 */
#define BYD_CMD_SET_TAP                         BYD_CMD_PAIR(0xd4)
/*
 * tap and drag
 *  1 : tap and hold to drag
 *  2 : tap and hold to drag + lock
 *  3 : disable
 */
#define BYD_CMD_SET_TAP_DRAG                    BYD_CMD_PAIR(0xd5)
/*
 * touch sensitivity
 *  1 - 7 : least to most sensitive
 */
#define BYD_CMD_SET_TOUCH_SENSITIVITY           BYD_CMD_PAIR(0xd6)
/*
 * one finger scrolling
 *  1 : vertical
 *  2 : horizontal
 *  3 : vertical + horizontal
 *  4 : disable
 */
#define BYD_CMD_SET_ONE_FINGER_SCROLL           BYD_CMD_PAIR(0xd7)
/*
 * one finger scrolling function
 *  1 : free scrolling
 *  2 : edge motion
 *  3 : free scrolling + edge motion
 *  4 : disable
 */
#define BYD_CMD_SET_ONE_FINGER_SCROLL_FUNC      BYD_CMD_PAIR(0xd8)
/*
 * sliding speed
 *  1 - 5 : slowest to fastest
 */
#define BYD_CMD_SET_SLIDING_SPEED               BYD_CMD_PAIR(0xda)
/*
 * edge motion
 *  1 : disable
 *  2 : enable when dragging
 *  3 : enable when dragging and pointing
 */
#define BYD_CMD_SET_EDGE_MOTION                 BYD_CMD_PAIR(0xdb)
/*
 * left edge region size
 *  0 - 7 : smallest to largest width
 */
#define BYD_CMD_SET_LEFT_EDGE_REGION            BYD_CMD_PAIR(0xdc)
/* 
 * top edge region size
 *  0 - 9 : smallest to largest height
 */
#define BYD_CMD_SET_TOP_EDGE_REGION             BYD_CMD_PAIR(0xdd)
/*
 * disregard palm press as clicks
 *  1 - 6 : smallest to largest
 */
#define BYD_CMD_SET_PALM_CHECK                  BYD_CMD_PAIR(0xde)
/* right edge region size
 *  0 - 7 : smallest to largest width
 */
#define BYD_CMD_SET_RIGHT_EDGE_REGION           BYD_CMD_PAIR(0xdf)
/*
 * bottom edge region size
 *  0 - 9 : smallest to largest height
 */
#define BYD_CMD_SET_BOTTOM_EDGE_REGION          BYD_CMD_PAIR(0xe1)
/*
 * multitouch gestures
 *  1 : enable
 *  2 : disable
 */
#define BYD_CMD_SET_MULTITOUCH                  BYD_CMD_PAIR(0xe3)
/*
 * edge motion speed
 *  0 : control with finger pressure
 *  1 - 9 : slowest to fastest
 */
#define BYD_CMD_SET_EDGE_MOTION_SPEED           BYD_CMD_PAIR(0xe4)
/*
 * two finger scolling funtion
 *  1 : free scrolling
 *  2 : edge motion
 *  3 : free scrolling  + edge motion
 *  4 : disable
 */
#define BYD_CMD_SET_TWO_FINGER_SCROLL_FUNC      BYD_CMD_PAIR(0xe5)

struct byd_init_command_pair {
	uint32_t command;
	uint8_t  value;
};

static const struct byd_init_command_pair init_commands[] = {
	{BYD_CMD_SET_HANDEDNESS, 0x01},
	{BYD_CMD_SET_PHYSICAL_BUTTONS, 0x06},
	{BYD_CMD_SET_TAP, 0x02},
	{BYD_CMD_SET_TAP_DRAG, 0x03},
	{BYD_CMD_SET_ONE_FINGER_SCROLL, 0x04},
	{BYD_CMD_SET_SLIDING_SPEED, 0x03},
	{BYD_CMD_SET_EDGE_MOTION, 0x01},
	{BYD_CMD_SET_TOUCH_SENSITIVITY, 0x01},
	{BYD_CMD_SET_PALM_CHECK, 0x00},
	{BYD_CMD_SET_MULTITOUCH, 0x01},
	{BYD_CMD_SET_TAP_DRAG_DELAY_TIME, 0x00},
	{BYD_CMD_SET_TWO_FINGER_SCROLL, 0x03},
	{BYD_CMD_SET_TWO_FINGER_SCROLL_FUNC, 0x01},
	{BYD_CMD_SET_LEFT_EDGE_REGION, 0x00},
	{BYD_CMD_SET_TOP_EDGE_REGION, 0x00},
	{BYD_CMD_SET_RIGHT_EDGE_REGION, 0x0},
	{BYD_CMD_SET_BOTTOM_EDGE_REGION, 0x00},
	{BYD_CMD_SET_ABSOLUTE_MODE, 0x00},
};

struct byd_model_info {
	char name[16];
	char id[BYD_MODEL_ID_LEN];
};

static struct byd_model_info byd_model_data[] = {
	{ "BTP10463", { 0x03, 0x64 } }
};

#if 0
static const unsigned char byd_init_param[] = {

	0xd3, 0x01,  // set right-handedness
	0xd0, 0x00,  // reset button
	0xd0, 0x06,  // send click in both corners as separate gestures
	0xd4, 0x02,  // disable tapping.
	0xd5, 0x03,  // tap and drag off
	0xd7, 0x04,  // edge scrolling off
	0xd8, 0x04,  // edge motion disabled
	0xda, 0x04,  // slide speed fast
	0xdb, 0x01,  // Edge motion off
	0xe4, 0x05,  // Edge motion speed middle.
	0xd6, 0x07,  // Touch Gesture Sensitivity high
	0xde, 0x01,  // Palm detection low: seems to affect gesture detection
	0xe3, 0x01,  // Enable gesture detection
	0xcf, 0x00,  // Tap/Drag delay - off
	0xd2, 0x03,  // Enable two-finger scrolling gesture in both directions
	0xe5, 0x00,  // Two finger continue scrolling at edge - off
	// 0xd9, 0x02,  // unknown - unnecessary?
	// 0xd9, 0x07,  // unknown - unnecessary?
	0xdc, 0x03,  // left edge width medium
	0xdd, 0x03,  // top edge height medium
	0xdf, 0x03,  // right edge height medium
	0xe1, 0x03,  // bottom edge height medium
	0xd1, 0x00,  // no 'absolute' position interleaving
	// 0xe7, 0xe8,   // set scaling normal then double. (have to send be in pairs atm.)
	0xce, 0x00,
	0xcc, 0x00,
	0xe0, 0x00
};
#endif

#define BYD_CMD_GESTURE		 0
#define BYD_CMD_SCROLL_INC	 1
#define BYD_CMD_SCROLL_DEC	-1

struct byd_ext_cmd {
	char type;
	unsigned char code;
	int cmd;
};

static const struct byd_ext_cmd byd_ext_cmd_data[] = {
#if 0
	{ BYD_CMD_SCROLL_DEC, 0x28, REL_Z       }, /* pinch out                 */
	{ BYD_CMD_GESTURE,    0x29, BTN_FORWARD }, /* rotate clockwise          */
	{ BYD_CMD_SCROLL_INC, 0x2a, REL_HWHEEL  }, /* scroll right (two finger) */
	{ BYD_CMD_SCROLL_DEC, 0x2b, REL_WHEEL   }, /* scroll down (two finger)  */
	{ BYD_CMD_GESTURE,    0x2c, BTN_SIDE    }, /* 3-finger-swipe right      */
	{ BYD_CMD_GESTURE,    0x2d, BTN_TASK    }, /* 3-finger-swipe down       */
	{ BYD_CMD_GESTURE,    0x33, BTN_MOUSE+10}, /* four finger down          */
	{ BYD_CMD_SCROLL_INC, 0x35, REL_HWHEEL  }, /* scroll right (region)     */
	{ BYD_CMD_SCROLL_DEC, 0x36, REL_WHEEL,  }, /* scroll down (region)      */
	{ BYD_CMD_GESTURE,    0xd3, BTN_MOUSE+8 }, /* 3-finger-swipe up         */
	{ BYD_CMD_GESTURE,    0xd4, BTN_EXTRA   }, /* 3-finger-swipe left       */
	{ BYD_CMD_SCROLL_INC, 0xd5, REL_WHEEL   }, /* scroll up (two finger)    */
	{ BYD_CMD_SCROLL_DEC, 0xd6, REL_HWHEEL  }, /* scroll left (two finger)  */
	{ BYD_CMD_GESTURE,    0xd7, BTN_BACK    }, /* rotate anti-clockwise     */
	{ BYD_CMD_SCROLL_INC, 0xd8, REL_RZ      }, /* pinch in                  */
	{ BYD_CMD_SCROLL_INC, 0xca, REL_WHEEL   }, /* scroll up (region)        */
	{ BYD_CMD_SCROLL_DEC, 0xcb, REL_HWHEEL  }, /* scroll left (region)      */
	{ BYD_CMD_GESTURE,    0xcd, BTN_MOUSE+9 }, /* four finger up            */
	{ BYD_CMD_GESTURE,    0xd2, BTN_RIGHT   }, /* right corner click        */
	{ BYD_CMD_GESTURE,    0x2e, BTN_LEFT    }, /* left corner click         */
#endif
	{ BYD_CMD_GESTURE,    0x2e, BTN_LEFT    }, /* left corner click         */
	{ BYD_CMD_GESTURE,    0xd2, BTN_RIGHT   }, /* right corner click        */	
	{ BYD_CMD_SCROLL_DEC, 0x2b, REL_WHEEL   }, /* scroll down (two finger)  */
	{ BYD_CMD_SCROLL_INC, 0xd5, REL_WHEEL   }, /* scroll up (two finger)    */
	{ BYD_CMD_SCROLL_DEC, 0xd6, REL_HWHEEL  }, /* scroll left (two finger)  */
	{ BYD_CMD_SCROLL_INC, 0x2a, REL_HWHEEL  }, /* scroll right (two finger) */
};

struct byd_data {
	unsigned char ext_lookup[256];
};

static psmouse_ret_t byd_process_byte(struct psmouse *psmouse)
{
	struct byd_data *priv = psmouse->private;
	struct input_dev *dev = psmouse->dev;
	unsigned char *packet = psmouse->packet;
	int i;

	if (psmouse->pktcnt < psmouse->pktsize)
		return PSMOUSE_GOOD_DATA;

#ifdef DEBUG
	psmouse_dbg(psmouse, "process: packet = %x %x %x %x\n",
			packet[0], packet[1], packet[2], packet[3]);
#endif

	input_report_key(dev, BTN_LEFT,    packet[0]       & 1);
	input_report_key(dev, BTN_MIDDLE, (packet[0] >> 2) & 1);
	input_report_key(dev, BTN_RIGHT,  (packet[0] >> 1) & 1);

	if (packet[3]) {
		i = priv->ext_lookup[packet[3]];
		if (i != 0xff && byd_ext_cmd_data[i].code == packet[3]) {
#ifdef DEBUG
			psmouse_dbg(psmouse, "process: %x %x\n",
					byd_ext_cmd_data[i].code,
					byd_ext_cmd_data[i].cmd);
#endif
			if (byd_ext_cmd_data[i].type == BYD_CMD_GESTURE) {
				input_report_key(dev, byd_ext_cmd_data[i].cmd, 1);
				input_report_key(dev, byd_ext_cmd_data[i].cmd, 0);
			} else {
				input_report_rel(dev, byd_ext_cmd_data[i].cmd,
						byd_ext_cmd_data[i].type);
			}
		} else {
			psmouse_warn(psmouse, "unknown code detected %x\n", packet[3]);
		}
	} else {
		input_report_rel(dev, REL_X, packet[1] ? (int) packet[1] - (int) ((packet[0] << 4) & 0x100) : 0);
		input_report_rel(dev, REL_Y, packet[2] ? (int) ((packet[0] << 3) & 0x100) - (int) packet[2] : 0);
	}

	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

int byd_init(struct psmouse *psmouse)
{
	struct byd_data *priv;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];
	int cmd, error = 0;
	int i = 0;

	/* it needs to be initialised like an intellimouse to get 4-byte packets */
	psmouse_reset(psmouse);
	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 100;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);

	if (param[0] != 3)
		return -1;

#ifdef DEBUG
	psmouse_dbg(psmouse, "detect: init sequence\n");
#endif

	/* activate the mouse to initialise it */
	psmouse_activate(psmouse);

	/* enter command mode */
	param[0] = 0x00;
	if (ps2_command(ps2dev, param, BYD_CMD_PAIR(0xe2))) {
		error = -EIO;
		goto init_fail;
	}
#ifdef DEBUG
	psmouse_dbg(psmouse, "detect: entered command mode\n");
#endif

	/* send second identification command */
	param[0] = 0x02;
	if (ps2_command(ps2dev, param, BYD_CMD_PAIR(0xe0))) {
		error = -EIO;
		goto init_fail;
	}

	param[0] = 0x01;
	if (ps2_command(ps2dev, param, BYD_CMD_PAIR_R(4, 0xe0))) {
		error = -EIO;
		goto init_fail;
	}

#ifdef DEBUG
	psmouse_dbg(psmouse, "detect: magic %x %x %x %x\n",
			param[0], param[1], param[2], param[3]);
#endif

	/* magic identifier the vendor driver reads */
	if (param[0] != 0x08 || param[1] != 0x01 ||
	    param[2] != 0x01 || param[3] != 0x31) {
		psmouse_err(psmouse, "unknown magic, expected: 08 01 01 31\n");
		error = -EINVAL;
		goto init_fail;
	}

	/*
	 * send the byd vendor commands
	 * these appear to be pairs of (command, param)
	 */
	for(i = 0; i < ARRAY_SIZE(init_commands); i++) {
		param[0] = init_commands[i].value;
		cmd = init_commands[i].command;
		if(ps2_command(ps2dev, param, cmd)) {
			error = -EIO;
			goto init_fail;
		}
	}

	/* confirm/finalize the above vender command table */
	param[0] = 0x00;
	if (ps2_command(ps2dev, param, BYD_CMD_PAIR(0xe0))) {
		error = -EIO;
		goto init_fail;
	}

	/* exit command mode */
	param[0] = 0x01;
	if (ps2_command(ps2dev, param, BYD_CMD_PAIR(0xe2))) {
		error = -ENOMEM;
		goto init_fail;
	}

	/* set scaling to double - makes low-speed a bit more sane */
	psmouse->set_scale(psmouse, PSMOUSE_SCALE21);

	/* build lookup table for extended commands */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto init_fail;
	}

	memset(priv, 0xff, sizeof(*priv));
	for (i = 0; i < ARRAY_SIZE(byd_ext_cmd_data); i++) {
		priv->ext_lookup[byd_ext_cmd_data[i].code] = i & 0xff;
	}
	psmouse->private = priv;

#ifdef DEBUG
	psmouse_dbg(psmouse, "detect: exit command mode\n");
#endif

	return 0;

init_fail:
	psmouse_deactivate(psmouse);
	return error;
}

static void byd_disconnect(struct psmouse *psmouse)
{
	if (psmouse->private)
		kfree(psmouse->private);
	psmouse->private = NULL;
}

static int byd_reconnect(struct psmouse *psmouse)
{
	if (byd_detect(psmouse, 0))
		return -1;

	if (byd_init(psmouse))
		return -1;

	return 0;
}

int byd_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];
	int i;

	/* reset the mouse */
	psmouse_reset(psmouse);

	/* magic knock - identify the mouse (as per. the datasheet) */
	param[0] = 0x03;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -EIO;

	psmouse_dbg(psmouse, "detect: model id: %x %x %x\n",
			param[0], param[1], param[2]);

	/*
	 * match the device - the first byte, param[0], appears to be set
	 * to some unknown value based on the state of the mouse and cannot
	 * be used for identification after suspend.
	 */
	for (i = 0; i < ARRAY_SIZE(byd_model_data); i++) {
		if (!memcmp(param + 1, &byd_model_data[i].id,
				 BYD_MODEL_ID_LEN))
			break;
	}

	/* no match found */
	if (i == ARRAY_SIZE(byd_model_data)) {
		psmouse_dbg(psmouse, "detect: no match found\n");
		return -EINVAL;
	} else {
		psmouse_dbg(psmouse, "detect: matched %s\n",
				byd_model_data[i].name);
	}

	if (set_properties) {
		#if 0
		__set_bit(BTN_SIDE, psmouse->dev->keybit);
		__set_bit(BTN_FORWARD, psmouse->dev->keybit);
		__set_bit(BTN_BACK, psmouse->dev->keybit);
		__set_bit(BTN_TASK, psmouse->dev->keybit);
		__set_bit(BTN_MOUSE+8, psmouse->dev->keybit);
		__set_bit(BTN_MOUSE+9, psmouse->dev->keybit);
		__set_bit(BTN_MOUSE+10, psmouse->dev->keybit);
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(REL_WHEEL, psmouse->dev->relbit);
		__set_bit(REL_HWHEEL, psmouse->dev->relbit);
		__set_bit(REL_MISC, psmouse->dev->relbit);
		__set_bit(REL_Z, psmouse->dev->relbit);
		__set_bit(REL_RZ, psmouse->dev->relbit);
		#endif
		
		struct input_dev *dev = psmouse->dev;

		__set_bit(INPUT_PROP_BUTTONPAD, dev->propbit);

 		__set_bit(BTN_TOUCH, dev->keybit);
		__set_bit(BTN_TOOL_FINGER, dev->keybit);
		
		__set_bit(EV_KEY, dev->evbit);
		__set_bit(EV_REL, dev->evbit);
		__set_bit(REL_X, dev->relbit);
		__set_bit(REL_Y, dev->relbit);

		__set_bit(REL_WHEEL, dev->relbit);
		__set_bit(REL_HWHEEL, dev->relbit);

		psmouse->vendor = "BYD";
		//psmouse->name = byd_model_data[i].name;
		psmouse->name = "TouchPad";
		psmouse->protocol_handler = byd_process_byte;
		psmouse->pktsize = 4;
		psmouse->private = NULL;
		psmouse->disconnect = byd_disconnect;
		psmouse->reconnect = byd_reconnect;
	}

	return 0;
}
