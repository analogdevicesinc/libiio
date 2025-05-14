// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022-2024 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"

#include <iio/iio-lock.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

struct iio_task_token {
	struct iio_task *task;
	struct iio_task_token *next;
	void *elm;

	struct iio_cond *done_cond;
	struct iio_mutex *done_lock;
	bool done, autoclear;
	int ret;
};

struct iio_task {
	struct iio_thrd *thrd;
	struct iio_cond *cond;
	struct iio_mutex *lock;
	int (*fn)(void *, void *);
	void *firstarg;

	struct iio_task_token *list;
	bool running, stop;
};

static void iio_task_process(struct iio_task *task)
{
	struct iio_task_token *entry;
	bool autoclear;

	/* Signal that we're idle */
	iio_cond_signal(task->cond);

	while (!task->stop && !(task->list && task->running)) {
		iio_cond_wait(task->cond, task->lock, 0);

		/* If iio_task_stop() was called while we were waiting
		 * for clients, notify that we're idle. */
		if (!task->running)
			iio_cond_signal(task->cond);
	}

	if (task->stop)
		return;

	entry = task->list;
	task->list = entry->next;
	iio_mutex_unlock(task->lock);

	entry->ret = task->fn(task->firstarg, entry->elm);

	iio_mutex_lock(entry->done_lock);
	entry->done = true;
	autoclear = entry->autoclear;
	iio_cond_signal(entry->done_cond);
	iio_mutex_unlock(entry->done_lock);

	if (autoclear)
		iio_task_token_destroy(entry);

	iio_mutex_lock(task->lock);
}

static int iio_task_run(void *d)
{
	struct iio_task *task = d;

	iio_mutex_lock(task->lock);

	while (!task->stop)
		iio_task_process(task);

	iio_mutex_unlock(task->lock);

	return 0;
}
static int iio_task_sync_core(struct iio_task_token *token, unsigned int timeout_ms, bool token_destroy)
{
	int ret;

	iio_mutex_lock(token->done_lock);
	while (!token->done) {
		ret = iio_cond_wait(token->done_cond, token->done_lock,
				    timeout_ms);
		if (ret) {
			iio_mutex_unlock(token->done_lock);
			iio_task_cancel(token);
			iio_mutex_lock(token->done_lock);
		}
	}
	iio_mutex_unlock(token->done_lock);

	ret = token->ret;

	if (token_destroy)
		iio_task_token_destroy(token);

	return ret;
}

static bool iio_task_token_find(struct iio_task *task, struct iio_task_token *token,
				bool del)
{
	struct iio_task_token *tmp;

	if (!task->list)
		return false;

	if (token == task->list) {
		if (del)
			task->list = token->next;
		return true;
	}

	for (tmp = task->list; tmp->next; tmp = tmp->next) {
		if (tmp->next == token) {
			if (del)
				tmp->next = token->next;
			return true;
		}
	}

	return false;
}

struct iio_task * iio_task_create(int (*fn)(void *, void *),
				  void *firstarg, const char *name)
{
	struct iio_task *task;
	int err = -ENOMEM;

	task = calloc(1, sizeof(*task));
	if (!task)
		return iio_ptr(-ENOMEM);

	task->lock = iio_mutex_create();
	err = iio_err(task->lock);
	if (err)
		goto err_free_task;

	task->cond = iio_cond_create();
	err = iio_err(task->cond);
	if (err)
		goto err_free_lock;

	task->fn = fn;
	task->firstarg = firstarg;

	if (!NO_THREADS) {
		task->thrd = iio_thrd_create(iio_task_run, task, name);
		err = iio_err(task->thrd);
		if (err)
			goto err_free_cond;
	}

	return task;

err_free_cond:
	iio_cond_destroy(task->cond);
err_free_lock:
	iio_mutex_destroy(task->lock);
err_free_task:
	free(task);
	return iio_ptr(err);
}

static int iio_task_token_do_enqueue(struct iio_task *task, struct iio_task_token *token,
				     bool autoclear, bool new_token)
{
	struct iio_task_token *tmp;

	iio_mutex_lock(task->lock);

	if (!new_token) {
		/* make sure the list is properly terminated */
		token->next = NULL;
		if (iio_task_token_find(task, token, false)) {
			iio_mutex_unlock(task->lock);
			return -EEXIST;
		}
	}

	if (task->stop) {
		iio_mutex_unlock(task->lock);
		return -EBADF;
	}

	if (!task->list) {
		task->list = token;
	} else {
		for (tmp = task->list; tmp->next; ) {
			tmp = tmp->next;
		}

		tmp->next = token;
	}

	iio_mutex_lock(token->done_lock);
	token->autoclear = autoclear;
	token->done = false;
	iio_mutex_unlock(token->done_lock);

	iio_cond_signal(task->cond);
	iio_mutex_unlock(task->lock);

	if (NO_THREADS && !task->stop && task->running)
		iio_task_process(task);

	return 0;
}

