/*
 * EIGRP Sending and Receiving EIGRP Packets.
 * Copyright (C) 1999, 2000 Toshiaki Takada
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

#ifndef _ZEBRA_EIGRP_PACKET_H
#define _ZEBRA_EIGRP_PACKET_H

#define EIGRP_MAX_PACKET_SIZE  65535U   /* includes IP Header size. */
#define EIGRP_HEADER_SIZE         20U
#define EIGRP_HELLO_MIN_SIZE      12U

#define EIGRP_HEADER_FLAG_INIT          Ox00000001
#define EIGRP_HEADER_FLAG_CR            Ox00000010
#define EIGRP_HEADER_FLAG_RESET         Ox00000100
#define EIGRP_HEADER_FLAG_EOT           Ox00001000

#define EIGRP_MSG_UPDATE        1  /* EIGRP Hello Message. */
#define EIGRP_MSG_REQUEST       2  /* EIGRP Database Descriptoin Message. */
#define EIGRP_MSG_QUERY         3  /* EIGRP Link State Request Message. */
#define EIGRP_MSG_REPLY         4  /* EIGRP Link State Update Message. */
#define EIGRP_MSG_HELLO         5  /* EIGRP Link State Acknoledgement Message. */
#define EIGRP_MSG_PROBE         7  /* EIGRP Probe Message. */
#define EIGRP_MSG_SIAQUERY     10  /* EIGRP SIAQUERY. */
#define EIGRP_MSG_SIAREPLY     11  /* EIGRP SIAREPLY. */

/*EIGRP TLV Type definitions*/
#define TLV_PARAMETER_TYPE              0x0001       /*K types*/
#define TLV_AUTHENTICATION_TYPE         0x0002
#define TLV_SEQUENCE_TYPE               0x0003
#define TLV_SOFTWARE_VERSION_TYPE       0x0004
#define TLV_MULTICAST_SEQUENCE_TYPE     0x0005
#define TLV_PEER_INFORMATION_TYPE       0x0006
#define TLV_PEER_TERMINATION_TYPE       0x0007
#define TLV_PEER_TID_LIST_TYPE          0x0008

/*Packet requiring ack will be retransmitted again after this time*/
#define EIGRP_PACKET_RETRANS_TIME        5 /* in seconds */


/* Return values of functions involved in packet verification */
#define MSG_OK    0
#define MSG_NG    1

#define EIGRP_HEADER_VERSION            2

struct eigrp_packet
{
  struct eigrp_packet *next;

  /* Pointer to data stream. */
  struct stream *s;

  /* IP destination address. */
  struct in_addr dst;

  /* EIGRP packet length. */
  u_int16_t length;
};

struct eigrp_fifo
{
  unsigned long count;

  struct eigrp_packet *head;

  struct eigrp_packet *tail;
};

struct eigrp_header
{
  u_char version;
  u_char opcode;
  u_int16_t checksum;
  u_int32_t flags;
  u_int32_t sequence;
  u_int32_t ack;
  u_int16_t routerID;
  u_int16_t ASNumber;

} __attribute__((packed));

struct TLV_Parameter_Type
{
    u_int16_t type;
    u_int16_t length;
    u_char K1;
    u_char K2;
    u_char K3;
    u_char K4;
    u_char K5;
    u_char K6;
    u_int16_t hold_time;
} __attribute__((packed));



/*Prototypes*/
extern int eigrp_read (struct thread *);
extern struct eigrp_fifo *eigrp_fifo_new (void);
extern struct eigrp_packet *eigrp_packet_new (size_t);
extern void eigrp_hello_send (struct eigrp_interface *);
extern void eigrp_update_send (struct eigrp_interface *);
extern struct eigrp_packet *eigrp_fifo_head (struct eigrp_fifo *);
extern void eigrp_packet_delete (struct eigrp_interface *);
extern struct eigrp_packet *eigrp_fifo_pop (struct eigrp_fifo *);
extern void eigrp_packet_free (struct eigrp_packet *);
extern void eigrp_fifo_free (struct eigrp_fifo *);
extern void eigrp_ack_send(struct eigrp_neighbor *);
extern void eigrp_send_init_update (struct eigrp_neighbor *);

extern int eigrp_unack_packet_retrans(struct thread *);


#endif /* _ZEBRA_EIGRP_PACKET_H */