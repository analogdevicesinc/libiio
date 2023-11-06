// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iiod-responder.h"

#include <errno.h>
#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-lock.h>
#include <string.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#endif

#define NB_BUFS_MAX 2

struct iiod_client_data {
	/*
	 * Structure for the command to send.
	 * When reading response, return code will be stored in the code field.
	 */
	struct iiod_command cmd;

	/* User-provided buffer where the data will be read or written */
	struct iiod_buf buf[NB_BUFS_MAX];
	size_t nb_buf;

	/* Value representing the time at which the command was sent. */
	uint64_t start_time;
};

struct iiod_io {
	struct iiod_io *r_next;
	uint16_t client_id;

	struct iiod_responder *responder;

	/* Cond to sleep until I/O is done */
	struct iio_cond *cond;
	struct iio_mutex *lock;

	/* Set to true when the response has been read */
	bool r_done;

	/* Reference counter */
	unsigned int refcnt;

	/* I/O data */
	struct iiod_client_data w_io, r_io;

	/* Timeout (in milli-seconds) for I/O operations */
	unsigned int timeout_ms;

	struct iio_task_token *write_token;
};

struct iiod_responder {
	const struct iiod_responder_ops *ops;
	void *d;

	struct iiod_io *readers, *writers, *default_io;
	uint16_t next_client_id;

	struct iio_mutex *lock;
	struct iio_thrd *read_thrd;
	struct iio_task *write_task;
	struct iio_thrd *write_thrd;

	bool thrd_stop;
	int thrd_err_code;
	unsigned int timeout_ms;
};

static uint64_t read_counter_us(void)
{
	uint64_t value;

#ifdef _WIN32
	LARGE_INTEGER freq, cnt;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&cnt);

	value = (1000000 * cnt.QuadPart) / freq.QuadPart;
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);

	value = tv.tv_sec * 1000000ull + tv.tv_usec;
#endif

	return value;
}

uint64_t iiod_responder_read_counter_us(void)
{
	return read_counter_us();
}

static void __iiod_io_cancel_unlocked(struct iiod_io *io)
{
	struct iiod_responder *priv = io->responder;
	struct iiod_io *tmp;

	/* Discard the entry from the readers list */
	if (io == priv->readers) {
		priv->readers = io->r_next;
	} else if (priv->readers) {
		for (tmp = priv->readers; tmp->r_next; tmp = tmp->r_next) {
			if (tmp->r_next == io) {
				tmp->r_next = io->r_next;
				break;
			}
		}
	}
}

static ssize_t iiod_rw_all(struct iiod_responder *priv,
			   const struct iiod_buf *cmd_buf,
			   const struct iiod_buf *buf, size_t nb,
			   size_t bytes, bool is_read)
{
	ssize_t ret, count = 0;
	struct iiod_buf bufs[32], *curr = &bufs[0];

	if (cmd_buf)
		nb++;

	if (nb == 0 || nb > ARRAY_SIZE(bufs))
		return EINVAL;

	if (cmd_buf) {
		bufs[0] = *cmd_buf;
		if (buf)
			memcpy(&bufs[1], buf, (nb - 1) * sizeof(*buf));
	} else {
		memcpy(bufs, buf, nb * sizeof(*buf));
	}

	while (true) {
		if (is_read && bytes - count <= curr->size) {
			curr->size = bytes - count;
			nb = 1;
		}

		if (is_read)
			ret = priv->ops->read(priv->d, curr, nb);
		else
			ret = priv->ops->write(priv->d, curr, nb);
		if (ret <= 0)
			return ret;

		while (ret && (size_t) ret >= curr->size) {
			ret -= curr->size;
			count += curr->size;
			nb--;
			curr++;
		}

		if (!ret && !nb)
			break;

		count += ret;
		curr->ptr = (char *) curr->ptr + ret;
		curr->size -= ret;
	}

	return count;
}

