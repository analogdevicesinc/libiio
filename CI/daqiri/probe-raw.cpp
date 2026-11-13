// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Analog Devices, Inc.
//
// Probe: DAQIRI raw-engine init, optional RX poll.
// Usage: daqiri-probe-raw <yaml> [--seconds N] [--port <name>] [--queue <id>]
// Prints "engine=<kind> ports=<n> init=ok" (exit 0) or "init=<status>" (exit 1).
// With --seconds > 0, polls get_rx_burst on the port/queue and prints
// "packets=<n> bytes=<n>" once per second.
#include <daqiri/daqiri.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static const char *status_name(daqiri::Status st)
{
	switch (st) {
	case daqiri::Status::SUCCESS: return "ok";
	case daqiri::Status::NULL_PTR: return "NULL_PTR";
	case daqiri::Status::NO_FREE_BURST_BUFFERS: return "NO_FREE_BURST_BUFFERS";
	case daqiri::Status::NO_FREE_PACKET_BUFFERS: return "NO_FREE_PACKET_BUFFERS";
	case daqiri::Status::NOT_READY: return "NOT_READY";
	case daqiri::Status::INVALID_PARAMETER: return "INVALID_PARAMETER";
	case daqiri::Status::NO_SPACE_AVAILABLE: return "NO_SPACE_AVAILABLE";
	case daqiri::Status::NOT_SUPPORTED: return "NOT_SUPPORTED";
	case daqiri::Status::GENERIC_FAILURE: return "GENERIC_FAILURE";
	case daqiri::Status::CONNECT_FAILURE: return "CONNECT_FAILURE";
	case daqiri::Status::INTERNAL_ERROR: return "INTERNAL_ERROR";
	case daqiri::Status::RESOURCE_IN_USE: return "RESOURCE_IN_USE";
	case daqiri::Status::ALREADY_EXISTS: return "ALREADY_EXISTS";
	}
	return "UNKNOWN";
}

static int usage(const char *prog)
{
	std::fprintf(stderr,
		     "usage: %s <yaml> [--seconds N] [--port <name>] [--queue <id>]\n",
		     prog);
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage(argv[0]);

	const std::string yaml = argv[1];
	int seconds = 0, queue = 0;
	std::string port_name;

	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
			seconds = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--port") && i + 1 < argc)
			port_name = argv[++i];
		else if (!std::strcmp(argv[i], "--queue") && i + 1 < argc)
			queue = std::atoi(argv[++i]);
		else
			return usage(argv[0]);
	}

	daqiri::NetworkConfig cfg;
	auto st = daqiri::parse_network_config_from_yaml_file(yaml, cfg);
	if (st == daqiri::Status::SUCCESS)
		st = daqiri::daqiri_init(cfg);
	if (st != daqiri::Status::SUCCESS) {
		std::printf("init=%s\n", status_name(st));
		return 1;
	}

	std::printf("engine=%s ports=%zu init=ok\n",
		    daqiri::engine_type_to_string(cfg.common_.engine_type).c_str(),
		    cfg.ifs_.size());
	std::fflush(stdout);

	int rc = 0;
	if (seconds > 0) {
		int port = 0;
		if (!port_name.empty())
			port = daqiri::get_port_id(port_name);
		if (port < 0) {
			std::fprintf(stderr, "unknown port '%s'\n", port_name.c_str());
			rc = 1;
			seconds = 0;
		}
		using clk = std::chrono::steady_clock;
		auto next = clk::now() + std::chrono::seconds(1);
		for (int s = 0; s < seconds;) {
			uint64_t packets = 0, bytes = 0;
			while (clk::now() < next) {
				daqiri::BurstParams *burst = nullptr;
				if (daqiri::get_rx_burst(&burst, port, queue) !=
					    daqiri::Status::SUCCESS ||
				    !burst) {
					std::this_thread::sleep_for(
						std::chrono::microseconds(50));
					continue;
				}
				int64_t n = daqiri::get_num_packets(burst);
				for (int64_t k = 0; k < n; k++)
					bytes += daqiri::get_packet_length(burst, (int)k);
				packets += n;
				daqiri::free_all_packets_and_burst_rx(burst);
			}
			std::printf("packets=%llu bytes=%llu\n",
				    (unsigned long long)packets,
				    (unsigned long long)bytes);
			std::fflush(stdout);
			next += std::chrono::seconds(1);
			s++;
		}
	}

	daqiri::shutdown();
	return rc;
}
