// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#define _DEFAULT_SOURCE

#include <getopt.h>
#include <iio.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>

#ifdef __APPLE__
	/* Needed for sysctlbyname */
#include <sys/sysctl.h>
#endif

#ifdef _WIN32
#include <sysinfoapi.h>
#endif

#include "iio_common.h"

#define MY_NAME "iio_stresstest"

#define SAMPLES_PER_READ 256
#define NUM_TIMESTAMPS (16*1024)

static int getNumCores(void) {
#ifdef _WIN32
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#elif __APPLE__
	int count;
	size_t count_len = sizeof(count);
	sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);
	return count;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}


/* Code snippet insired by Nick Strupat
 * https://stackoverflow.com/questions/794632/programmatically-get-the-cache-line-size
 * released under the "Feel free to do whatever you want with it." license.
 */
static size_t cache_line_size(void)
{
	size_t cacheline = 0;

#ifdef _WIN32
	DWORD buffer_size = 0;
	DWORD i = 0;
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION * buffer = 0;

	GetLogicalProcessorInformation(0, &buffer_size);
	buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(buffer_size);
	GetLogicalProcessorInformation(&buffer[0], &buffer_size);

	for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
		if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
			cacheline = buffer[i].Cache.LineSize;
			break;
		}
	}
	free(buffer);

#elif __APPLE__
	size_t sizeof_line_size = sizeof(cacheline);
	sysctlbyname("hw.cachelinesize", &cacheline, &sizeof_line_size, 0, 0);

#elif __linux__
	FILE * p = 0;
	int ret;

	p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
	if (p) {
		ret = fscanf(p, "%zu", &cacheline);
		fclose(p);
		if (ret != 1)
			cacheline = 0;
	}
#endif
	return cacheline;
}

static const struct option options[] = {
	{"help", no_argument, 0, 'h'},
	{"uri", required_argument, 0, 'u'},
	{"buffer-size", required_argument, 0, 'b'},
	{"samples", required_argument, 0, 's' },
	{"duration", required_argument, 0, 'd'},
	{"threads", required_argument, 0, 't'},
	{"verbose", no_argument, 0, 'v'},
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	("[-n <hostname>] [-u <vid>:<pid>] [-t <trigger>] [-b <buffer-size>] [-s <samples>]"
		"<iio_device> [<channel> ...]"),
	"Show this help and quit.",
	"Use the context at the provided URI.",
	"Size of the capture buffer. Default is 256.",
	"Number of samples to capture, 0 = infinite. Default is 0.",
	"Time to wait (in s) between stopping all threads",
	"Number of Threads",
	"Increase verbosity (-vv and -vvv for more)",
};

static bool app_running = true;
static bool threads_running = true;
static int exit_code = EXIT_SUCCESS;

static int compare_timeval(const void *a, const void *b)
{
	const struct timeval *t1 = (struct timeval *)a;
	const struct timeval *t2 = (struct timeval *)b;

	if (t1->tv_sec < t2->tv_sec)
		return -1;
	if (t1->tv_sec > t2->tv_sec)
		return 1;

	/* only way to get here is if *t1.tv_sec == *t2.tv_sec */
	if (t1->tv_usec < t2->tv_usec)
		return -1;
	if (t1->tv_usec > t2->tv_usec)
		return 1;

	/* must be same */
	return 0;
}
static void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
	if (sig == SIGSEGV) {
		fprintf(stderr, "fatal error SIGSEGV, break out gdb\n");
		abort();
	}
}

static void set_handler(int signal_nb, void (*handler)(int))
{
#ifdef _WIN32
	signal(signal_nb, handler);
#else
	struct sigaction sig;
	sigaction(signal_nb, NULL, &sig);
	sig.sa_handler = handler;
	sigaction(signal_nb, &sig, NULL);
#endif
}

static struct iio_device * get_device(const struct iio_context *ctx,
		const char *id)
{
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	struct iio_device *device;

	for (i = 0; i < nb_devices; i++) {
		const char *name;
		device = iio_context_get_device(ctx, i);
		name = iio_device_get_name(device);
		if (name && !strcmp(name, id))
			break;
		if (!strcmp(id, iio_device_get_id(device)))
			break;
	}

	if (i < nb_devices)
		return device;

	fprintf(stderr, "Device %s not found\n", id);
	return NULL;
}

enum verbosity {
	QUIET,
	SUMMARY,
	VERBOSE,
	VERYVERBOSE,
};

struct info {
	int argc;
	char **argv;
	enum backend back;
	enum verbosity verbose;