static int iiod_discard_data(struct iiod_responder *priv, size_t bytes)
{
	ssize_t ret;

	while (bytes) {
		ret = priv->ops->discard(priv->d, bytes);
		if (ret < 0)
			return (int) ret;

		bytes -= (size_t) ret;
	}

	return 0;
}

int iiod_command_data_read(struct iiod_command_data *data,
			   const struct iiod_buf *buf)
{
	struct iiod_responder *priv = (struct iiod_responder *) data;
	ssize_t ret;

	ret = iiod_rw_all(priv, NULL, buf, 1, buf->size, true);
	if (ret < 0)
		return (int) ret;
	if (ret != buf->size)
		return -EIO;

	return 0;
}

static ssize_t iiod_run_command(struct iiod_responder *priv,
				struct iiod_command *cmd)
{
	return priv->ops->cmd(cmd, (struct iiod_command_data *) priv, priv->d);
}

static void iiod_responder_signal_io(struct iiod_io *io, int32_t code)
{
	struct iiod_responder *priv = io->responder;

	io->r_io.cmd.code = code;

	/* Wake up the reader */
	iio_mutex_lock(io->lock);
	io->r_done = true;
	iio_cond_signal(io->cond);
	iio_mutex_unlock(io->lock);
}

static void iiod_responder_cancel_responses(struct iiod_responder *priv)
{
	struct iiod_io *io, *next;

	/* Discard the entry from the readers list */
	for (io = priv->readers; io; io = next) {
		next = io->r_next;
		iiod_responder_signal_io(io, priv->thrd_err_code);
	}
}

static int iiod_responder_reader_thrd(void *d)
{
	struct iiod_responder *priv = d;
	struct iiod_command cmd;
	struct iiod_buf cmd_buf;
	struct iiod_io *io;
	ssize_t ret = 0;

	cmd_buf.ptr = &cmd;
	cmd_buf.size = sizeof(cmd);

	iio_mutex_lock(priv->lock);

	while (!priv->thrd_stop) {
		iio_mutex_unlock(priv->lock);

		ret = iiod_rw_all(priv, NULL, &cmd_buf, 1, sizeof(cmd), true);

		iio_mutex_lock(priv->lock);
		if (ret <= 0)
			break;

		if (cmd.op != IIOD_OP_RESPONSE) {
			iio_mutex_unlock(priv->lock);

			ret = iiod_run_command(priv, &cmd);

			iio_mutex_lock(priv->lock);
			if (ret < 0)
				break;

			continue;
		}

		/* Find the client for the given ID in the readers list */
		for (io = priv->readers; io; io = io->r_next) {
			if (io->client_id == cmd.client_id)
				break;
		}

		if (!io) {
			/* We received a response, but have no client waiting
			 * for it, so drop it. */
			iio_mutex_unlock(priv->lock);
			iiod_discard_data(priv, cmd.code);
			iio_mutex_lock(priv->lock);
			continue;
		}

		/* Discard the entry from the readers list */
		__iiod_io_cancel_unlocked(io);

		iio_mutex_unlock(priv->lock);

		if (io->r_io.nb_buf && cmd.code > 0) {
			ret = iiod_rw_all(priv, NULL, io->r_io.buf,
					  io->r_io.nb_buf, cmd.code, true);

			if (ret > 0 && (size_t) ret < (size_t) cmd.code)
				iiod_discard_data(priv, cmd.code - ret);

			iio_mutex_lock(priv->lock);

			if (ret <= 0) {
				iiod_responder_signal_io(io, (int32_t) ret);
				break;
			}
		} else {
			iio_mutex_lock(priv->lock);
		}

		/* Wake up the reader */
		iiod_responder_signal_io(io, cmd.code);
	}

	priv->thrd_err_code = priv->thrd_stop ? -EINTR : (int) ret;
	priv->thrd_stop = true;

	iiod_responder_cancel_responses(priv);
	iio_task_stop(priv->write_task);
	iio_task_flush(priv->write_task);

	iio_mutex_unlock(priv->lock);

	return (int) ret;
}

