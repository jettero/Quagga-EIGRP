/*
 * EIGRPd main header.
 * Copyright (C) 1998, 99, 2000 Kunihiro Ishiguro, Toshiaki Takada
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _ZEBRA_EIGRPD_H
#define _ZEBRA_EIGRPD_H

#include <zebra.h>

#include "filter.h"
#include "log.h"


/* Default protocol, port number. */
#ifndef IPPROTO_EIGRPIGP
#define IPPROTO_EIGRPIGP         88
#endif /* IPPROTO_EIGRPIGP */

/* IP TTL for EIGRP protocol. */
#define EIGRP_IP_TTL             1

/* VTY port number. */
#define EIGRP_VTY_PORT          2609

/* Default configuration file name for eigrp. */
#define EIGRP_DEFAULT_CONFIG   "eigrpd.conf"

#define EIGRP_HELLO_INTERVAL_DEFAULT        5
#define EIGRP_HOLD_INTERVAL_DEFAULT         15

#define EIGRP_MULTICAST_ADDRESS                0xe000000A /*224.0.0.10*/


/* EIGRP master for system wide configuration and variables. */
struct eigrp_master
{
  /* EIGRP instance. */
  struct list *eigrp;

  /* EIGRP thread master. */
  struct thread_master *master;

  /* Zebra interface list. */
  struct list *iflist;

  /* EIGRP start time. */
  time_t start_time;

  /* Various EIGRP global configuration. */
    u_char options;

#define EIGRP_MASTER_SHUTDOWN (1 << 0) /* deferred-shutdown */
};

struct eigrp
{

  /* EIGRP Router ID. */
  struct in_addr router_id;             /* Configured automatically. */
  struct in_addr router_id_static;      /* Configured manually. */

  struct list *eiflist;                 /* eigrp interfaces */
  u_char passive_interface_default;   /* passive-interface default */

  int AS; /* Autonomous system number */

  unsigned int fd;
  unsigned int maxsndbuflen;

  u_int32_t sequence_number;    /*Global EIGRP sequence number*/

  struct stream *ibuf;
  struct list *oi_write_q;

  /*Threads*/
  struct thread *t_write;
  struct thread *t_read;

  struct route_table *networks;         /* EIGRP config networks. */

  u_char k_values[5]; /*Array for K values configuration*/

};

/* Extern variables. */
extern struct zclient *zclient;
extern struct thread_master *master;
extern struct eigrp_master *eigrp_om;

/* Prototypes */
 extern void eigrp_master_init (void);
 extern void eigrp_terminate (void);
 extern void eigrp_finish (struct eigrp *);
 extern struct eigrp *eigrp_get (const char *);
 extern struct eigrp *eigrp_lookup (void);
 extern void eigrp_router_id_update (struct eigrp *);

#endif /* _ZEBRA_EIGRPD_H */