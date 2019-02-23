// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * res-cmid.c	RDMA tool
 * Authors:     Leon Romanovsky <leonro@mellanox.com>
 */

#include "res.h"
#include <inttypes.h>

static void print_qp_type(struct rd *rd, uint32_t val)
{
	if (rd->json_output)
		jsonw_string_field(rd->jw, "qp-type", qp_types_to_str(val));
	else
		pr_out("qp-type %s ", qp_types_to_str(val));
}

static const char *cm_id_state_to_str(uint8_t idx)
{
	static const char *const cm_id_states_str[] = {
		"IDLE",		  "ADDR_QUERY",     "ADDR_RESOLVED",
		"ROUTE_QUERY",    "ROUTE_RESOLVED", "CONNECT",
		"DISCONNECT",     "ADDR_BOUND",     "LISTEN",
		"DEVICE_REMOVAL", "DESTROYING"
	};

	if (idx < ARRAY_SIZE(cm_id_states_str))
		return cm_id_states_str[idx];
	return "UNKNOWN";
}

static const char *cm_id_ps_to_str(uint32_t ps)
{
	switch (ps) {
	case RDMA_PS_IPOIB:
		return "IPoIB";
	case RDMA_PS_IB:
		return "IPoIB";
	case RDMA_PS_TCP:
		return "TCP";
	case RDMA_PS_UDP:
		return "UDP";
	default:
		return "---";
	}
}

static void print_cm_id_state(struct rd *rd, uint8_t state)
{
	if (rd->json_output) {
		jsonw_string_field(rd->jw, "state", cm_id_state_to_str(state));
		return;
	}
	pr_out("state %s ", cm_id_state_to_str(state));
}

static void print_ps(struct rd *rd, uint32_t ps)
{
	if (rd->json_output) {
		jsonw_string_field(rd->jw, "ps", cm_id_ps_to_str(ps));
		return;
	}
	pr_out("ps %s ", cm_id_ps_to_str(ps));
}

static void print_ipaddr(struct rd *rd, const char *key, char *addrstr,
			 uint16_t port)
{
	if (rd->json_output) {
		int name_size = INET6_ADDRSTRLEN + strlen(":65535");
		char json_name[name_size];

		snprintf(json_name, name_size, "%s:%u", addrstr, port);
		jsonw_string_field(rd->jw, key, json_name);
		return;
	}
	pr_out("%s %s:%u ", key, addrstr, port);
}