static int iiod_responder_write(void *p, void *elm)
{
	struct iiod_responder *priv = p;
	struct iiod_io *writer = elm;
	struct iiod_command cmd;
	struct iiod_buf cmd_buf;
	ssize_t ret;

	cmd_buf.ptr = &writer->w_io.cmd;
	cmd_buf.size = sizeof(cmd);

	ret = iiod_rw_all(priv, &cmd_buf, writer->w_io.buf,
			  writer->w_io.nb_buf, 0, false);
	writer->w_io.cmd.code = (int32_t) ret;

	return 0;
}

static int iiod_enqueue_command(struct iiod_io *writer, uint8_t op,
				uint8_t dev, int32_t code,
				const struct iiod_buf *buf, size_t nb)
{
	struct iiod_responder *priv = writer->responder;

	if (nb > NB_BUFS_MAX)
		return -EINVAL;

	writer->w_io.start_time = read_counter_us();
	writer->w_io.cmd.op = op;
	writer->w_io.cmd.dev = dev;
	writer->w_io.cmd.client_id = writer->client_id;
	writer->w_io.cmd.code = code;
	if (nb)
		memcpy(writer->w_io.buf, buf, sizeof(*buf) * nb);
	writer->w_io.nb_buf = nb;

	if (writer->write_token)
	      return -EIO;

	iio_mutex_lock(priv->lock);
	if (priv->thrd_stop) {
		iio_mutex_unlock(priv->lock);
		return priv->thrd_err_code;
	}

	writer->write_token = iio_task_enqueue(priv->write_task, writer);
	iio_mutex_unlock(priv->lock);

	return iio_err(writer->write_token);
}

bool iiod_io_command_is_done(struct iiod_io *io)
{
	uint64_t timeout_us;
	bool done;

	iio_mutex_lock(io->lock);

	done = io->write_token && iio_task_is_done(io->write_token);

	if (!done && io->timeout_ms) {
		timeout_us = io->timeout_ms * 1000;

		done = read_counter_us() - io->w_io.start_time > timeout_us;
	}

	iio_mutex_unlock(io->lock);

	return done;
}

int iiod_io_wait_for_command_done(struct iiod_io *io)
{
	uint64_t diff_ms = 0, timeout_ms = io->timeout_ms;
	struct iio_task_token *token;

	iio_mutex_lock(io->lock);
	token = io->write_token;
	io->write_token = NULL;
	iio_mutex_unlock(io->lock);

	if (!token)
		return 0;

	if (timeout_ms) {
		diff_ms = (read_counter_us() - io->w_io.start_time) / 1000;

		if (diff_ms >= timeout_ms)
			iio_task_cancel(token);
	}

	return iio_task_sync(token, (unsigned int)(timeout_ms - diff_ms));
}

bool iiod_io_has_response(struct iiod_io *io)
{
	uint64_t timeout_us = io->timeout_ms * 1000;

	if (io->r_done)
		return true;

	if (!io->timeout_ms)
		return false;

	timeout_us = io->timeout_ms * 1000;

	return read_counter_us() - io->w_io.start_time > timeout_us;
}

static int iiod_io_cond_wait(const struct iiod_io *io)
{
	uint64_t diff_ms, timeout_ms = io->timeout_ms;

	if (!timeout_ms)
		return iio_cond_wait(io->cond, io->lock, 0);

	diff_ms = (read_counter_us() - io->r_io.start_time) / 1000;

	if (diff_ms < timeout_ms)
		return iio_cond_wait(io->cond, io->lock, timeout_ms - diff_ms);

	return -ETIMEDOUT;
}

intptr_t iiod_io_wait_for_response(struct iiod_io *io)
{
	struct iiod_responder *priv = io->responder;
	int ret = 0;

	iio_mutex_lock(io->lock);

	while (!io->r_done) {
		ret = iiod_io_cond_wait(io);
		if (ret) {
			__iiod_io_cancel_unlocked(io);
			io->r_io.cmd.code = ret;
			io->r_done = true;
			break;
		}
	}

	iio_mutex_unlock(io->lock);

	return io->r_io.cmd.code;
}

