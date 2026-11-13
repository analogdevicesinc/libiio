// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Analog Devices, Inc.
//
// Probe: DAQIRI socket-engine init/shutdown. Set PROBE_TWICE=1 to repeat.
#include <daqiri/daqiri.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static const char *yaml =
	"daqiri:\n"
	"  cfg:\n"
	"    version: 1\n"
	"    stream_type: socket\n"
	"    master_core: 0\n"
	"    memory_regions:\n"
	"    - name: HOST_MR\n"
	"      kind: host\n"
	"      affinity: 0\n"
	"      num_bufs: 64\n"
	"      buf_size: 9216\n"
	"    interfaces:\n"
	"    - name: data\n"
	"      address: 127.0.0.1\n"
	"      socket_config:\n"
	"        mode: server\n"
	"        local_addr: 'udp://127.0.0.1:25101'\n"
	"        remote_addr: 'udp://127.0.0.1:25102'\n"
	"        max_payload_size: 9216\n"
	"      rx:\n"
	"        queues:\n"
	"        - name: rx0\n"
	"          id: 0\n"
	"          cpu_core: 0\n"
	"          batch_size: 1\n"
	"          memory_regions:\n"
	"          - HOST_MR\n";

int main()
{
	int rounds = std::getenv("PROBE_TWICE") ? 2 : 1;

	for (int i = 0; i < rounds; i++) {
		auto st = daqiri::daqiri_init(std::string(yaml));
		if (st != daqiri::Status::SUCCESS) {
			std::fprintf(stderr, "round %d: daqiri_init failed (%d)\n",
				     i, (int)st);
			return 1;
		}
		std::printf("round %d: port id = %d\n", i, daqiri::get_port_id("data"));
		daqiri::shutdown();
		std::printf("round %d: shutdown ok\n", i);
	}
	return 0;
}
