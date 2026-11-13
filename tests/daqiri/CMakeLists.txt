# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT

set(XMLS ${CMAKE_SOURCE_DIR}/tests/resources/xmls)

# Loopback tests: a daqiri: context against iiod-emu on 127.0.0.1:30431.
if (WITH_DAQIRI_BACKEND AND WITH_IIOD_EMU)
    # libiio looks modules up in IIO_MODULES_DIR, which is baked in at configure
    # time; without the override these tests would load whatever is installed.
    if (NOT IIO_MODULES_DIR_OVERRIDE)
        message(
            WARNING
                "daqiri loopback tests need -DIIO_MODULES_DIR_OVERRIDE=${CMAKE_BINARY_DIR}/ "
                "to load the modules from the build tree")
    endif ()

    # With libiio-emu.so as a module, iiod-emu crashes on attribute writes
    # (the module calls the non-exported iio_strndup).
    if (WITH_EMU_BACKEND_DYNAMIC)
        message(
            WARNING
                "daqiri loopback tests need -DWITH_EMU_BACKEND_DYNAMIC=OFF: "
                "iiod-emu crashes on attribute writes when emu is a module")
    endif ()

    add_executable(test_daqiri_context test_daqiri_context.c)
    target_link_libraries(test_daqiri_context PRIVATE iio)
    target_include_directories(test_daqiri_context
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    # The test only links libiio; make sure the module and the emulator it
    # exercises are rebuilt before it runs.
    add_dependencies(test_daqiri_context iio-daqiri iiod-emu)

    add_test(NAME daqiri_context
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh
                     ${XMLS}/fmcomms2.xml $<TARGET_FILE:test_daqiri_context>)
    set_tests_properties(
        daqiri_context
        PROPERTIES LABELS daqiri-loopback ENVIRONMENT
                   "IIOD_EMU=$<TARGET_FILE:iiod-emu>" RESOURCE_LOCK
                   iiod-emu-30431)

    # The buffer attributes of a daqiri: context against ip:'s: the server's
    # read and write the same through both, the daqiri_* ones are found by
    # name and served locally.
    add_executable(test_daqiri_buffer_attrs test_daqiri_buffer_attrs.c)
    target_link_libraries(test_daqiri_buffer_attrs PRIVATE iio)
    target_include_directories(test_daqiri_buffer_attrs
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_daqiri_buffer_attrs PRIVATE TESTS_DEBUG)
    add_dependencies(test_daqiri_buffer_attrs iio-daqiri iiod-emu)

    add_test(
        NAME daqiri_buffer_attrs
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_buffer_attrs>)
    # IIO_DAQIRI_YAML is cleared: the test needs no YAML.
    set_tests_properties(
        daqiri_buffer_attrs
        PROPERTIES LABELS
                   daqiri-loopback
                   ENVIRONMENT
                   "IIOD_EMU=$<TARGET_FILE:iiod-emu>;IIO_DAQIRI_YAML="
                   RESOURCE_LOCK
                   iiod-emu-30431
                   TIMEOUT
                   60)

    # iiod-emu fills a block from its .bin with at most one wrap, so a .bin
    # shorter than a block leaves the end of the block unwritten. Serve a copy
    # of fmcomms2.xml next to the stock cf-ad9361-lpc_buf0.bin (2019 bytes)
    # three times over, which fills the 4096-byte blocks of the test.
    set(EQ_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/equivalence)
    add_custom_command(
        OUTPUT ${EQ_FIXTURE}/fmcomms2.xml ${EQ_FIXTURE}/cf-ad9361-lpc_buf0.bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EQ_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E copy ${XMLS}/fmcomms2.xml
                ${EQ_FIXTURE}/fmcomms2.xml
        COMMAND
            sh -c "cat \"$0\" \"$0\" \"$0\" > \"$1\""
            ${XMLS}/cf-ad9361-lpc_buf0.bin ${EQ_FIXTURE}/cf-ad9361-lpc_buf0.bin
        DEPENDS ${XMLS}/fmcomms2.xml ${XMLS}/cf-ad9361-lpc_buf0.bin
        VERBATIM)
    add_custom_target(
        daqiri_equivalence_fixture DEPENDS ${EQ_FIXTURE}/fmcomms2.xml
                                           ${EQ_FIXTURE}/cf-ad9361-lpc_buf0.bin)

    find_package(Threads REQUIRED)
    add_executable(test_block_equivalence test_block_equivalence.c)
    target_link_libraries(test_block_equivalence PRIVATE iio Threads::Threads)
    target_include_directories(test_block_equivalence
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(
        test_block_equivalence
        PRIVATE TESTS_DEBUG TESTS_BUF0_BIN="${XMLS}/cf-ad9361-lpc_buf0.bin")
    add_dependencies(test_block_equivalence iio-daqiri iiod-emu
                     daqiri_equivalence_fixture)

    add_test(
        NAME daqiri_block_equivalence
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh ${EQ_FIXTURE}/fmcomms2.xml
            $<TARGET_FILE:test_block_equivalence>)
    # IIO_DAQIRI_YAML is cleared: its data=auto case expects no YAML.
    set_tests_properties(
        daqiri_block_equivalence
        PROPERTIES LABELS daqiri-loopback ENVIRONMENT
                   "IIOD_EMU=$<TARGET_FILE:iiod-emu>;IIO_DAQIRI_YAML="
                   RESOURCE_LOCK iiod-emu-30431)

    # Opens a buffer through TESTS_API_URI; run_loopback.sh runs it per XML so
    # that the api label cannot pass on buffers that never opened.
    add_executable(test_daqiri_open test_daqiri_open.c)
    target_link_libraries(test_daqiri_open PRIVATE iio)
    target_include_directories(test_daqiri_open
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_daqiri_open PRIVATE TESTS_DEBUG)
    add_dependencies(test_daqiri_open iio-daqiri iiod-emu)

    # data=udp buffer lifecycle over the DAQIRI socket engine, against
    # iiod-emu's mock UDP provider (built on Linux only), which answers the
    # setup and never streams. The engine binds UDP 127.0.0.1:25101 (see the
    # YAML), hence the second lock.
    if (IIOD_EMU_UDP)
        add_executable(test_daqiri_lifecycle test_daqiri_lifecycle.c)
        target_link_libraries(test_daqiri_lifecycle PRIVATE iio
                                                            Threads::Threads)
        target_include_directories(test_daqiri_lifecycle
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_daqiri_lifecycle PRIVATE TESTS_DEBUG)
        add_dependencies(test_daqiri_lifecycle iio-daqiri iiod-emu)

        set(LIFECYCLE_LOG ${CMAKE_CURRENT_BINARY_DIR}/daqiri_lifecycle.log)
        add_test(
            NAME daqiri_lifecycle
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${LIFECYCLE_LOG} -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_lifecycle>)

        # The device's own layout: a device with 16- and 32-bit channels
        set(LIFECYCLE_MIXED_LOG
            ${CMAKE_CURRENT_BINARY_DIR}/daqiri_lifecycle_mixed.log)
        add_test(
            NAME daqiri_lifecycle_mixed
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${LIFECYCLE_MIXED_LOG} --
                ${XMLS}/float_scan_elements.xml
                $<TARGET_FILE:test_daqiri_lifecycle> --mixed)

        # The reference server sending from another address than the YAML's
        # remote_addr, where the socket engine receives from
        add_test(
            NAME daqiri_lifecycle_wrong_source
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args --udp
                --udp-from 127.0.0.1:25109 -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_lifecycle> --wrong-source)

        # IIO_DAQIRI_YAML is cleared: some cases expect no YAML.
        set_tests_properties(
            daqiri_lifecycle daqiri_lifecycle_mixed
            daqiri_lifecycle_wrong_source
            PROPERTIES
                LABELS
                daqiri-loopback
                ENVIRONMENT
                "IIOD_EMU=$<TARGET_FILE:iiod-emu>;DAQIRI_YAML=${CMAKE_CURRENT_SOURCE_DIR}/resources/socket-udp-loopback.yaml;IIO_DAQIRI_YAML="
                RESOURCE_LOCK
                "iiod-emu-30431;daqiri-udp-25101")
        # The decision table and the refused enable read the mock's calls
        set_property(
            TEST daqiri_lifecycle
            APPEND
            PROPERTY ENVIRONMENT "DAQIRI_MOCK_LOG=${LIFECYCLE_LOG}")

        # A failed ENABLE_BUFFER, and a DISABLE_BUFFER that fails once, then
        # twice, against the mock with --udp-mock-fail-disable <n>, which also
        # logs every DISABLE_BUFFER. The failed enable's YAML asks for payloads
        # whose stream the mock fails to start.
        set(LIFECYCLE_ENABLE_LOG
            ${CMAKE_CURRENT_BINARY_DIR}/daqiri_lifecycle_enable_fails.log)
        add_test(
            NAME daqiri_lifecycle_enable_fails
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${LIFECYCLE_ENABLE_LOG}
                --udp-mock-fail-disable 0 -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_lifecycle> --enable-fails
                ${CMAKE_CURRENT_SOURCE_DIR}/resources/socket-udp-loopback-1250.yaml
        )
        set(LIFECYCLE_DISABLE_ONCE_LOG
            ${CMAKE_CURRENT_BINARY_DIR}/daqiri_lifecycle_disable_fails_once.log)
        add_test(
            NAME daqiri_lifecycle_disable_fails_once
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${LIFECYCLE_DISABLE_ONCE_LOG}
                --udp-mock-fail-disable 1 -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_lifecycle> --disable-fails-once)
        set(LIFECYCLE_DISABLE_TWICE_LOG
            ${CMAKE_CURRENT_BINARY_DIR}/daqiri_lifecycle_disable_fails_twice.log
        )
        add_test(
            NAME daqiri_lifecycle_disable_fails_twice
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${LIFECYCLE_DISABLE_TWICE_LOG}
                --udp-mock-fail-disable 2 -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_daqiri_lifecycle> --disable-fails-twice)
        set_tests_properties(
            daqiri_lifecycle_enable_fails daqiri_lifecycle_disable_fails_once
            daqiri_lifecycle_disable_fails_twice
            PROPERTIES
                LABELS
                daqiri-loopback
                ENVIRONMENT
                "IIOD_EMU=$<TARGET_FILE:iiod-emu>;DAQIRI_YAML=${CMAKE_CURRENT_SOURCE_DIR}/resources/socket-udp-loopback.yaml;IIO_DAQIRI_YAML="
                RESOURCE_LOCK
                "iiod-emu-30431;daqiri-udp-25101"
                TIMEOUT
                60)
        set_property(
            TEST daqiri_lifecycle_enable_fails
            APPEND
            PROPERTY ENVIRONMENT "DAQIRI_MOCK_LOG=${LIFECYCLE_ENABLE_LOG}")
        set_property(
            TEST daqiri_lifecycle_disable_fails_once
            APPEND
            PROPERTY ENVIRONMENT
                     "DAQIRI_MOCK_LOG=${LIFECYCLE_DISABLE_ONCE_LOG}")
        set_property(
            TEST daqiri_lifecycle_disable_fails_twice
            APPEND
            PROPERTY ENVIRONMENT
                     "DAQIRI_MOCK_LOG=${LIFECYCLE_DISABLE_TWICE_LOG}")
    endif ()

    # UDP sample mode end to end: the backend through the DAQIRI socket
    # engine against iiod-emu --udp, the reference server (built on Linux
    # only), and against the plain emulator, which does not advertise it.
    if (IIOD_EMU_UDP)
        # What the exact-counter tests stream: a copy of fmcomms2.xml next to
        # a sample file of 4099 letters and digits, no whole number of
        # samples, blocks or payloads, whose 1456-byte payloads all differ
        # (the stock file repeats itself, so a block would not tell which
        # datagram it holds), received with 1456-byte payloads, of which even
        # the kernel's default receive buffer (rcvbuf=0) holds some 90
        set(UDP_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/udp-loopback)
        string(
            RANDOM
            LENGTH 4099
            RANDOM_SEED 2026 _udp_samples)
        file(WRITE ${UDP_FIXTURE}/cf-ad9361-lpc_buf0.bin "${_udp_samples}")
        configure_file(${XMLS}/fmcomms2.xml ${UDP_FIXTURE}/fmcomms2.xml
                       COPYONLY)
        set(UDP_LOOPBACK_YAML
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/socket-udp-loopback-1472.yaml)

        # The blocks and counters against the emulator's oracle, case by
        # case, each against an emulator with its own flags, which the test
        # starts through with_emu.sh
        add_executable(test_udp_loopback test_udp_loopback.c)
        target_link_libraries(test_udp_loopback PRIVATE iio)
        target_include_directories(test_udp_loopback
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_udp_loopback PRIVATE TESTS_DEBUG)
        add_dependencies(test_udp_loopback iio-daqiri iiod-emu)

        set(UDP_LOOPBACK_ORACLE ${CMAKE_CURRENT_BINARY_DIR}/udp_loopback.json)
        add_test(
            NAME daqiri_udp_loopback
            COMMAND
                test_udp_loopback ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh
                ${UDP_FIXTURE}/fmcomms2.xml
                ${UDP_FIXTURE}/cf-ad9361-lpc_buf0.bin ${UDP_LOOPBACK_ORACLE})
        # A burst into a buffer that is open but not enabled, with the
        # receive buffer the backend asks for by default: nothing lost. Where
        # net.core.rmem_max does not grant room for the whole burst, it exits
        # 77, which ctest reports as skipped. (The burst_small case above
        # shows the same burst losing datagrams with rcvbuf=65536.)
        add_test(
            NAME daqiri_udp_rcvbuf_burst
            COMMAND
                test_udp_loopback ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh
                ${UDP_FIXTURE}/fmcomms2.xml
                ${UDP_FIXTURE}/cf-ad9361-lpc_buf0.bin ${UDP_LOOPBACK_ORACLE}
                burst_sized)
        set_tests_properties(
            daqiri_udp_loopback daqiri_udp_rcvbuf_burst
            PROPERTIES
                LABELS
                daqiri-loopback
                ENVIRONMENT
                "IIOD_EMU=$<TARGET_FILE:iiod-emu>;DAQIRI_YAML=${UDP_LOOPBACK_YAML};IIO_DAQIRI_YAML="
                RESOURCE_LOCK
                "iiod-emu-30431;daqiri-udp-25101"
                TIMEOUT
                120)
        set_tests_properties(daqiri_udp_rcvbuf_burst PROPERTIES SKIP_RETURN_CODE
                                                                77)

        # The same device and mask through data=tcp and data=udp: the same
        # bytes, the sample file repeated
        add_executable(test_udp_equivalence test_udp_equivalence.c)
        target_link_libraries(test_udp_equivalence PRIVATE iio)
        target_include_directories(test_udp_equivalence
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_udp_equivalence PRIVATE TESTS_DEBUG)
        add_dependencies(test_udp_equivalence iio-daqiri iiod-emu)

        set(UDP_EQ_ORACLE ${CMAKE_CURRENT_BINARY_DIR}/udp_equivalence.json)
        add_test(
            NAME daqiri_udp_equivalence
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args --udp
                --udp-pps 250 --udp-count 36 --udp-json ${UDP_EQ_ORACLE} --
                ${UDP_FIXTURE}/fmcomms2.xml $<TARGET_FILE:test_udp_equivalence>
                ${UDP_FIXTURE}/cf-ad9361-lpc_buf0.bin ${UDP_EQ_ORACLE})
        set_tests_properties(
            daqiri_udp_equivalence
            PROPERTIES
                LABELS
                daqiri-loopback
                ENVIRONMENT
                "IIOD_EMU=$<TARGET_FILE:iiod-emu>;DAQIRI_YAML=${UDP_LOOPBACK_YAML};IIO_DAQIRI_YAML="
                RESOURCE_LOCK
                "iiod-emu-30431;daqiri-udp-25101"
                TIMEOUT
                60)
    endif ()

    # Against the plain emulator, which does not advertise UDP sample mode:
    # data=auto streams over TCP and data=udp does not open, and neither
    # sends the setup (the server logs it) nor starts the engine (the
    # YAML's UDP port stays free, hence the second lock).
    add_executable(test_udp_fallback test_udp_fallback.c emu_client.c)
    target_link_libraries(test_udp_fallback PRIVATE iio)
    target_include_directories(test_udp_fallback
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_udp_fallback PRIVATE TESTS_DEBUG)
    add_dependencies(test_udp_fallback iio-daqiri iiod-emu)

    add_test(
        NAME daqiri_auto_fallback
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh ${XMLS}/fmcomms2.xml
            $<TARGET_FILE:test_udp_fallback> --auto-fallback
            ${XMLS}/cf-ad9361-lpc_buf0.bin)
    add_test(
        NAME daqiri_udp_refused
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh ${XMLS}/fmcomms2.xml
            $<TARGET_FILE:test_udp_fallback> --refused
            ${XMLS}/cf-ad9361-lpc_buf0.bin)
    set_tests_properties(
        daqiri_auto_fallback daqiri_udp_refused
        PROPERTIES
            LABELS
            daqiri-loopback
            ENVIRONMENT
            "IIOD_EMU=$<TARGET_FILE:iiod-emu>;DAQIRI_YAML=${CMAKE_CURRENT_SOURCE_DIR}/resources/socket-udp-loopback.yaml;IIO_DAQIRI_YAML="
            RESOURCE_LOCK
            "iiod-emu-30431;daqiri-udp-25101"
            TIMEOUT
            60)

    # The api label, once per XML, with TESTS_API_URI pointing at daqiri:.
    add_test(
        NAME daqiri_loopback_api
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_loopback.sh ${CMAKE_BINARY_DIR}
                $<TARGET_FILE:test_daqiri_open> ${XMLS})
    set_tests_properties(
        daqiri_loopback_api
        PROPERTIES LABELS
                   daqiri-loopback
                   ENVIRONMENT
                   "IIOD_EMU=$<TARGET_FILE:iiod-emu>"
                   RESOURCE_LOCK
                   iiod-emu-30431
                   RUN_SERIAL
                   TRUE
                   TIMEOUT
                   300)
endif ()

# The engine shim's view of a YAML, parsed only, no engine started: the UDP
# endpoint of a socket engine queue and, in daqiri_engine_raw_flows, of a raw
# queue's flow rule. Only a DAQIRI with the ibverbs engine parses the raw YAML:
# without it that test exits 77, which ctest reports as skipped, unless
# DAQIRI_REQUIRE_RAW_ENGINES=1 makes it fail. The shim is built into the test.
if (WITH_DAQIRI_BACKEND)
    add_executable(
        test_engine_udp_endpoint test_engine_udp_endpoint.c
                                 ${CMAKE_SOURCE_DIR}/daqiri-engine.cpp)
    target_compile_features(test_engine_udp_endpoint PRIVATE cxx_std_17)
    target_link_libraries(
        test_engine_udp_endpoint PRIVATE iio daqiri::daqiri CUDA::cudart
                                         Threads::Threads ${CMAKE_DL_LIBS})
    target_include_directories(test_engine_udp_endpoint
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_engine_udp_endpoint PRIVATE TESTS_DEBUG)

    add_test(NAME daqiri_engine_udp_endpoint
             COMMAND test_engine_udp_endpoint
                     ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    add_test(NAME daqiri_engine_raw_flows
             COMMAND test_engine_udp_endpoint
                     ${CMAKE_CURRENT_SOURCE_DIR}/resources --raw)
    set_tests_properties(daqiri_engine_udp_endpoint daqiri_engine_raw_flows
                         PROPERTIES LABELS daqiri-loopback TIMEOUT 60)
    set_tests_properties(daqiri_engine_raw_flows PROPERTIES SKIP_RETURN_CODE 77)
endif ()

# Emulator tests of the iiod side of UDP sample mode: the iiod-client API over a
# plain TCP socket against iiod-emu on 127.0.0.1:30431. They need neither the
# daqiri module nor DAQIRI.
if (WITH_IIOD_EMU
    AND WITH_EMU_BACKEND
    AND IIOD_CLIENT
    AND NOT WIN32)
    # iiod-emu registers no UDP stream provider: the setup command must be
    # answered with -ENOTSUP, and the connection must stay usable.
    add_executable(test_udp_setup_emu test_udp_setup_emu.c emu_client.c)
    target_link_libraries(test_udp_setup_emu PRIVATE iio)
    target_include_directories(test_udp_setup_emu
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_udp_setup_emu PRIVATE TESTS_DEBUG)
    add_dependencies(test_udp_setup_emu iiod-emu)

    add_test(NAME daqiri_udp_setup_emu
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh
                     ${XMLS}/fmcomms2.xml $<TARGET_FILE:test_udp_setup_emu>)
    set_tests_properties(
        daqiri_udp_setup_emu
        PROPERTIES LABELS
                   daqiri-emu
                   ENVIRONMENT
                   "IIOD_EMU=$<TARGET_FILE:iiod-emu>"
                   RESOURCE_LOCK
                   iiod-emu-30431
                   TIMEOUT
                   60)

    # iiod-emu --udp (built on Linux only) as the reference server: its
    # datagrams checked from a plain UDP socket bound where the loopback YAML's
    # engine listens, hence the second lock.
    if (IIOD_EMU_UDP)
        add_executable(test_udp_emu_server test_udp_emu_server.c emu_client.c)
        target_link_libraries(test_udp_emu_server PRIVATE iio)
        target_include_directories(test_udp_emu_server
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_udp_emu_server PRIVATE TESTS_DEBUG)
        add_dependencies(test_udp_emu_server iiod-emu)

        set(UDP_ORACLE ${CMAKE_CURRENT_BINARY_DIR}/udp_emu_server.json)
        add_test(
            NAME daqiri_udp_emu_server
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args --udp
                --udp-from 127.0.0.1:25102 --udp-pps 2000 --udp-drop-every 10
                --udp-json ${UDP_ORACLE} -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_udp_emu_server>
                ${XMLS}/cf-ad9361-lpc_buf0.bin ${UDP_ORACLE})

        # --udp-count: the stream ends by itself, the oracle written then
        set(UDP_COUNT_ORACLE ${CMAKE_CURRENT_BINARY_DIR}/udp_emu_count.json)
        add_test(
            NAME daqiri_udp_emu_count
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args --udp
                --udp-pps 2000 --udp-drop-every 10 --udp-count 100 --udp-json
                ${UDP_COUNT_ORACLE} -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_udp_emu_server>
                ${XMLS}/cf-ad9361-lpc_buf0.bin ${UDP_COUNT_ORACLE} --count)

        # One datagram per second: DISABLE, CLOSE_BUFFER and a lost
        # connection must not wait for the next one.
        add_test(
            NAME daqiri_udp_emu_prompt_stop
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args --udp
                --udp-pps 1 -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_udp_emu_server>
                ${XMLS}/cf-ad9361-lpc_buf0.bin --prompt-stop)

        set_tests_properties(
            daqiri_udp_emu_server daqiri_udp_emu_count
            daqiri_udp_emu_prompt_stop
            PROPERTIES LABELS
                       daqiri-emu
                       ENVIRONMENT
                       "IIOD_EMU=$<TARGET_FILE:iiod-emu>"
                       RESOURCE_LOCK
                       "iiod-emu-30431;daqiri-udp-25101"
                       TIMEOUT
                       60)

        # The responder's rules for a UDP stream provider (iiod/udp-stream.h),
        # against iiod-emu's hidden mock provider, which logs its calls and
        # sends nothing.
        add_executable(test_udp_provider_contract test_udp_provider_contract.c
                                                  emu_client.c)
        target_link_libraries(test_udp_provider_contract PRIVATE iio)
        target_include_directories(test_udp_provider_contract
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_udp_provider_contract
                                   PRIVATE TESTS_DEBUG)
        add_dependencies(test_udp_provider_contract iiod-emu)

        set(UDP_MOCK_LOG ${CMAKE_CURRENT_BINARY_DIR}/udp_provider_contract.log)
        add_test(
            NAME daqiri_udp_provider_contract
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${UDP_MOCK_LOG} -- ${XMLS}/fmcomms2.xml
                $<TARGET_FILE:test_udp_provider_contract> ${UDP_MOCK_LOG})
        set_tests_properties(
            daqiri_udp_provider_contract
            PROPERTIES LABELS
                       daqiri-emu
                       ENVIRONMENT
                       "IIOD_EMU=$<TARGET_FILE:iiod-emu>"
                       RESOURCE_LOCK
                       iiod-emu-30431
                       TIMEOUT
                       60)

        # The same mock, answering a setup with a response tail cut short: the
        # client gives up, and the bytes that come later must not land in
        # memory it no longer owns.
        add_executable(test_udp_setup_short_tail test_udp_setup_short_tail.c
                                                 emu_client.c)
        target_link_libraries(test_udp_setup_short_tail PRIVATE iio)
        target_include_directories(test_udp_setup_short_tail
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(test_udp_setup_short_tail
                                   PRIVATE TESTS_DEBUG)
        add_dependencies(test_udp_setup_short_tail iiod-emu)

        set(UDP_SHORT_TAIL_LOG
            ${CMAKE_CURRENT_BINARY_DIR}/udp_setup_short_tail.log)
        add_test(
            NAME daqiri_udp_setup_short_tail
            COMMAND
                ${CMAKE_CURRENT_SOURCE_DIR}/with_emu.sh --emu-args
                --udp-mock-provider ${UDP_SHORT_TAIL_LOG} --
                ${XMLS}/fmcomms2.xml $<TARGET_FILE:test_udp_setup_short_tail>
                ${UDP_SHORT_TAIL_LOG})
        set_tests_properties(
            daqiri_udp_setup_short_tail
            PROPERTIES LABELS
                       daqiri-emu
                       ENVIRONMENT
                       "IIOD_EMU=$<TARGET_FILE:iiod-emu>"
                       RESOURCE_LOCK
                       iiod-emu-30431
                       TIMEOUT
                       60)
    endif ()
endif ()

# Unit tests of the framing objects (iio_daqiri_framing, top-level
# CMakeLists): libc only, they need neither libiio's daqiri module nor the
# DAQIRI SDK, so they are built whenever tests are. test_udp_header,
# test_udp_hds_split and test_l2_parse read the golden vectors.
if (WITH_TESTS AND NOT WIN32)
    # The UDP sample mode receive side (datagram header checks, the 64-bit
    # sequence tracker, datagrams cut in two by header-data split, the end of
    # the drain after DISABLE, the default socket receive buffer), the block
    # assembler and the Ethernet/IPv4/UDP parser of raw queues.
    foreach (
        _test
        test_udp_header
        test_udp_seq
        test_udp_hds_split
        test_udp_drain
        test_udp_rcvbuf
        test_asm
        test_l2_parse)
        add_executable(${_test} ${_test}.c)
        target_link_libraries(${_test} PRIVATE iio_daqiri_framing)
        target_include_directories(${_test}
                                   PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
        target_compile_definitions(
            ${_test}
            PRIVATE
                TESTS_DEBUG
                TESTS_DAQIRI_DIR="${CMAKE_SOURCE_DIR}/tests/resources/daqiri")
        set_target_properties(
            ${_test}
            PROPERTIES C_STANDARD 99
                       C_STANDARD_REQUIRED ON
                       C_EXTENSIONS OFF)
        add_test(NAME ${_test} COMMAND ${_test})
        set_tests_properties(${_test} PROPERTIES LABELS daqiri)
    endforeach ()

    # UDP sample stream codec (iiod-udp-stream.h): header-only, libc only.
    add_executable(test_udp_stream_codec test_udp_stream_codec.c)
    target_include_directories(test_udp_stream_codec
                               PRIVATE ${CMAKE_SOURCE_DIR}/tests/api)
    target_compile_definitions(test_udp_stream_codec PRIVATE TESTS_DEBUG)
    set_target_properties(
        test_udp_stream_codec
        PROPERTIES C_STANDARD 99
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    add_test(NAME test_udp_stream_codec COMMAND test_udp_stream_codec)
    set_tests_properties(test_udp_stream_codec PROPERTIES LABELS daqiri)
endif ()