void iiod_io_cancel_response(struct iiod_io *io)
{
	iiod_responder_signal_io(io, -EINTR);
}

int iiod_io_send_command_async(struct iiod_io *io,
			       const struct iiod_command *cmd,
			       const struct iiod_buf *buf, size_t nb)
{
	return iiod_enqueue_command(io, cmd->op, cmd->dev,
				    cmd->code, buf, nb);
}

int iiod_io_send_command(struct iiod_io *io,
			 const struct iiod_command *cmd,
			 const struct iiod_buf *buf, size_t nb)
{
	int ret;

	ret = iiod_io_send_command_async(io, cmd, buf, nb);
	if (ret)
		return ret;

	return iiod_io_wait_for_command_done(io);
}

int iiod_io_send_response_async(struct iiod_io *io, intptr_t code,
				const struct iiod_buf *buf, size_t nb)
{
	return iiod_enqueue_command(io, IIOD_OP_RESPONSE, 0, code, buf, nb);
}

int iiod_io_send_response(struct iiod_io *io, intptr_t code,
			  const struct iiod_buf *buf, size_t nb)
{
	int ret;

	ret = iiod_io_send_response_async(io, code, buf, nb);
	if (ret)
		return ret;

	return iiod_io_wait_for_command_done(io);
}

int iiod_io_get_response_async(struct iiod_io *io,
			       const struct iiod_buf *buf, size_t nb)
{
	struct iiod_responder *priv = io->responder;
	struct iiod_io *tmp;

	if (nb > NB_BUFS_MAX)
		return -EINVAL;

	iio_mutex_lock(priv->lock);
	if (priv->thrd_stop) {
		/* Thread has been stopped, cannot enqueue response */
		iio_mutex_unlock(priv->lock);
		return priv->thrd_err_code;
	}

	if (nb)
		memcpy(io->r_io.buf, buf, sizeof(*buf) * nb);
	io->r_io.nb_buf = nb;
	io->r_done = false;
	io->r_next = NULL;
	io->r_io.start_time = read_counter_us();

	/* Add it to the readers list */
	if (!priv->readers) {
		priv->readers = io;
	} else {
		for (tmp = priv->readers; tmp->r_next; )
			tmp = tmp->r_next;
		tmp->r_next = io;
	}

	iio_mutex_unlock(priv->lock);

	return 0;
}

int iiod_io_exec_command(struct iiod_io *io,
			 const struct iiod_command *cmd,
			 const struct iiod_buf *cmd_buf,
			 const struct iiod_buf *buf)
{
	int ret;

	ret = iiod_io_get_response_async(io, buf, buf != NULL);
	if (ret < 0)
		return ret;

	ret = iiod_io_send_command(io, cmd, cmd_buf, cmd_buf != NULL);
	if (ret < 0) {
		iiod_io_cancel(io);
		return ret;
	}

	return (int) iiod_io_wait_for_response(io);
}

struct iiod_io *
iiod_responder_create_io(struct iiod_responder *priv, uint16_t id)
{
	struct iiod_io *io;
	int err;

	io = zalloc(sizeof(*io));
	if (!io)
		return iio_ptr(-ENOMEM);

	io->responder = priv;
	io->refcnt = 1;
	io->timeout_ms = priv->timeout_ms;

	io->cond = iio_cond_create();
	err = iio_err(io->cond);
	if (err)
		goto err_free_io;

	io->lock = iio_mutex_create();
	err = iio_err(io->lock);
	if (err)
		goto err_free_cond;

	io->client_id = id;

	return io;

err_free_cond:
	iio_cond_destroy(io->cond);
err_free_io:
	free(io);
	return iio_ptr(err);
}

void
iiod_responder_set_timeout(struct iiod_responder *priv, unsigned int timeout_ms)
{
	priv->timeout_ms = timeout_ms;
	priv->default_io->timeout_ms = timeout_ms;
}

