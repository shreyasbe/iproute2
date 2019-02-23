// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * res-cq.c	RDMA tool
 * Authors:     Leon Romanovsky <leonro@mellanox.com>
 */

#include "res.h"
#include <inttypes.h>

static const char *poll_ctx_to_str(uint8_t idx)
{
	static const char * const cm_id_states_str[] = {
		"DIRECT", "SOFTIRQ", "WORKQUEUE", "UNBOUND_WORKQUEUE"};

	if (idx < ARRAY_SIZE(cm_id_states_str))
		return cm_id_states_str[idx];
	return "UNKNOWN";
}

static void print_poll_ctx(struct rd *rd, uint8_t poll_ctx, struct nlattr *attr)
{
	if (!attr)
		return;

	if (rd->json_output) {
		jsonw_string_field(rd->jw, "poll-ctx",
				   poll_ctx_to_str(poll_ctx));
		return;
	}
	pr_out("poll-ctx %s ", poll_ctx_to_str(poll_ctx));
}

static int res_cq_line(struct rd *rd, const char *name, int idx,
		       struct nlattr *nla_entry)
{
	struct nlattr *nla_line[RDMA_NLDEV_ATTR_MAX] = {};
	char *comm = NULL;
	uint32_t pid = 0;
	uint8_t poll_ctx = 0;
	uint32_t ctxn = 0;
	uint32_t cqn = 0;
	uint64_t users;
	uint32_t cqe;
	int err;

	err = mnl_attr_parse_nested(nla_entry, rd_attr_cb, nla_line);
	if (err != MNL_CB_OK)
		return MNL_CB_ERROR;

	if (!nla_line[RDMA_NLDEV_ATTR_RES_CQE] ||
	    !nla_line[RDMA_NLDEV_ATTR_RES_USECNT] ||
	    (!nla_line[RDMA_NLDEV_ATTR_RES_PID] &&
	     !nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME])) {
		return MNL_CB_ERROR;
	}

	cqe = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_CQE]);

	users = mnl_attr_get_u64(nla_line[RDMA_NLDEV_ATTR_RES_USECNT]);
	if (rd_check_is_filtered(rd, "users", users))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_POLL_CTX]) {
		poll_ctx =
			mnl_attr_get_u8(nla_line[RDMA_NLDEV_ATTR_RES_POLL_CTX]);
		if (rd_check_is_string_filtered(rd, "poll-ctx",
						poll_ctx_to_str(poll_ctx)))
			goto out;
	}

	if (nla_line[RDMA_NLDEV_ATTR_RES_PID]) {
		pid = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_PID]);
		comm = get_task_name(pid);
	}

	if (rd_check_is_filtered(rd, "pid", pid))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_CQN])
		cqn = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_CQN]);
	if (rd_check_is_filtered(rd, "cqn", cqn))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_CTXN])
		ctxn = mnl_attr_get_u32(nla_line[RDMA_NLDEV_ATTR_RES_CTXN]);
	if (rd_check_is_filtered(rd, "ctxn", ctxn))
		goto out;

	if (nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME])
		/* discard const from mnl_attr_get_str */
		comm = (char *)mnl_attr_get_str(
			nla_line[RDMA_NLDEV_ATTR_RES_KERN_NAME]);

	if (rd->json_output)
		jsonw_start_array(rd->jw);

	print_dev(rd, idx, name);
	res_print_uint(rd, "cqn", cqn, nla_line[RDMA_NLDEV_ATTR_RES_CQN]);
	res_print_uint(rd, "cqe", cqe, nla_line[RDMA_NLDEV_ATTR_RES_CQE]);
	res_print_uint(rd, "users", users,
		       nla_line[RDMA_NLDEV_ATTR_RES_USECNT]);
	print_poll_ctx(rd, poll_ctx, nla_line[RDMA_NLDEV_ATTR_RES_POLL_CTX]);
	res_print_uint(rd, "ctxn", ctxn, nla_line[RDMA_NLDEV_ATTR_RES_CTXN]);
	res_print_uint(rd, "pid", pid, nla_line[RDMA_NLDEV_ATTR_RES_PID]);
	print_comm(rd, comm, nla_line);

	print_driver_table(rd, nla_line[RDMA_NLDEV_ATTR_DRIVER]);
	newline(rd);

out:	if (nla_line[RDMA_NLDEV_ATTR_RES_PID])
		free(comm);
	return MNL_CB_OK;
}

int res_cq_parse_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nlattr *tb[RDMA_NLDEV_ATTR_MAX] = {};
	struct nlattr *nla_table, *nla_entry;
	struct rd *rd = data;
	int ret = MNL_CB_OK;
	const char *name;
	uint32_t idx;

	mnl_attr_parse(nlh, 0, rd_attr_cb, tb);
	if (!tb[RDMA_NLDEV_ATTR_DEV_INDEX] || !tb[RDMA_NLDEV_ATTR_DEV_NAME] ||
	    !tb[RDMA_NLDEV_ATTR_RES_CQ])
		return MNL_CB_ERROR;

	name = mnl_attr_get_str(tb[RDMA_NLDEV_ATTR_DEV_NAME]);
	idx = mnl_attr_get_u32(tb[RDMA_NLDEV_ATTR_DEV_INDEX]);
	nla_table = tb[RDMA_NLDEV_ATTR_RES_CQ];

	mnl_attr_for_each_nested(nla_entry, nla_table) {
		ret = res_cq_line(rd, name, idx, nla_entry);

		if (ret != MNL_CB_OK)
			break;
	}
	return ret;
}
