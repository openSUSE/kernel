#ifndef _GAMEPORT_H
#define _GAMEPORT_H

/*
 *  Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <asm/io.h>
#include <linux/input.h>
#include <linux/list.h>
#include <linux/device.h>

struct gameport {

	void *private;		/* Private pointer for joystick drivers */
	void *port_data;	/* Private pointer for gameport drivers */
	char *name;
	char name_buf[32];
	char *phys;
	char phys_buf[32];

	struct input_id id;

	int io;
	int speed;
	int fuzz;

	void (*trigger)(struct gameport *);
	unsigned char (*read)(struct gameport *);
	int (*cooked_read)(struct gameport *, int *, int *);
	int (*calibrate)(struct gameport *, int *, int *);
	int (*open)(struct gameport *, int);
	void (*close)(struct gameport *);

	struct gameport_driver *drv;
	struct device dev;

	struct list_head node;

	/* temporary, till sysfs transition is complete */
	int dyn_alloc;
};

struct gameport_driver {

	void *private;
	char *name;

	void (*connect)(struct gameport *, struct gameport_driver *drv);
	void (*disconnect)(struct gameport *);

	struct list_head node;
};

int gameport_open(struct gameport *gameport, struct gameport_driver *drv, int mode);
void gameport_close(struct gameport *gameport);
void gameport_rescan(struct gameport *gameport);

static inline struct gameport *gameport_allocate_port(void)
{
	struct gameport *gameport = kcalloc(1, sizeof(struct gameport), GFP_KERNEL);

	if (gameport)
		gameport->dyn_alloc = 1;

	return gameport;
}

static inline void gameport_free_port(struct gameport *gameport)
{
	kfree(gameport);
}

static inline void gameport_set_name(struct gameport *gameport, const char *name)
{
	gameport->name = gameport->name_buf;
	strlcpy(gameport->name, name, sizeof(gameport->name_buf));
}

void gameport_set_phys(struct gameport *gameport, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

void gameport_register_port(struct gameport *gameport);
void gameport_unregister_port(struct gameport *gameport);

void gameport_register_driver(struct gameport_driver *drv);
void gameport_unregister_driver(struct gameport_driver *drv);

#define GAMEPORT_MODE_DISABLED		0
#define GAMEPORT_MODE_RAW		1
#define GAMEPORT_MODE_COOKED		2

#define GAMEPORT_ID_VENDOR_ANALOG	0x0001
#define GAMEPORT_ID_VENDOR_MADCATZ	0x0002
#define GAMEPORT_ID_VENDOR_LOGITECH	0x0003
#define GAMEPORT_ID_VENDOR_CREATIVE	0x0004
#define GAMEPORT_ID_VENDOR_GENIUS	0x0005
#define GAMEPORT_ID_VENDOR_INTERACT	0x0006
#define GAMEPORT_ID_VENDOR_MICROSOFT	0x0007
#define GAMEPORT_ID_VENDOR_THRUSTMASTER	0x0008
#define GAMEPORT_ID_VENDOR_GRAVIS	0x0009
#define GAMEPORT_ID_VENDOR_GUILLEMOT	0x000a

static __inline__ void gameport_trigger(struct gameport *gameport)
{
	if (gameport->trigger)
		gameport->trigger(gameport);
	else
		outb(0xff, gameport->io);
}

static __inline__ unsigned char gameport_read(struct gameport *gameport)
{
	if (gameport->read)
		return gameport->read(gameport);
	else
		return inb(gameport->io);
}

static __inline__ int gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	if (gameport->cooked_read)
		return gameport->cooked_read(gameport, axes, buttons);
	else
		return -1;
}

static __inline__ int gameport_calibrate(struct gameport *gameport, int *axes, int *max)
{
	if (gameport->calibrate)
		return gameport->calibrate(gameport, axes, max);
	else
		return -1;
}

static __inline__ int gameport_time(struct gameport *gameport, int time)
{
	return (time * gameport->speed) / 1000;
}

#endif