void
iiod_io_set_timeout(struct iiod_io *io, unsigned int timeout_ms)
{
	io->timeout_ms = timeout_ms;
}

struct iiod_responder *
iiod_responder_create(const struct iiod_responder_ops *ops, void *d)
{
	struct iiod_responder *priv;
	int err;

	priv = zalloc(sizeof(*priv));
	if (!priv)
		return iio_ptr(-ENOMEM);

	priv->ops = ops;
	priv->d = d;

	priv->lock = iio_mutex_create();
	err = iio_err(priv->lock);
	if (err)
		goto err_free_priv;

	priv->default_io = iiod_responder_create_io(priv, 0);
	err = iio_err(priv->default_io);
	if (err)
	      goto err_free_lock;

	priv->write_task = iio_task_create(iiod_responder_write, priv,
					   "iiod-responder-writer-task");
	err = iio_err(priv->write_task);
	if (err)
		goto err_free_io;

	priv->read_thrd = iio_thrd_create(iiod_responder_reader_thrd, priv,
					  "iiod-responder-reader-thd");
	err = iio_err(priv->read_thrd);
	if (err)
		goto err_free_write_task;

	iio_task_start(priv->write_task);

	return priv;

err_free_write_task:
	iio_task_destroy(priv->write_task);
err_free_io:
	iiod_io_unref(priv->default_io);
err_free_lock:
	iio_mutex_destroy(priv->lock);
err_free_priv:
	free(priv);
	return iio_ptr(err);
}

void iiod_responder_destroy(struct iiod_responder *priv)
{
	priv->thrd_stop = true;
	iiod_responder_wait_done(priv);

	iio_task_destroy(priv->write_task);

	iiod_io_unref(priv->default_io);
	iio_mutex_destroy(priv->lock);
	free(priv);
}

void iiod_responder_wait_done(struct iiod_responder *priv)
{
	if (priv->read_thrd) {
		iio_thrd_join_and_destroy(priv->read_thrd);
		priv->read_thrd = NULL;
	}
}

struct iiod_io * iiod_command_create_io(const struct iiod_command *cmd,
					struct iiod_command_data *data)
{
	struct iiod_responder *priv = (struct iiod_responder *) data;

	return iiod_responder_create_io(priv, cmd->client_id);
}

void iiod_io_cancel(struct iiod_io *io)
{
	struct iiod_responder *priv = io->responder;
	struct iio_task_token *token;

	iio_mutex_lock(priv->lock);
	__iiod_io_cancel_unlocked(io);
	token = io->write_token;
	io->write_token = NULL;
	iio_mutex_unlock(priv->lock);

	/* Discard the entry from the writers list */
	if (token) {
		iio_task_cancel(token);
		iio_task_sync(token, 0);
	}

	/* Cancel any pending response request */
	iiod_io_cancel_response(io);
}

static void iiod_io_destroy(struct iiod_io *io)
{
	iio_mutex_destroy(io->lock);
	iio_cond_destroy(io->cond);
	free(io);
}

void iiod_io_ref(struct iiod_io *io)
{
	struct iiod_responder *priv = io->responder;

	iio_mutex_lock(priv->lock);

	if (io->refcnt > 0)
		io->refcnt += 1;

	iio_mutex_unlock(priv->lock);
}

void iiod_io_unref(struct iiod_io *io)
{
	struct iiod_responder *priv = io->responder;

	iio_mutex_lock(priv->lock);

	io->refcnt -= 1;
	if (io->refcnt == 0)
		iiod_io_destroy(io);

	iio_mutex_unlock(priv->lock);
}

struct iiod_io *
iiod_responder_get_default_io(struct iiod_responder *priv)
{
	return priv->default_io;
}

struct iiod_io *
iiod_command_get_default_io(struct iiod_command_data *data)
{
	return iiod_responder_get_default_io((struct iiod_responder *) data);
}
