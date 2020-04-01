/*
 * Copyright (C) 2020-2021 Seagate
 * Copyright (C) 2020-2021 Linaro Ltd
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <uuid/uuid.h>
#include <unistd.h>

#include "client.h"
#include "cmd.h"
#include "list.h"
#include "lockspace.h"
#include "lock.h"
#include "log.h"

static int ilm_lock_payload_read(struct ilm_cmd *cmd,
				 struct ilm_lock_payload *payload)
{
	int ret;

	ret = recv(cmd->cl->fd, payload,
		   sizeof(struct ilm_lock_payload), MSG_WAITALL);
	if (!ret || (ret != sizeof(struct ilm_lock_payload))) {
		ilm_log_err("Client fd %d recv lock payload errno %d\n",
			    cmd->cl->fd, errno);
		return -EIO;
	}

	if (payload->magic != ILM_LOCK_MAGIC) {
	        ilm_log_err("Client fd %d ret %d magic %x vs %x\n",
			    cmd->cl->fd, ret, payload->magic, ILM_LOCK_MAGIC);
		return -EINVAL;
	}

	return 0;
}

static struct ilm_lock *ilm_alloc(struct ilm_cmd *cmd,
				  struct ilm_lockspace *ls,
				  int drive_num)
{
	char path[PATH_MAX];
	struct ilm_lock *lock;
	int ret, i, copied;

	lock = malloc(sizeof(struct ilm_lock));
	if (!lock) {
	        ilm_log_err("No spare memory to allocate lock\n");
		return NULL;
	}
	memset(lock, 0, sizeof(struct ilm_lock));

	INIT_LIST_HEAD(&lock->list);

	for (i = 0, copied = 0; i < drive_num; i++, copied++) {
		ret = recv(cmd->cl->fd, &path, sizeof(path), MSG_WAITALL);
		if (ret <= 0) {
	        	ilm_log_err("Failed to read out drive path\n");
			goto drive_fail;
		}

		lock->drive_list[i] = strdup(path);
		if (!lock->drive_list[i]) {
	        	ilm_log_err("Failed to copy drive path\n");
			goto drive_fail;
		}
	}

	ret = ilm_lockspace_add_lock(ls, lock);
	if (ret < 0)
		goto drive_fail;

	return lock;

drive_fail:
	for (i = 0; i < copied; i++)
		free(lock->drive_list[i]);
	free(lock);
	return NULL;
}

static int ilm_free(struct ilm_lockspace *ls, struct ilm_lock *lock)
{
	int ret;

	ret = ilm_lockspace_del_lock(ls, lock);
	if (ret < 0)
		return ret;

	free(lock);
	return 0;
}

int ilm_lock_acquire(struct ilm_cmd *cmd, struct ilm_lockspace *ls)
{
	struct ilm_lock_payload payload;
	struct ilm_lock *lock;
	int ret;

	ret = ilm_lock_payload_read(cmd, &payload);
	if (ret < 0)
		goto fail;

	if (payload.drive_num > ILM_DRIVE_MAX_NUM) {
	        ilm_log_err("Drive list is out of scope: drive_num %d\n",
			    payload.drive_num);
		ret = -EINVAL;
		goto fail;
	}

	lock = ilm_alloc(cmd, ls, payload.drive_num);
	if (!lock) {
		ret = -ENOMEM;
		goto fail;
	}

	memcpy(lock->lock_id, payload.lock_id, ILM_ID_LENGTH);

	/* TODO: add majority locking algorithm */

	ilm_send_result(cmd->cl->fd, 0, NULL, 0);
	return 0;

fail:
	ilm_send_result(cmd->cl->fd, ret, NULL, 0);
	return ret;
}

int ilm_lock_release(struct ilm_cmd *cmd, struct ilm_lockspace *ls)
{
	struct ilm_lock_payload payload;
	struct ilm_lock *lock;
	int ret;

	ret = ilm_lock_payload_read(cmd, &payload);
	if (ret < 0)
		goto fail;

	ret = ilm_lockspace_find_lock(ls, payload.lock_id, &lock);
	if (ret < 0)
		goto fail;

	/* TODO: add majority locking algorithm */

	ret = ilm_free(ls, lock);
	if (ret < 0)
		goto fail;

	ilm_send_result(cmd->cl->fd, 0, NULL, 0);
	return 0;

fail:
	ilm_send_result(cmd->cl->fd, ret, NULL, 0);
	return ret;
}

int ilm_lock_convert_mode(struct ilm_cmd *cmd, struct ilm_lockspace *ls)
{
	struct ilm_lock_payload payload;
	struct ilm_lock *lock;
	int ret;

	ret = ilm_lock_payload_read(cmd, &payload);
	if (ret < 0)
		goto fail;

	ret = ilm_lockspace_find_lock(ls, payload.lock_id, &lock);
	if (ret < 0)
		goto fail;

	/* TODO: add majority locking algorithm */

	ilm_send_result(cmd->cl->fd, 0, NULL, 0);
	return 0;

fail:
	ilm_send_result(cmd->cl->fd, ret, NULL, 0);
	return ret;
}

int ilm_lock_vb_write(struct ilm_cmd *cmd, struct ilm_lockspace *ls)
{
	struct ilm_lock_payload payload;
	struct ilm_lock *lock;
	char buf[ILM_LVB_SIZE];
	int ret;

	ret = ilm_lock_payload_read(cmd, &payload);
	if (ret < 0)
		goto fail;

	ret = ilm_lockspace_find_lock(ls, payload.lock_id, &lock);
	if (ret < 0)
		goto fail;

	ret = recv(cmd->cl->fd, buf, ILM_LVB_SIZE, MSG_WAITALL);
	if (ret != ILM_LVB_SIZE) {
		ilm_log_err("Fail to receive LVB fd %d errno %d\n",
			    cmd->cl->fd, errno);
		ret = -EIO;
		goto fail;
	}

	/* TODO: add majority locking algorithm */

	ilm_send_result(cmd->cl->fd, 0, NULL, 0);
	return 0;

fail:
	ilm_send_result(cmd->cl->fd, ret, NULL, 0);
	return ret;
}

int ilm_lock_vb_read(struct ilm_cmd *cmd, struct ilm_lockspace *ls)
{
	struct ilm_lock_payload payload;
	struct ilm_lock *lock;
	char buf[ILM_LVB_SIZE];
	int ret;

	ret = ilm_lock_payload_read(cmd, &payload);
	if (ret < 0)
		goto fail;

	ret = ilm_lockspace_find_lock(ls, payload.lock_id, &lock);
	if (ret < 0)
		goto fail;

	/* TODO: add majority locking algorithm */

	ilm_send_result(cmd->cl->fd, 0, buf, ILM_LVB_SIZE);
	return 0;

fail:
	ilm_send_result(cmd->cl->fd, ret, NULL, 0);
	return ret;
}
