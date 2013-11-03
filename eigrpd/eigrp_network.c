/*
 * EIGRP network related functions.
 *   Copyright (C) 1999 Toshiaki Takada
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


#include <zebra.h>

#include "thread.h"
#include "linklist.h"
#include "prefix.h"
#include "if.h"
#include "sockunion.h"
#include "log.h"
#include "sockopt.h"
#include "privs.h"
#include "table.h"

extern struct zebra_privs_t eigrpd_privs;

#include "eigrpd/eigrpd.h"
#include "eigrpd/eigrp_interface.h"
#include "eigrpd/eigrp_packet.h"
#include "eigrpd/eigrp_zebra.h"
#include "eigrpd/eigrp_vty.h"
#include "eigrpd/eigrp_neighbor.h"
#include "eigrpd/eigrp_network.h"


static int eigrp_network_match_iface(const struct connected *,
                                     const struct prefix *);
static void eigrp_network_run_interface (struct eigrp *,
                                         struct prefix *,
                                         struct interface *);

int
eigrp_sock_init (void)
{
  int eigrp_sock;
  int ret, hincl = 1;

  if ( eigrpd_privs.change (ZPRIVS_RAISE) )
    zlog_err ("eigrp_sock_init: could not raise privs, %s",
               safe_strerror (errno) );

  eigrp_sock = socket (AF_INET, SOCK_RAW, IPPROTO_EIGRPIGP);
  if (eigrp_sock < 0)
    {
      int save_errno = errno;
      if ( eigrpd_privs.change (ZPRIVS_LOWER) )
        zlog_err ("eigrp_sock_init: could not lower privs, %s",
                   safe_strerror (errno) );
      zlog_err ("eigrp_read_sock_init: socket: %s", safe_strerror (save_errno));
      exit(1);
    }

#ifdef IP_HDRINCL
  /* we will include IP header with packet */
  ret = setsockopt (eigrp_sock, IPPROTO_IP, IP_HDRINCL, &hincl, sizeof (hincl));
  if (ret < 0)
    {
      int save_errno = errno;
      if ( eigrpd_privs.change (ZPRIVS_LOWER) )
        zlog_err ("eigrp_sock_init: could not lower privs, %s",
                   safe_strerror (errno) );
      zlog_warn ("Can't set IP_HDRINCL option for fd %d: %s",
                 eigrp_sock, safe_strerror(save_errno));

    }
#elif defined (IPTOS_PREC_INTERNETCONTROL)
#warning "IP_HDRINCL not available on this system"
#warning "using IPTOS_PREC_INTERNETCONTROL"
  ret = setsockopt_ipv4_tos(eigrp_sock, IPTOS_PREC_INTERNETCONTROL);
  if (ret < 0)
    {
      int save_errno = errno;
      if ( eigrpd_privs.change (ZPRIVS_LOWER) )
        zlog_err ("eigrpd_sock_init: could not lower privs, %s",
                   safe_strerror (errno) );
      zlog_warn ("can't set sockopt IP_TOS %d to socket %d: %s",
                 tos, eigrp_sock, safe_strerror(save_errno));
      close (eigrp_sock);        /* Prevent sd leak. */
      return ret;
    }
#else /* !IPTOS_PREC_INTERNETCONTROL */
#warning "IP_HDRINCL not available, nor is IPTOS_PREC_INTERNETCONTROL"
  zlog_warn ("IP_HDRINCL option not available");
#endif /* IP_HDRINCL */

  ret = setsockopt_ifindex (AF_INET, eigrp_sock, 1);

  if (ret < 0)
     zlog_warn ("Can't set pktinfo option for fd %d", eigrp_sock);

  if (eigrpd_privs.change (ZPRIVS_LOWER))
    {
      zlog_err ("eigrp_sock_init: could not lower privs, %s",
               safe_strerror (errno) );
    }

  return eigrp_sock;
}

void
eigrp_adjust_sndbuflen (struct eigrp * eigrp, unsigned int buflen)
{
  int ret, newbuflen;
  /* Check if any work has to be done at all. */
  if (eigrp->maxsndbuflen >= buflen)
    return;
  if (eigrpd_privs.change (ZPRIVS_RAISE))
    zlog_err ("%s: could not raise privs, %s", __func__,
      safe_strerror (errno));
  /* Now we try to set SO_SNDBUF to what our caller has requested
   * (the MTU of a newly added interface). However, if the OS has
   * truncated the actual buffer size to somewhat less size, try
   * to detect it and update our records appropriately. The OS
   * may allocate more buffer space, than requested, this isn't
   * a error.
   */
  ret = setsockopt_so_sendbuf (eigrp->fd, buflen);
  newbuflen = getsockopt_so_sendbuf (eigrp->fd);
  if (ret < 0 || newbuflen < 0 || newbuflen < (int) buflen)
    zlog_warn ("%s: tried to set SO_SNDBUF to %u, but got %d",
      __func__, buflen, newbuflen);
  if (newbuflen >= 0)
    eigrp->maxsndbuflen = (unsigned int)newbuflen;
  else
    zlog_warn ("%s: failed to get SO_SNDBUF", __func__);
  if (eigrpd_privs.change (ZPRIVS_LOWER))
    zlog_err ("%s: could not lower privs, %s", __func__,
      safe_strerror (errno));
}

