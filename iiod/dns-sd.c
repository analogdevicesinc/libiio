// SPDX-license-identifier: LGPL-v2-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dns-sd.h"
#include "ops.h"
#include "thread-pool.h"

#include "../debug.h"
#include "../iio.h"

#include <avahi-common/thread-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/domain.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <stddef.h>
#include <time.h>

/***
 * Parts of the avahi code are borrowed from the client-publish-service.c
 * https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 * which is an example in the avahi library. Copyright Lennart Poettering
 * released under the LGPL 2.1 or (at your option) any later version.
 */

/* Global Data */

static struct avahi_data {
	AvahiThreadedPoll *poll;
	AvahiClient *client;
	AvahiEntryGroup *group;
	char * name;
	uint16_t port;
} avahi;

static void create_services(AvahiClient *c);
static AvahiClient * client_new(void);

static void client_free()
{
	/* This also frees the entry group, if any. */
	if (avahi.client) {
		avahi_client_free(avahi.client);
		avahi.client = NULL;
		avahi.group = NULL;
	}
}

static void shutdown_avahi()
{
	/* Stop the avahi client, if it's running. */
	if (avahi.poll)
		avahi_threaded_poll_stop(avahi.poll);

	/* Clean up the avahi objects. The order is significant. */
	client_free();
	if (avahi.poll) {
		avahi_threaded_poll_free(avahi.poll);
		avahi.poll = NULL;
	}
	if (avahi.name) {
		IIO_INFO("Avahi: Removing service '%s'\n", avahi.name);
		avahi_free(avahi.name);
		avahi.name = NULL;
	}
}

static void __avahi_group_cb(AvahiEntryGroup *group,
		AvahiEntryGroupState state, void *d)
{
	/* Called whenever the entry group state changes */
	if (!group)  {
		IIO_ERROR("__avahi_group_cb with no valid group\n");
		return;
	}

	avahi.group = group;

	switch (state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED :
			IIO_INFO("Avahi: Service '%s:%hu' successfully established.\n",
					avahi.name, avahi.port);
			break;
		case AVAHI_ENTRY_GROUP_COLLISION : {
			char *n;
			/* A service name collision, pick a new name */
			n = avahi_alternative_service_name(avahi.name);
			avahi_free(avahi.name);
			avahi.name = n;
			IIO_INFO("Avahi: Group Service name collision, renaming service to '%s:%hu'\n",
					avahi.name, avahi.port);
			create_services(avahi_entry_group_get_client(group));
			break;
		}
		case AVAHI_ENTRY_GROUP_FAILURE :
			IIO_ERROR("Entry group failure: %s\n",
					avahi_strerror(avahi_client_errno(
						avahi_entry_group_get_client(group))));
			break;
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
			/* This is normal,
			 * since we commit things in the create_services()
			 */
			IIO_DEBUG("Avahi: Group uncommitted\n");
			break;
		case AVAHI_ENTRY_GROUP_REGISTERING:
			IIO_DEBUG("Avahi: Group registering\n");
			break;
	}
}

static void __avahi_client_cb(AvahiClient *client,
		AvahiClientState state, void *d)
{
	if (!client) {
		IIO_ERROR("__avahi_client_cb with no valid client\n");
		return;
	}

	switch (state) {
		case AVAHI_CLIENT_S_RUNNING:
			/* Same as AVAHI_SERVER_RUNNING */
			IIO_DEBUG("Avahi: create services\n");
			/* The server has startup successfully, so create our services */
			create_services(client);
			break;
		case AVAHI_CLIENT_FAILURE:
			if (avahi_client_errno(client) != AVAHI_ERR_DISCONNECTED) {
				IIO_ERROR("Avahi: Client failure: %s\n",
					avahi_strerror(avahi_client_errno(client)));
				break;
			}
			IIO_INFO("Avahi: server disconnected\n");
			avahi_client_free(client);
			avahi.group = NULL;
			avahi.client = client_new();
			break;
		case AVAHI_CLIENT_S_COLLISION:
			/* Same as AVAHI_SERVER_COLLISION */
			/* When the server is back in AVAHI_SERVER_RUNNING state
			 * we will register them again with the new host name. */
			IIO_DEBUG("Avahi: Client collision\n");
			/* Let's drop our registered services.*/
			if (avahi.group)
				avahi_entry_group_reset(avahi.group);
			break;
		case AVAHI_CLIENT_S_REGISTERING:
			/* Same as AVAHI_SERVER_REGISTERING */
			IIO_DEBUG("Avahi: Client group reset\n");
			if (avahi.group)
				avahi_entry_group_reset(avahi.group);
			break;
		case AVAHI_CLIENT_CONNECTING:
			IIO_DEBUG("Avahi: Client Connecting\n");
			break;
	}

	/* NOTE: group is freed by avahi_client_free */
}

static AvahiClient * client_new(void)
{
	int ret;
	AvahiClient * client;

	client = avahi_client_new(avahi_threaded_poll_get(avahi.poll),
			AVAHI_CLIENT_NO_FAIL, __avahi_client_cb, NULL, &ret);

	/* No Daemon is handled by the avahi_start thread */
	if (!client && ret != AVAHI_ERR_NO_DAEMON) {
		IIO_ERROR("Avahi: failure creating client: %s (%d)\n",
				avahi_strerror(ret), ret);
	}

	return client;
}