	int uri_index, device_index, arg_index;
	unsigned int buffer_size, timeout;
	unsigned int num_threads;
	pthread_t *tid;
	unsigned int *starts, *buffers, *refills;
	pthread_t *threads;
	struct timeval **start;
};

static void thread_err(int id, ssize_t ret, char * what)
{
	if (ret < 0) {
		char err_str[1024];
		iio_strerror(-ret, err_str, sizeof(err_str)); \
		fprintf(stderr, "%i : IIO ERROR : %s : %s\n", id, what, err_str); \
	}
}

static void *client_thread(void *data)
{
	struct info *info = data;
	struct iio_context *ctx;
	struct iio_buffer *buffer;
	unsigned int i, nb_channels, duration;
	struct iio_device *dev;
	struct timeval start, end;
	int id = -1, stamp, r_errno;
	ssize_t ret;

	/* Find my ID */
	for (i = 0; i < info->num_threads; i++) {
		if (pthread_equal(info->threads[i], pthread_self())) {
			info->tid[i] = pthread_self();
			id = i;
			break;
		}
	}

	if (info->verbose == VERYVERBOSE)
		printf("%2d: Entered\n", id);

	stamp = 0;
	while (stamp < NUM_TIMESTAMPS && info->start[id][stamp].tv_sec) {
		stamp++;
	}

	while (app_running && threads_running) {
		gettimeofday(&start, NULL);
		do {
			errno = 0;
			if (info->uri_index) {
				ctx = iio_create_context_from_uri(info->argv[info->uri_index]);
			} else {
				ctx = iio_create_default_context();
			}
			r_errno = errno;
			gettimeofday(&end, NULL);

			duration = ((end.tv_sec - start.tv_sec) * 1000) +
					((end.tv_usec - start.tv_usec) / 1000);
		} while (threads_running && !ctx && duration < info->timeout);

		if (!ctx) {
			thread_err(id, r_errno, "Unable to create IIO context");
			goto thread_fail;
		}

		/* store the timestamp of the context creation */
		info->start[id][stamp].tv_sec = end.tv_sec;
		info->start[id][stamp].tv_usec = end.tv_usec;
		stamp++;
		if (stamp > NUM_TIMESTAMPS - 10 )
			threads_running = false;

		/* started another context */
		info->starts[id]++;

		ret = iio_context_set_timeout(ctx, UINT_MAX);
		thread_err(id, ret, "iio_context_set_timeout failed");

		dev = get_device(ctx, info->argv[info->arg_index + 1]);
		if (!dev) {
			iio_context_destroy(ctx);
			goto thread_fail;
		}

		nb_channels = iio_device_get_channels_count(dev);

		if (info->argc == info->arg_index + 2) {
			/* Enable all channels */
			for (i = 0; i < nb_channels; i++)
				iio_channel_enable(iio_device_get_channel(dev, i));
		} else {
			for (i = 0; i < nb_channels; i++) {
				unsigned int j;
				struct iio_channel *ch = iio_device_get_channel(dev, i);
				for (j = info->arg_index + 2; j < (unsigned int) info->argc; j++) {
					const char *n = iio_channel_get_name(ch);
					if (!strcmp(info->argv[j], iio_channel_get_id(ch)) ||
							(n && !strcmp(n, info->argv[j])))
						iio_channel_enable(ch);
				}
			}
		}

		if (info->verbose == VERYVERBOSE)
			printf("%2d: Running\n", id);

		i = 0;
		while (threads_running || i == 0) {
			info->buffers[id]++;
			buffer = iio_device_create_buffer(dev, info->buffer_size, false);
			if (!buffer) {
				struct timespec wait;
				wait.tv_sec = 0;
				wait.tv_nsec = (1 * 1000);
				thread_err(id, errno, "iio_device_create_buffer failed");
				nanosleep(&wait, &wait);
				continue;
			}
	
			while (threads_running || i == 0) {
				ret = iio_buffer_refill(buffer);
				thread_err(id, ret, "iio_buffer_refill failed");
				if (ret < 0) {
					threads_running = 0;
					break;
				}
				info->refills[id]++;
				i = 1;

				/* depending on backend, do more */
				if(info->back == IIO_USB && rand() % 3 == 0)
					break;
				else if (info->back == IIO_NETWORK && rand() % 5 == 0)
					break;
				else if (rand() % 10 == 0)
					break;
			}
			iio_buffer_destroy(buffer);

			/* depending on backend, do more */
			if(info->back == IIO_USB) {
				break;
			} else if (info->back == IIO_NETWORK) {
				if (rand() % 5 == 0)
					break;
			} else {
				if (rand() % 10 == 0)
					break;
			}
		}

		iio_context_destroy(ctx);
		if (info->verbose == VERYVERBOSE)
			printf("%2d: Stopping\n", id);

		/* 1 in 10, (or with above loops, 1 in 1000 stop */
		if (rand() % 100 == 0) {
			break;
		}
	}

	if (info->verbose == VERYVERBOSE)
		printf("%2d: Stopped normal\n", id);
	info->tid[id] = 0;
	info->start[id][stamp].tv_sec = 0; info->start[id][stamp].tv_usec = 0;
	return (void *)0;

thread_fail:
	if (info->verbose == VERYVERBOSE)
		printf("%2d: Stopped via error\n", id);
	info->tid[id] = 0;
	info->start[id][stamp].tv_sec = 0; info->start[id][stamp].tv_usec = 0;
	return (void *)EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	sigset_t set, oldset;
	struct info info;
	int option_index;
	unsigned int i, duration;
	int c, pret;
	struct timeval start, end, s_loop;
	void **ret;
	size_t min_samples;

#ifndef _WIN32
	set_handler(SIGHUP, &quit_all);
	set_handler(SIGPIPE, &quit_all);
#endif
	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);

	info.num_threads = getNumCores() * 4;
	info.buffer_size = SAMPLES_PER_READ;
	info.arg_index = 0;
	info.uri_index = 0;
	info.timeout = UINT_MAX;
	info.verbose = QUIET;
	info.argc = argc;
	info.argv = argv;

	min_samples = cache_line_size();
	if(!min_samples)
		min_samples = 128;

	while ((c = getopt_long(argc, argv, "hvu:b:s:t:T:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(MY_NAME, options, options_descriptions);
			return EXIT_SUCCESS;
		case 'u':
			info.arg_index += 2;
			info.uri_index = info.arg_index;
			break;
		case 'b':
			info.arg_index += 2;
			/* Max 4M , min 64 bytes (cache line) */
			info.buffer_size = sanitize_clamp("buffersize", info.argv[info.arg_index],
					min_samples, 1024 * 1024 * 4);
			break;
		case 'T':
			info.arg_index +=2;
			/* ensure between least once a day and never (0) */
			info.timeout = 1000 * sanitize_clamp("timeout", info.argv[info.arg_index],
					0, 60 * 60 * 24);
			break;
		case 't':
			info.arg_index +=2;
			/* Max number threads 1024, min 1 */
			info.num_threads = sanitize_clamp("threads", info.argv[info.arg_index],
					1, 1024);
			break;
		case 'v':
			if (!info.verbose)
				info.arg_index++;
			info.verbose++;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (info.arg_index + 1 >= argc) {
		fprintf(stderr, "Incorrect number of arguments.\n");
		if (info.uri_index) {
			struct iio_context *ctx = iio_create_context_from_uri(info.argv[info.uri_index]);
			if (ctx) {
				fprintf(stderr, "checking uri %s\n", info.argv[info.uri_index]);
				i = iio_context_set_timeout(ctx, 500);
				thread_err(-1, i, "iio_context_set_timeout fail");
				unsigned int nb_devices = iio_context_get_devices_count(ctx);
				for (i = 0; i < nb_devices; i++) {
					unsigned int j;
					const struct iio_device *dev = iio_context_get_device(ctx, i);
					const char *name = iio_device_get_name(dev);
					unsigned int nb_channels = iio_device_get_channels_count(dev);
					if (!iio_device_get_buffer_attrs_count(dev))
						continue;
					for (j = 0; j < nb_channels; j++) {
						struct iio_channel *ch = iio_device_get_channel(dev, j);
						if (iio_channel_is_output(ch))
							continue;
						iio_channel_enable(ch);
					}
					struct iio_buffer *buffer = iio_device_create_buffer(dev, info.buffer_size, false);
					if (buffer) {
						iio_buffer_destroy(buffer);
						printf("try : %s\n", name);
					}
				}
				iio_context_destroy(ctx);
			} else {
				fprintf(stderr, "need valid uri\n");
			}
		}
		fprintf(stderr, "\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	if (info.uri_index) {
		struct iio_context *ctx = iio_create_context_from_uri(info.argv[info.uri_index]);
		if (!ctx) {
			fprintf(stderr, "need valid uri\n");
			usage(MY_NAME, options, options_descriptions);
			return EXIT_FAILURE;
		}
		iio_context_destroy(ctx);
		if (!strncmp(info.argv[info.uri_index], "usb:", strlen("usb:")))
			info.back = IIO_USB;
		else if (!strncmp(info.argv[info.uri_index], "ip:", strlen("ip:")))
			info.back = IIO_NETWORK;
		else if (!strncmp(info.argv[info.uri_index], "local:", strlen("local:")))
			info.back = IIO_LOCAL;

	} else {
		fprintf(stderr, "need valid uri\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	/* prep memory for all the threads */
	size_t histogram[10];
	histogram[0] = histogram[1] = histogram[2] = histogram[3] = histogram[4] = 0;
	histogram[5] = histogram[6] = histogram[7] = histogram[8] = 0;

	info.threads = calloc(info.num_threads, sizeof(*info.threads));
	info.tid = calloc(info.num_threads, sizeof(*info.threads));
	info.starts = calloc(info.num_threads, sizeof(unsigned int));
	info.buffers = calloc(info.num_threads, sizeof(unsigned int));
	info.refills = calloc(info.num_threads, sizeof(unsigned int));
	info.start = calloc(info.num_threads, sizeof(struct timeval *));

	ret = (void *)calloc(info.num_threads, sizeof(void *));

	if (!ret || !info.start || !info.refills || !info.buffers || !info.starts ||
			!info.tid || !info.threads) {
		fprintf(stderr, "Memory allocation failure\n");
		return 0;
	}

	for (i = 0; i < info.num_threads; i++) {
		info.start[i] = malloc(NUM_TIMESTAMPS * sizeof(struct timeval));
	}

	sigfillset(&set);
	/* turn off buffering */
	setbuf(stdout, NULL);

	gettimeofday(&s_loop, NULL);
	while (app_running) {
		unsigned int flag;

		/* start all the threads */
		threads_running = true;
		pthread_sigmask(SIG_BLOCK, &set, &oldset);
		for (i = 0; i < info.num_threads; i++) {
			/* before starting a thread, set up things */
			info.start[i][0].tv_sec = 0; info.start[i][0].tv_usec = 0;
			memset(&info.tid[i], -1, sizeof(pthread_t));
			pthread_create(&info.threads[i], NULL, client_thread, &info);
		}
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		gettimeofday(&start, NULL);

		/* If a thread prematurely dies, start it again */
		while (app_running && threads_running) {
			/* If we find a thread that isn't running, restart it */
			for (i = 0; i < info.num_threads && threads_running; i++){
				if (info.tid[i] == 0){
					if (info.verbose == VERYVERBOSE)
						printf("waiting for %u\n", i);
					pret = pthread_join(info.threads[i], &ret[i]);
					thread_err(-1, pret, "pthread_join fail");
					if (pret < 0) {
						app_running = 0;
					} else {
						memset(&info.tid[i], -1, sizeof(pthread_t));
						pthread_create(&info.threads[i], NULL, client_thread, &info);
					}
				}
			}

			/* if we timeout, stop */
			gettimeofday(&end, NULL);
			duration = ((end.tv_sec - start.tv_sec) * 1000) +
					((end.tv_usec - start.tv_usec) / 1000);
			if (info.timeout && duration >= info.timeout) {
				threads_running = false;
			} else {
				struct timespec wait;
				wait.tv_sec = 0;
				wait.tv_nsec = (1000 * 1000);
				nanosleep(&wait, &wait);
			}
		}

		gettimeofday(&end, NULL);
		duration = ((end.tv_sec - s_loop.tv_sec) * 1000) +
			((end.tv_usec - s_loop.tv_usec) / 1000);

		flag = 0;
		threads_running = false;

		/* let all the threads end */
		if (!app_running || info.verbose >= SUMMARY)
			printf("-------------------------------------------------------------\n");
		for (i = 0; i < info.num_threads; i++) {
			pret = pthread_join(info.threads[i], &ret[i]);
			thread_err(-1, pret, "pthread_join fail");
			if (pret < 0)
				app_running = 0;
		}
		/* Did at least one thread end in success? */
		for (i = 0; i < info.num_threads; i++) {
			if (!((int) (intptr_t)ret[i])) {
				flag = 1;
				break;
			}
		}
		if (!flag) {
			app_running = 0;
			printf("All threads failed\n");
		}

		/* Calculate some stats about the threads */
		unsigned int a =0, b = 0;
		c = 0;
		for (i = 0; i < info.num_threads; i++) {
			a+= info.starts[i];
			b+= info.buffers[i];
			c+= info.refills[i];
			if (!app_running || info.verbose >= VERBOSE)
				printf("%2u: Ran : %u times, opening %u buffers, doing %u refills\n",
						i, info.starts[i], info.buffers[i], info.refills[i]);
		}
		if (!app_running || info.verbose >= SUMMARY)
			printf("total: ");
		i = duration/1000;
		flag=0;
		if (i > 60*60*24) {
			if (!app_running || info.verbose >= SUMMARY)
				printf("%ud", i/(60*60*24));
			i -= (i/(60*60*24))*60*60*24;
			flag = 1;
		}
		if (flag || i > 60*60) {
			if (!app_running || info.verbose >= SUMMARY) {
				if (flag)
					printf("%02uh", i/(60*60));
				else
					printf("%uh", i/(60*60));
			}
			i -= (i/(60*60))*60*60;
			flag = 1;
		}
		if (flag || i > 60) {
			if (!app_running || info.verbose >= SUMMARY) {
				if (flag)
					printf("%02um", i/60);
				else
					printf("%um", i/60);
			}
			i -= (i/60)*60;
			flag = 1;
		}
		if (flag || i) {
			if (!app_running || info.verbose >= SUMMARY)
				printf("%02us", i);
		}

		if (!app_running || info.verbose >= SUMMARY) {
			printf(" Context : %i (%2.2f/s), buffers: %i (%2.2f/s), refills : %i (%2.2f/s)\n",
					a, (double)a * 1000 / duration,
					b, (double)b * 1000 / duration,
					c, (double)c * 1000 / duration);
		}
		/* gather and sort things, so we can print out a histogram */
		struct timeval *sort;
		sort = calloc(info.num_threads * NUM_TIMESTAMPS, sizeof(struct timeval));
		if (!sort) {
			app_running = 0;
			fprintf(stderr, "Memory allocation failure\n");
			break;
		}
		b = 0;
		/* gather */
		for (i = 0; i < info.num_threads; i++) {
			for (a = 0; a < NUM_TIMESTAMPS; a++) {
				if (info.start[i][a].tv_sec) {
					sort[b].tv_sec = info.start[i][a].tv_sec;
					sort[b].tv_usec = info.start[i][a].tv_usec;
					b++;
				} else {
					/* if we hit a zero, this loop is done */
					break;
				}
			}
		}
		/* sort */
		qsort(sort, b, sizeof(struct timeval), compare_timeval);
		/* bin */
		for (i = 1; i < b; i++) {
			duration = (sort[i].tv_sec - sort[i -1].tv_sec) * 1000000 +
				sort[i].tv_usec - sort[i - 1].tv_usec;
			histogram[8]++;
			if (duration == 0)
				histogram[0]++;
			else if (duration < 10)
				histogram[1]++;
			else if (duration < 100)
				histogram[2]++;
			else if (duration < 1000)
				histogram[3]++;
			else if (duration < 10000)
				histogram[4]++;
			else if (duration < 100000)
				histogram[5]++;
			else if (duration < 1000000)
				histogram[6]++;
			else
				histogram[7]++;
		}
		/* dump */
		if (!app_running || info.verbose >= SUMMARY) {
			printf("    0        : %7zu (%5.2f%%)\n",
					histogram[0],
					(double)histogram[0]*100/histogram[8]);
			printf("  1 - 9   μs : %7zu (%5.2f%%)\n",
					histogram[1],
					(double)histogram[1]*100/histogram[8]);
			printf(" 10 - 99  μs : %7zu (%5.2f%%)\n",
					histogram[2],
					(double)histogram[2]*100/histogram[8]);
			printf("100 - 999 μs : %7zu (%5.2f%%)\n",
					histogram[3],
					(double)histogram[3]*100/histogram[8]);
			printf("  1 - 9.9 ms : %7zu (%5.2f%%)\n",
					histogram[4],
					(double)histogram[4]*100/histogram[8]);
			printf(" 10 - 99  ms : %7zu (%5.2f%%)\n",
					histogram[5],
					(double)histogram[5]*100/histogram[8]);
			printf("100 - 999 ms : %7zu (%5.2f%%)\n",
					histogram[6],
					(double)histogram[6]*100/histogram[8]);
			printf("over 1 s     : %7zu (%5.2f%%)\n",
					histogram[7],
					(double)histogram[7]*100/histogram[8]);
			printf("\n");
		}
		free(sort);

		/* if the app is still running, go again */
	}

	free(info.threads);
	free(info.tid);
	free(info.starts);
	free(info.buffers);
	free(info.refills);
	for (i = 0; i < info.num_threads; i++)
		free(info.start[i]);
	free(info.start);
	free(ret);
	return 0;
}
