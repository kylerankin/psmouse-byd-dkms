/*
 * byd.h --- Driver for BYD Touchpad
 *
 * Copyright (C) 2015, Tai Chi Minh Ralph Eastwood
 * Copyright (C) 2015, Martin Wimpress
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef __BYD_H
#define __BYD_H

int byd_detect(struct psmouse *psmouse, bool set_properties);
int byd_init(struct psmouse *psmouse);

#endif /* !__BYD_H */