int
eigrp_if_ipmulticast (struct eigrp *top, struct prefix *p, unsigned int ifindex)
{
  u_char val;
  int ret, len;

  val = 0;
  len = sizeof (val);

  /* Prevent receiving self-origined multicast packets. */
  ret = setsockopt (top->fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void *)&val, len);
  if (ret < 0)
    zlog_warn ("can't setsockopt IP_MULTICAST_LOOP(0) for fd %d: %s",
               top->fd, safe_strerror(errno));

  /* Explicitly set multicast ttl to 1 -- endo. */
  val = 1;
  ret = setsockopt (top->fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&val, len);
  if (ret < 0)
    zlog_warn ("can't setsockopt IP_MULTICAST_TTL(1) for fd %d: %s",
               top->fd, safe_strerror (errno));

  ret = setsockopt_ipv4_multicast_if (top->fd, ifindex);
  if (ret < 0)
    zlog_warn("can't setsockopt IP_MULTICAST_IF(fd %d, addr %s, "
              "ifindex %u): %s",
              top->fd, inet_ntoa(p->u.prefix4), ifindex, safe_strerror(errno));

  return ret;
}

/* Join to the EIGRP multicast group. */
int
eigrp_if_add_allspfrouters (struct eigrp *top, struct prefix *p,
                           unsigned int ifindex)
{
  int ret;

  ret = setsockopt_ipv4_multicast (top->fd, IP_ADD_MEMBERSHIP,
                                   htonl (EIGRP_MULTICAST_ADDRESS),
                                   ifindex);
  if (ret < 0)
    zlog_warn ("can't setsockopt IP_ADD_MEMBERSHIP (fd %d, addr %s, "
               "ifindex %u, AllSPFRouters): %s; perhaps a kernel limit "
               "on # of multicast group memberships has been exceeded?",
               top->fd, inet_ntoa(p->u.prefix4), ifindex, safe_strerror(errno));
  else
    zlog_debug ("interface %s [%u] join EIGRP Multicast group.",
               inet_ntoa (p->u.prefix4), ifindex);

  return ret;
}

int
eigrp_network_set (struct eigrp *eigrp, struct prefix_ipv4 *p)
{
  struct route_node *rn;
  struct interface *ifp;
  struct listnode *node;

  rn = route_node_get (eigrp->networks, (struct prefix *)p);
  if (rn->info)
      {
        /* There is already same network statement. */
        route_unlock_node (rn);
        return 0;
      }

  rn->info = (void *)1;

  /* Schedule Router ID Update. */
    if (eigrp->router_id.s_addr == 0)
      eigrp_router_id_update (eigrp);

  /* Run network config now. */
  /* Get target interface. */
   for (ALL_LIST_ELEMENTS_RO (eigrp_om->iflist, node, ifp))
     eigrp_network_run_interface (eigrp, (struct prefix *)p, ifp);

  return 1;
}

/* Check whether interface matches given network
 * returns: 1, true. 0, false
 */
static int
eigrp_network_match_iface(const struct connected *co, const struct prefix *net)
{
  /* new approach: more elegant and conceptually clean */
  return prefix_match(net, CONNECTED_PREFIX(co));
}

static void
eigrp_network_run_interface (struct eigrp *eigrp, struct prefix *p, struct interface *ifp)
{
  struct listnode *cnode;
  struct connected *co;

  /* if interface prefix is match specified prefix,
     then create socket and join multicast group. */
  for (ALL_LIST_ELEMENTS_RO (ifp->connected, cnode, co))
    {

      if (CHECK_FLAG(co->flags,ZEBRA_IFA_SECONDARY))
        continue;

      if (p->family == co->address->family
          && ! eigrp_if_table_lookup(ifp, co->address)
          && eigrp_network_match_iface(co,p))
        {
           struct eigrp_interface *ei;

            ei = eigrp_if_new (eigrp, ifp, co->address);
            ei->connected = co;

            ei->params = eigrp_lookup_if_params (ifp, ei->address->u.prefix4);

            /* Relate eigrp interface to eigrp instance. */
            ei->eigrp = eigrp;

            /* update network type as interface flag */
            /* If network type is specified previously,
               skip network type setting. */
            ei->type = IF_DEF_PARAMS (ifp)->type;

            /* if router_id is not configured, dont bring up
             * interfaces.
             * ospf_router_id_update() will call ospf_if_update
             * whenever r-id is configured instead.
             */
            if ((eigrp->router_id.s_addr != 0)
                            && if_is_operative (ifp))
              eigrp_if_up (ei);
          }
    }
}

int
eigrp_hello_timer (struct thread *thread)
{
//  if (IS_DEBUG_OSPF (ism, ISM_TIMERS))
//    zlog (NULL, LOG_DEBUG, "ISM[%s]: Timer (Hello timer expire)",
//          IF_NAME (oi));

  struct eigrp_interface *ei;

  ei = THREAD_ARG (thread);
  ei->t_hello = NULL;

  /* Sending hello packet. */
  eigrp_hello_send (ei);

  /* Hello timer set. */
  thread_add_timer (master, eigrp_hello_timer, ei, EIGRP_IF_PARAM (ei, v_hello));

  return 0;
}

void
eigrp_if_update (struct eigrp *eigrp, struct interface *ifp)
{
  struct route_node *rn;

  if (!eigrp)
    eigrp = eigrp_lookup ();

  /* EIGRP must be on and Router-ID must be configured. */
  if (!eigrp || eigrp->router_id.s_addr == 0)
    return;

  /* Run each network for this interface. */
  for (rn = route_top (eigrp->networks); rn; rn = route_next (rn))
    if (rn->info != NULL)
      {
        eigrp_network_run_interface (eigrp, &rn->p, ifp);
      }
}