static void create_services(AvahiClient *c)
{
	int ret;

	if (!c) {
		IIO_ERROR("create_services called with no valid client\n");
		goto fail;
	}

	if (!avahi.group) {
		avahi.group = avahi_entry_group_new(c, __avahi_group_cb, NULL);
		if (!avahi.group) {
			IIO_ERROR("avahi_entry_group_new() failed: %s\n",
					avahi_strerror(avahi_client_errno(c)));
			goto fail;
		}
	}

	if (!avahi_entry_group_is_empty(avahi.group)) {
		IIO_DEBUG("Avahi group not empty\n");
		return;
	}

	ret = avahi_entry_group_add_service(avahi.group,
			AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
			0, avahi.name, "_iio._tcp", NULL, NULL, avahi.port, NULL);
	if (ret < 0) {
		if (ret == AVAHI_ERR_COLLISION) {
			char *n;
			n = avahi_alternative_service_name(avahi.name);
			avahi_free(avahi.name);
			avahi.name = n;
			IIO_DEBUG("Service name collision, renaming service to '%s'\n",
					avahi.name);
			avahi_entry_group_reset(avahi.group);
			create_services(c);
			return;
		}
		IIO_ERROR("Failed to add _iio._tcp service: %s\n", avahi_strerror(ret));
		goto fail;
	}

	ret = avahi_entry_group_commit(avahi.group);
	if (ret < 0) {
		IIO_ERROR("Failed to commit entry group: %s\n", avahi_strerror(ret));
		goto fail;
	}

	IIO_INFO("Avahi: Registered '%s:%hu' to ZeroConf server %s\n",
			avahi.name, avahi.port, avahi_client_get_version_string(c));

	return;

fail:
	avahi_entry_group_reset(avahi.group);
}

#define IIOD_ON "iiod on "
#define TIMEOUT 20

static void start_avahi_thd(struct thread_pool *pool, void *d)
{
	char label[AVAHI_LABEL_MAX];
	char host[AVAHI_LABEL_MAX - sizeof(IIOD_ON)];
	struct timespec ts;
	int ret, net = 0;

	ts.tv_nsec = 0;
	ts.tv_sec = 1;

	/*
	 * Try to make sure the network is up before letting avahi
	 * know we are here, and advertising on the network.
	 * However, if we are on the last try before we timeout,
	 * ignore some prerequisites, and just assume it will be
	 * OK later (if someone boots, and later plugs in USB <-> ethernet).
	 */
	while(true) {
		struct ifaddrs *ifaddr = 0;
		struct ifaddrs *ifa = 0;

		if (!net && ts.tv_sec < TIMEOUT) {
			/* Ensure networking is alive */
			ret = getifaddrs(&ifaddr);
			if (ret)
				goto again;

			/* Make sure at least one practical interface is up and ready */
			for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
				/* no address */
				if (!ifa->ifa_addr)
					continue;
				/* Interface is running, think ifup */
				if (!(ifa->ifa_flags & IFF_UP))
					continue;
				/* supports multicast (i.e. MDNS) */
				if (!(ifa->ifa_flags & IFF_MULTICAST))
					continue;
				/* Interface is not a loopback interface */
				if ((ifa->ifa_flags & IFF_LOOPBACK))
					continue;
				IIO_INFO("found applicable network for mdns on %s\n", ifa->ifa_name);
				net++;
			}
			freeifaddrs(ifaddr);
			if (!net)
				goto again;
		}

		/* Get the hostname, which on uClibc, can return (none)
		 * rather than fail/zero length string, like on glibc */
		ret = gethostname(host, sizeof(host));
		if (ret || !host[0] || (ts.tv_sec < TIMEOUT && (!strcmp(host, "none") ||
				!strcmp(host, "(none)"))))
			goto again;

		snprintf(label, sizeof(label), "%s%s", IIOD_ON, host);

		if (!avahi.name)
			avahi.name = avahi_strdup(label);
		if (!avahi.name)
			break;

		if (!avahi.poll)
			avahi.poll = avahi_threaded_poll_new();
		if (!avahi.poll) {
			goto again;
		}

		if (!avahi.client)
			avahi.client = client_new();
		if (avahi.client)
			break;
again:
		IIO_INFO("Avahi didn't start, try again in %ld seconds later\n", ts.tv_sec);
		nanosleep(&ts, NULL);
		ts.tv_sec++;
		/* If it hasn't started in 20 times over 210 seconds (3.5 min),
		 * it is not going to, so stop
		 */
		if (ts.tv_sec > TIMEOUT)
			break;
	}

	if (avahi.client && avahi.poll) {
		avahi_threaded_poll_start(avahi.poll);
		IIO_INFO("Avahi: Started.\n");
	} else  {
		shutdown_avahi();
		IIO_INFO("Avahi: Failed to start.\n");
	}
}

void start_avahi(struct thread_pool *pool, uint16_t port)
{
	int ret;
	char err_str[1024];

	IIO_INFO("Attempting to start Avahi\n");

	avahi.poll = NULL;
	avahi.client = NULL;
	avahi.group = NULL;
	avahi.name = NULL;
	avahi.port = port;

	/* In case dbus, or avahi daemon are not started, we create a thread
	 * that tries a few times to attach, if it can't within the first
	 * minute, it gives up.
	 */
	ret = thread_pool_add_thread(pool, start_avahi_thd, NULL, "avahi_thd");
	if (ret) {
		iio_strerror(ret, err_str, sizeof(err_str));
		IIO_ERROR("Failed to create new Avahi thread: %s\n",
				err_str);
	}
}

void stop_avahi(void)
{
	shutdown_avahi();
	IIO_INFO("Avahi: Stopped\n");
}