static struct iio_task_token *
iio_task_do_enqueue(struct iio_task *task, void *elm, bool autoclear)
{
	struct iio_task_token *entry;
	int err;

	entry = iio_task_token_create(task, elm);
	if (iio_err(entry))
		return iio_err_cast(entry);

	err = iio_task_token_do_enqueue(task, entry, autoclear, true);
	if (err)
		goto err_destroy_entry;

	return entry;

err_destroy_entry:
	iio_task_token_destroy(entry);
	return iio_ptr(err);
}

void iio_task_token_destroy(struct iio_task_token *token)
{
	iio_mutex_destroy(token->done_lock);
	iio_cond_destroy(token->done_cond);
	free(token);
}

struct iio_task_token * iio_task_token_create(struct iio_task *task, void *elm)
{
	struct iio_task_token *entry;

	int err = -ENOMEM;

	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return iio_ptr(-ENOMEM);

	entry->task = task;
	entry->elm = elm;
	/* Initialize it to true. This matters if we create a token and never happen
	 * to enqueue it. iio_task_cancel_sync() would then wait forever.
	 */
	entry->done = true;

	entry->done_cond = iio_cond_create();
	err = iio_err(entry->done_cond);
	if (err)
		goto err_free_entry;

	entry->done_lock = iio_mutex_create();
	err = iio_err(entry->done_lock);
	if (err)
		goto err_free_cond;

	return entry;

err_free_cond:
	iio_cond_destroy(entry->done_cond);
err_free_entry:
	free(entry);
	return iio_ptr(err);
}

int iio_task_token_enqueue(struct iio_task_token *token)
{
	return iio_task_token_do_enqueue(token->task, token, false, false);
}

struct iio_task_token * iio_task_enqueue(struct iio_task *task, void *elm)
{
	return iio_task_do_enqueue(task, elm, false);
}

int iio_task_enqueue_autoclear(struct iio_task *task, void *elm)
{
	return iio_err(iio_task_do_enqueue(task, elm, true));
}

int iio_task_sync(struct iio_task_token *token, unsigned int timeout_ms)
{
	return iio_task_sync_core(token, timeout_ms, true);
}

void iio_task_flush(struct iio_task *task)
{
	struct iio_task_token *token;

	iio_mutex_lock(task->lock);

	while (task->list) {
		token = task->list;
		task->list = token->next;
		iio_mutex_unlock(task->lock);

		iio_mutex_lock(token->done_lock);
		token->done = true;
		token->ret = -EINTR;
		iio_cond_signal(token->done_cond);
		iio_mutex_unlock(token->done_lock);

		iio_mutex_lock(task->lock);
	}

	iio_mutex_unlock(task->lock);
}

int iio_task_destroy(struct iio_task *task)
{
	int ret = 0;

	iio_mutex_lock(task->lock);
	task->stop = true;
	iio_cond_signal(task->cond);
	iio_mutex_unlock(task->lock);

	if (!NO_THREADS)
		ret = iio_thrd_join_and_destroy(task->thrd);

	iio_task_flush(task);

	iio_cond_destroy(task->cond);
	iio_mutex_destroy(task->lock);
	free(task);

	return ret;
}

bool iio_task_is_done(struct iio_task_token *token)
{
	return token->done;
}

int iio_task_cancel_sync(struct iio_task_token *token, unsigned int timeout_ms)
{
	iio_task_cancel(token);
	return iio_task_sync_core(token, timeout_ms, false);
}

void iio_task_cancel(struct iio_task_token *token)
{
	struct iio_task *task = token->task;
	bool found = false;

	iio_mutex_lock(task->lock);
	found = iio_task_token_find(task, token, true);
	iio_mutex_unlock(task->lock);

	if (found) {
		iio_mutex_lock(token->done_lock);
		token->done = true;
		token->ret = -ETIMEDOUT;
		iio_cond_signal(token->done_cond);
		iio_mutex_unlock(token->done_lock);
	}

	/* If it wasn't removed from the list, it's being processed or
	 * has been processed already; there is nothing to do here. */
}

void iio_task_start(struct iio_task *task)
{
	iio_mutex_lock(task->lock);
	task->running = true;
	iio_cond_signal(task->cond);
	iio_mutex_unlock(task->lock);

	if (NO_THREADS && !task->stop)
		while (task->list)
			iio_task_process(task);
}

void iio_task_stop(struct iio_task *task)
{
	iio_mutex_lock(task->lock);
	task->running = false;
	iio_cond_signal(task->cond);
	iio_cond_wait(task->cond, task->lock, 0);
	iio_mutex_unlock(task->lock);
}