static int ss_ntop(struct nlattr *nla_line, char *addr_str, uint16_t *port)
{
	struct __kernel_sockaddr_storage *addr;

	addr = (struct __kernel_sockaddr_storage *)mnl_attr_get_payload(
		nla_line);
	switch (addr->ss_family) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;

		if (!inet_ntop(AF_INET, (const void *)&sin->sin_addr, addr_str,
			       INET6_ADDRSTRLEN))
			return -EINVAL;
		*port = ntohs(sin->sin_port);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;

		if (!inet_ntop(AF_INET6, (const void *)&sin6->sin6_addr,
			       addr_str, INET6_ADDRSTRLEN))
			return -EINVAL;
		*port = ntohs(sin6->sin6_port);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int res_cm_id_line(struct rd *rd, const char *name, int idx,
			  struct nlattr *nla_entry)
{
	struct nlattr *nla_line[RDMA_NLDEV_ATTR_MAX] = {};
	char src_addr_str[INET6_ADDRSTRLEN];
	char dst_addr_str[INET6_ADDRSTRLEN];
	uint16_t src_port, dst_port;
	uint32_t port = 0, pid = 0;
	uint8_t type = 0, state;
	uint32_t lqpn = 0, ps;
	uint32_t cm_idn = 0;
	char *comm = NULL;
	int err;

	err = mnl_attr_parse_nested(nla_entry, rd_attr_cb, nla_line);
	if (err != MNL_CB_OK)
		return MNL_CB_ERROR;

	if (!nla_line[RDMA_NLDEV_ATTR_RES_STATE] ||
	    !nla_line[RDMA_NLDEV_ATTR_RES_PS] ||
	    (!nla_line[RDMA_NLDEV_ATTR_RES_PID] &&
	     !nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME])) {
		return MNL_CB_ERROR;
	}

	if (nla_line[RDMA_NLDEV_ATTR_PORT_INDEX])
		port = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_PORT_INDEX]);

	if (port && port != rd->port_idx)
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_LQPN]) {
		lqpn = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_LQPN]);
		if (rd_check_is_filtered(rd, "lqpn", lqpn))
			goto out;
	}
	if (nla_line[RDMA_NLDEV_ATTR_RES_TYPE]) {
		type = mnl_attr_get_u8(nla_line[RDMA_NLDEV_ATTR_RES_TYPE]);
		if (rd_check_is_string_filtered(rd, "qp-type",
						qp_types_to_str(type)))
			goto out;
	}

	ps = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_PS]);
	if (rd_check_is_string_filtered(rd, "ps", cm_id_ps_to_str(ps)))
		goto out;

	state = mnl_attr_get_u8(nla_line[RDMA_NLDEV_ATTR_RES_STATE]);
	if (rd_check_is_string_filtered(rd, "state", cm_id_state_to_str(state)))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_SRC_ADDR]) {
		if (ss_ntop(nla_line[RDMA_NLDEV_ATTR_RES_SRC_ADDR],
			    src_addr_str, &src_port))
			goto out;
		if (rd_check_is_string_filtered(rd, "src-addr", src_addr_str))
			goto out;
		if (rd_check_is_filtered(rd, "src-port", src_port))
			goto out;
	}

	if (nla_line[RDMA_NLDEV_ATTR_RES_DST_ADDR]) {
		if (ss_ntop(nla_line[RDMA_NLDEV_ATTR_RES_DST_ADDR],
			    dst_addr_str, &dst_port))
			goto out;
		if (rd_check_is_string_filtered(rd, "dst-addr", dst_addr_str))
			goto out;
		if (rd_check_is_filtered(rd, "dst-port", dst_port))
			goto out;
	}

	if (nla_line[RDMA_NLDEV_ATTR_RES_PID]) {
		pid = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_PID]);
		comm = get_task_name(pid);
	}

	if (rd_check_is_filtered(rd, "pid", pid))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_CM_IDN])
		cm_idn = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_CM_IDN]);
	if (rd_check_is_filtered(rd, "cm-idn", cm_idn))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME]) {
		/* discard const from mnl_attr_get_str */
		comm = (char *)mnl_attr_get_str(
			nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME]);
	}

	if (rd->json_output)
		jsonw_start_array(rd->jw);

	print_link(rd, idx, name, port, nla_line);
	res_print_uint(rd, "cm-idn", cm_idn,
		       nla_line[RDMA_NLDEV_ATTR_RES_CM_IDN]);
	res_print_uint(rd, "lqpn", lqpn, nla_line[RDMA_NLDEV_ATTR_RES_LQPN]);
	if (nla_line[RDMA_NLDEV_ATTR_RES_TYPE])
		print_qp_type(rd, type);
	print_cm_id_state(rd, state);
	print_ps(rd, ps);
	res_print_uint(rd, "pid", pid, nla_line[RDMA_NLDEV_ATTR_RES_PID]);
	print_comm(rd, comm, nla_line);

	if (nla_line[RDMA_NLDEV_ATTR_RES_SRC_ADDR])
		print_ipaddr(rd, "src-addr", src_addr_str, src_port);
	if (nla_line[RDMA_NLDEV_ATTR_RES_DST_ADDR])
		print_ipaddr(rd, "dst-addr", dst_addr_str, dst_port);

	print_driver_table(rd, nla_line[RDMA_NLDEV_ATTR_DRIVER]);
	newline(rd);

out:	if (nla_line[RDMA_NLDEV_ATTR_RES_PID])
		free(comm);
	return MNL_CB_OK;
}

int res_cm_id_parse_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX] = {};
	struct nlattr *nla_table, *nla_entry;
	struct rd *rd = data;
	int ret = MNL_CB_OK;
	const char *name;
	int idx;

	mnl_attr_parse(nlh, 0, rd_attr_cb, tb);
	if (!tb[RDMA_NLDEV_ATTR_DEV_INDEX] || !tb[RDMA_NLDEV_ATTR_DEV_NAME] ||
	    !tb[RDMA_NLDEV_ATTR_RES_CM_ID])
		return MNL_CB_ERROR;

	name = mnl_attr_get_str(tb[RDMA_NLDEV_ATTR_DEV_NAME]);
	idx = mnl_attr_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	nla_table = tb[RDMA_NLDEV_ATTR_RES_CM_ID];

	mnl_attr_for_each_nested(nla_entry, nla_table) {
		ret = res_cm_id_line(rd, name, idx, nla_entry);

		if (ret != MNL_CB_OK)
			break;
	}
	return ret;
}
