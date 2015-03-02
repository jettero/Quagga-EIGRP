/*
 * EIGRPd Finite State Machine (DUAL).
 * Copyright (C) 2013-2014
 * Authors:
 *   Donnie Savage
 *   Jan Janovic
 *   Matej Perina
 *   Peter Orsag
 *   Peter Paluch
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
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 *
 * This file contains functions for executing logic of finite state machine
 *
 *                                +------------ +
 *                                |     (7)     |
 *                                |             v
 *                    +=====================================+
 *                    |                                     |
 *                    |              Passive                |
 *                    |                                     |
 *                    +=====================================+
 *                        ^     |     ^     ^     ^    |
 *                     (3)|     |  (1)|     |  (1)|    |
 *                        |  (0)|     |  (3)|     | (2)|
 *                        |     |     |     |     |    +---------------+
 *                        |     |     |     |     |                     \
 *              +--------+      |     |     |     +-----------------+    \
 *            /                /     /      |                        \    \
 *          /                /     /        +----+                    \    \
 *         |                |     |               |                    |    |
 *         |                v     |               |                    |    v
 *    +===========+   (6)  +===========+       +===========+   (6)   +===========+
 *    |           |------->|           |  (5)  |           |-------->|           |
 *    |           |   (4)  |           |------>|           |   (4)   |           |
 *    | ACTIVE 0  |<-------| ACTIVE 1  |       | ACTIVE 2  |<--------| ACTIVE 3  |
 * +--|           |     +--|           |    +--|           |      +--|           |
 * |  +===========+     |  +===========+    |  +===========+      |  +===========+
 * |       ^  |(5)      |      ^            |    ^    ^           |         ^
 * |       |  +---------|------|------------|----+    |           |         |
 * +-------+            +------+            +---------+           +---------+
 *    (7)                 (7)                  (7)                   (7)
 *
 * 0- input event other than query from successor, FC not satisfied
 * 1- last reply, FD is reset
 * 2- query from successor, FC not satisfied
 * 3- last reply, FC satisfied with current value of FDij
 * 4- distance increase while in active state
 * 5- query from successor while in active state
 * 6- last reply, FC not satisfied with current value of FDij
 * 7- state not changed, usually by receiving not last reply
 *
 */

#include <thread.h>
#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "log.h"
#include "linklist.h"

#include "eigrpd/eigrp_structs.h"
#include "eigrpd/eigrpd.h"
#include "eigrpd/eigrp_interface.h"
#include "eigrpd/eigrp_neighbor.h"
#include "eigrpd/eigrp_packet.h"
#include "eigrpd/eigrp_zebra.h"
#include "eigrpd/eigrp_vty.h"
#include "eigrpd/eigrp_network.h"
#include "eigrpd/eigrp_dump.h"
#include "eigrpd/eigrp_topology.h"
#include "eigrpd/eigrp_fsm.h"

/*
 * Prototypes
 */
int
eigrp_fsm_event_keep_state(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_nq_fcn(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_q_fcn(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_lr(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_dinc(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_lr_fcs(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_lr_fcn(struct eigrp_fsm_action_message *);
int
eigrp_fsm_event_qact(struct eigrp_fsm_action_message *);

//---------------------------------------------------------------------

/*
 * NSM - field of fields of struct containing one function each.
 * Which function is used depends on actual state of FSM and occurred
 * event(arrow in diagram). Usage:
 * NSM[actual/starting state][occurred event].func
 * Functions are should be executed within separate thread.
 */
struct {
	int
	(*func)(struct eigrp_fsm_action_message *);
} NSM[EIGRP_FSM_STATE_MAX][EIGRP_FSM_EVENT_MAX] = { {
//PASSIVE STATE
		{ eigrp_fsm_event_nq_fcn }, /* Event 0 */
		{ eigrp_fsm_event_keep_state }, /* Event 1 */
		{ eigrp_fsm_event_q_fcn }, /* Event 2 */
		{ eigrp_fsm_event_keep_state }, /* Event 3 */
		{ eigrp_fsm_event_keep_state }, /* Event 4 */
		{ eigrp_fsm_event_keep_state }, /* Event 5 */
		{ eigrp_fsm_event_keep_state }, /* Event 6 */
		{ eigrp_fsm_event_keep_state }, /* Event 7 */
}, {
//Active 0 state
		{ eigrp_fsm_event_keep_state }, /* Event 0 */
		{ eigrp_fsm_event_keep_state }, /* Event 1 */
		{ eigrp_fsm_event_keep_state }, /* Event 2 */
		{ eigrp_fsm_event_lr_fcs }, /* Event 3 */
		{ eigrp_fsm_event_keep_state }, /* Event 4 */
		{ eigrp_fsm_event_qact }, /* Event 5 */
		{ eigrp_fsm_event_lr_fcn }, /* Event 6 */
		{ eigrp_fsm_event_keep_state }, /* Event 7 */

}, {
//Active 1 state
		{ eigrp_fsm_event_keep_state }, /* Event 0 */
		{ eigrp_fsm_event_lr }, /* Event 1 */
		{ eigrp_fsm_event_keep_state }, /* Event 2 */
		{ eigrp_fsm_event_keep_state }, /* Event 3 */
		{ eigrp_fsm_event_dinc }, /* Event 4 */
		{ eigrp_fsm_event_qact }, /* Event 5 */
		{ eigrp_fsm_event_keep_state }, /* Event 6 */
		{ eigrp_fsm_event_keep_state }, /* Event 7 */
}, {
//Active 2 state
		{ eigrp_fsm_event_keep_state }, /* Event 0 */
		{ eigrp_fsm_event_keep_state }, /* Event 1 */
		{ eigrp_fsm_event_keep_state }, /* Event 2 */
		{ eigrp_fsm_event_lr_fcs }, /* Event 3 */
		{ eigrp_fsm_event_keep_state }, /* Event 4 */
		{ eigrp_fsm_event_keep_state }, /* Event 5 */
		{ eigrp_fsm_event_lr_fcn }, /* Event 6 */
		{ eigrp_fsm_event_keep_state }, /* Event 7 */
}, {
//Active 3 state
		{ eigrp_fsm_event_keep_state }, /* Event 0 */
		{ eigrp_fsm_event_lr }, /* Event 1 */
		{ eigrp_fsm_event_keep_state }, /* Event 2 */
		{ eigrp_fsm_event_keep_state }, /* Event 3 */
		{ eigrp_fsm_event_dinc }, /* Event 4 */
		{ eigrp_fsm_event_keep_state }, /* Event 5 */
		{ eigrp_fsm_event_keep_state }, /* Event 6 */
		{ eigrp_fsm_event_keep_state }, /* Event 7 */
}, };

/*
 * Main function in which are make decisions which event occurred.
 * msg - argument of type struct eigrp_fsm_action_message contain
 * details about what happen
 *
 * Return number of occurred event (arrow in diagram).
 *
 */
int eigrp_get_fsm_event(struct eigrp_fsm_action_message *msg) {
	// Loading base information from message
	//struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	struct eigrp_neighbor_entry *entry = msg->entry;
	u_char actual_state = prefix->state;

	if (entry == NULL) {
		entry = eigrp_neighbor_entry_new();
		entry->adv_router = msg->adv_router;
		entry->ei = msg->adv_router->ei;
		entry->prefix = prefix;
		msg->entry = entry;
	}

	// Dividing by actual state of prefix's FSM
	switch (actual_state) {
	case EIGRP_FSM_STATE_PASSIVE: {
		//Calculate resultant metrics and insert to correct position in entries list
		eigrp_topology_update_distance(msg);

		struct eigrp_neighbor_entry * head =
				(struct eigrp_neighbor_entry *) entry->prefix->entries->head->data;
		//zlog_info ("flag: %d rdist: %u dist: %u pfdist: %u pdist: %u", head->flags, head->reported_distance, head->distance, prefix->fdistance, prefix->distance);
		if (head->reported_distance < prefix->fdistance) {
			return EIGRP_FSM_KEEP_STATE;
		}
		/*
		 * if best entry doesn't satisfy feasibility condition it means move to active state
		 * dependently if it was query from successor
		 */
		else {
			if (msg->packet_type == EIGRP_OPC_QUERY) {
				return EIGRP_FSM_EVENT_Q_FCN;
			} else {
				return EIGRP_FSM_EVENT_NQ_FCN;
			}
		}

		break;
	}
	case EIGRP_FSM_STATE_ACTIVE_0: {
		eigrp_topology_update_distance(msg);

		if (msg->packet_type == EIGRP_OPC_REPLY) {
			listnode_delete(prefix->rij, entry->adv_router);
			if (prefix->rij->count) {
				return EIGRP_FSM_KEEP_STATE;
			} else {
				zlog_info("All reply received\n");
				if (((struct eigrp_neighbor_entry *) prefix->entries->head->data)->reported_distance
						< prefix->fdistance) {
					return EIGRP_FSM_EVENT_LR_FCS;
				}

				return EIGRP_FSM_EVENT_LR_FCN;
			}
		} else if (msg->packet_type == EIGRP_OPC_QUERY
				&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
			return EIGRP_FSM_EVENT_QACT;
		}

		return EIGRP_FSM_KEEP_STATE;

		break;
	}
	case EIGRP_FSM_STATE_ACTIVE_1: {
		int change = eigrp_topology_update_distance(msg);

		if (msg->packet_type == EIGRP_OPC_QUERY
				&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
			return EIGRP_FSM_EVENT_QACT;
		} else if (msg->packet_type == EIGRP_OPC_REPLY) {
			listnode_delete(prefix->rij, entry->adv_router);

			if (change == 1
					&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
				return EIGRP_FSM_EVENT_DINC;
			} else if (prefix->rij->count) {
				return EIGRP_FSM_KEEP_STATE;
			} else {
				zlog_info("All reply received\n");
				return EIGRP_FSM_EVENT_LR;
			}
		} else if (msg->packet_type == EIGRP_OPC_UPDATE && change == 1
				&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
			return EIGRP_FSM_EVENT_DINC;
		}
		return EIGRP_FSM_KEEP_STATE;

		break;
	}
	case EIGRP_FSM_STATE_ACTIVE_2: {

		eigrp_topology_update_distance(msg);

		if (msg->packet_type == EIGRP_OPC_REPLY) {
			listnode_delete(prefix->rij, entry->adv_router);
			if (prefix->rij->count) {
				return EIGRP_FSM_KEEP_STATE;
			} else {
				zlog_info("All reply received\n");
				if (((struct eigrp_neighbor_entry *) prefix->entries->head->data)->reported_distance
						< prefix->fdistance) {
					return EIGRP_FSM_EVENT_LR_FCS;
				}

				return EIGRP_FSM_EVENT_LR_FCN;
			}
		}
		return EIGRP_FSM_KEEP_STATE;

		break;
	}
	case EIGRP_FSM_STATE_ACTIVE_3: {

		int change = eigrp_topology_update_distance(msg);

		if (msg->packet_type == EIGRP_OPC_REPLY) {
			listnode_delete(prefix->rij, entry->adv_router);

			if (change == 1
					&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
				return EIGRP_FSM_EVENT_DINC;
			} else if (prefix->rij->count) {
				return EIGRP_FSM_KEEP_STATE;
			} else {
				zlog_info("All reply received\n");
				return EIGRP_FSM_EVENT_LR;
			}
		} else if (msg->packet_type == EIGRP_OPC_UPDATE && change == 1
				&& (entry->flags & EIGRP_NEIGHBOR_ENTRY_SUCCESSOR_FLAG)) {
			return EIGRP_FSM_EVENT_DINC;
		}
		return EIGRP_FSM_KEEP_STATE;

		break;
	}
	}

	return EIGRP_FSM_KEEP_STATE;
}

/*
 * Function made to execute in separate thread.
 * Load argument from thread and execute proper NSM function
 */
int eigrp_fsm_event(struct eigrp_fsm_action_message *msg, int event) {

	zlog_info("EIGRP AS: %d State: %d  Event: %d Network: %s\n", msg->eigrp->AS,
			msg->prefix->state, event, eigrp_topology_ip_string(msg->prefix));
	(*(NSM[msg->prefix->state][event].func))(msg);

	return 1;
}
/*
 * Function of event 0.
 *
 */
int eigrp_fsm_event_nq_fcn(struct eigrp_fsm_action_message *msg) {
	struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	struct list *successors = eigrp_topology_get_successor(prefix);
	prefix->state = EIGRP_FSM_STATE_ACTIVE_1;
	prefix->rdistance = prefix->distance = prefix->fdistance =
			((struct eigrp_neighbor_entry *) successors->head->data)->distance;
	prefix->reported_metric =
			((struct eigrp_neighbor_entry *) successors->head->data)->total_metric;

	if (eigrp_nbr_count_get()) {
		prefix->req_action |= EIGRP_FSM_NEED_QUERY;
		listnode_add(eigrp->topology_changes_internalIPV4,prefix);
	} else {
		eigrp_fsm_event_lr(msg); //in the case that there are no more neighbors left
	}

	return 1;
}

int eigrp_fsm_event_q_fcn(struct eigrp_fsm_action_message *msg) {
	struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	struct list *successors = eigrp_topology_get_successor(prefix);
	prefix->state = EIGRP_FSM_STATE_ACTIVE_3;
	prefix->rdistance = prefix->distance = prefix->fdistance =
			((struct eigrp_neighbor_entry *) successors->head->data)->distance;
	prefix->reported_metric =
			((struct eigrp_neighbor_entry *) successors->head->data)->total_metric;
	if (eigrp_nbr_count_get()) {
			prefix->req_action |= EIGRP_FSM_NEED_QUERY;
			listnode_add(eigrp->topology_changes_internalIPV4,prefix);
		} else {
			eigrp_fsm_event_lr(msg); //in the case that there are no more neighbors left
		}

	return 1;
}

int eigrp_fsm_event_keep_state(struct eigrp_fsm_action_message *msg) {

	struct eigrp_prefix_entry *prefix = msg->prefix;
	struct eigrp_neighbor_entry *entry = msg->entry;

	if (prefix->state == EIGRP_FSM_STATE_PASSIVE) {
		if (!eigrp_metrics_is_same(&prefix->reported_metric,
				&((struct eigrp_neighbor_entry *) prefix->entries->head->data)->total_metric)) {
			prefix->rdistance =
					prefix->fdistance =
							prefix->distance =
									((struct eigrp_neighbor_entry *) prefix->entries->head->data)->distance;
			prefix->reported_metric =
					((struct eigrp_neighbor_entry *) prefix->entries->head->data)->total_metric;
			if (msg->packet_type == EIGRP_OPC_QUERY)
				eigrp_send_reply(msg->adv_router, msg->entry);
			prefix->req_action |= EIGRP_FSM_NEED_UPDATE;
			listnode_add((eigrp_lookup())->topology_changes_internalIPV4,prefix);
//			eigrp_update_send_all(msg->eigrp, prefix, entry->adv_router->ei);
		}
		eigrp_topology_update_node_flags(prefix);
		eigrp_update_routing_table(prefix);
	}

	if (msg->packet_type == EIGRP_OPC_QUERY)
		eigrp_send_reply(msg->adv_router, prefix);

	return 1;
}

int eigrp_fsm_event_lr(struct eigrp_fsm_action_message *msg) {
	struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	prefix->fdistance =
			prefix->distance =
					prefix->rdistance =
							((struct eigrp_neighbor_entry *) (prefix->entries->head->data))->distance;
	prefix->reported_metric =
			((struct eigrp_neighbor_entry *) (prefix->entries->head->data))->total_metric;
	if (prefix->state == EIGRP_FSM_STATE_ACTIVE_3)
		eigrp_send_reply(
				((struct eigrp_neighbor_entry *) (eigrp_topology_get_successor(
						prefix)->head->data))->adv_router, prefix);
	prefix->state = EIGRP_FSM_STATE_PASSIVE;
	prefix->req_action |= EIGRP_FSM_NEED_UPDATE;
	listnode_add(eigrp->topology_changes_internalIPV4,prefix);
//  eigrp_update_send_all(eigrp, msg->prefix, msg->adv_router->ei);
	eigrp_topology_update_node_flags(prefix);
	eigrp_update_routing_table(prefix);
	eigrp_update_topology_table_prefix(eigrp->topology_table, prefix);

	return 1;
}

int eigrp_fsm_event_dinc(struct eigrp_fsm_action_message *msg) {

	msg->prefix->state =
			msg->prefix->state == EIGRP_FSM_STATE_ACTIVE_1 ?
					EIGRP_FSM_STATE_ACTIVE_0 : EIGRP_FSM_STATE_ACTIVE_2;
	msg->prefix->distance =
			((struct eigrp_neighbor_entry *) (eigrp_topology_get_successor(
					msg->prefix)->head->data))->distance;
	if (!msg->prefix->rij->count) {
		(*(NSM[msg->prefix->state][eigrp_get_fsm_event(msg)].func))(msg);
	}

	return 1;
}

int eigrp_fsm_event_lr_fcs(struct eigrp_fsm_action_message *msg) {
	struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	prefix->state = EIGRP_FSM_STATE_PASSIVE;
	prefix->distance =
			prefix->rdistance =
					((struct eigrp_neighbor_entry *) (prefix->entries->head->data))->distance;
	prefix->reported_metric =
			((struct eigrp_neighbor_entry *) (prefix->entries->head->data))->total_metric;
	prefix->fdistance =
			prefix->fdistance > prefix->distance ?
					prefix->distance : prefix->fdistance;
	if (prefix->state == EIGRP_FSM_STATE_ACTIVE_2)
		eigrp_send_reply(
				((struct eigrp_neighbor_entry *) (eigrp_topology_get_successor(
						prefix)->head->data))->adv_router, prefix);
	prefix->req_action |= EIGRP_FSM_NEED_UPDATE;
	listnode_add(eigrp->topology_changes_internalIPV4,prefix);
//  eigrp_update_send_all(eigrp, prefix, msg->adv_router->ei);
	eigrp_topology_update_node_flags(prefix);
	eigrp_update_routing_table(prefix);
	eigrp_update_topology_table_prefix(eigrp->topology_table, prefix);

	return 1;
}

int eigrp_fsm_event_lr_fcn(struct eigrp_fsm_action_message *msg) {
	struct eigrp *eigrp = msg->eigrp;
	struct eigrp_prefix_entry *prefix = msg->prefix;
	prefix->state =
			prefix->state == EIGRP_FSM_STATE_ACTIVE_0 ?
					EIGRP_FSM_STATE_ACTIVE_1 : EIGRP_FSM_STATE_ACTIVE_3;
	struct eigrp_neighbor_entry *best_successor =
			((struct eigrp_neighbor_entry *) (eigrp_topology_get_successor(
					prefix)->head->data));
	prefix->rdistance = prefix->distance = best_successor->distance;
	prefix->reported_metric = best_successor->total_metric;
	if (eigrp_nbr_count_get()) {
		prefix->req_action |= EIGRP_FSM_NEED_QUERY;
		listnode_add(eigrp->topology_changes_internalIPV4,prefix);
	} else {
		eigrp_fsm_event_lr(msg); //in the case that there are no more neighbors left
	}

	return 1;
}

int eigrp_fsm_event_qact(struct eigrp_fsm_action_message *msg) {
	msg->prefix->state = EIGRP_FSM_STATE_ACTIVE_2;
	msg->prefix->distance =
			((struct eigrp_neighbor_entry *) (eigrp_topology_get_successor(
					msg->prefix)->head->data))->distance;
	return 1;
}
