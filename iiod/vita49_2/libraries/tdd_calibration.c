/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 *
 */

// This script offers a template for how a library file can be written to execute a sequence of commands
// via the VITA subsystem within iiod. 

// This example is based on this article: https://wiki.analog.com/university/tools/pluto/hacking/power_amp

#include <vita49_2/vita49_2_packet_elements.h>
#include "../vita49_2_iiod_helpers.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define PLUGIN_NAME "tdd_calibration" // Corresponds to the name of the plugin that can be referenced in a Control Extension Packet

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct _command 
 * @brief Contains information about a command that is to be executed.
 * 
 */
struct _command {
    // Metadata about the new attribute value
    enum vita49_2_control_extension_data_types data_type;
	union {
		long long ll;
		double d;
		uint32_t u32;
		float f;
		bool b;
	} data;

    // Some attributes have string values, hence I have a separate attribute for that since it can be unsafe
    // to place a char pointer inside the data union.
    char* string_data;


    // Metadata about the parameter
    char* device_name;
    char* channel_name;
    char* attribute_name;
    bool is_output;
};

// Initialize your command sequence here
struct _command sequence[] = {
    {
        .data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B,
        .data.b = false,
        .string_data = NULL,
        .device_name = "ad9361-phy",
        .channel_name = "debug",
        .attribute_name = "adi,frequency-division-duplex-mode-enable",
        .is_output = false
    },
    {
        .data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B,
        .data.b = true,
        .string_data = NULL,
        .device_name = "ad9361-phy",
        .channel_name = "debug",
        .attribute_name = "adi,gpo0-slave-rx-enable",
        .is_output = false
    },
    {
        .data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B,
        .data.b = true,
        .string_data = NULL,
        .device_name = "ad9361-phy",
        .channel_name = "debug",
        .attribute_name = "adi,gpo1-slave-tx-enable",
        .is_output = false
    },
    {
        .data_type = VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B,
        .data.b = true,
        .string_data = NULL,
        .device_name = "ad9361-phy",
        .channel_name = "debug",
        .attribute_name = "initialize ",
        .is_output = false
    },
};

/**
 * @brief Returns the plugin name associated with this command sequence library. It's used during startup of the VITA 49.2 subsystem when
 * all of the libraries are loaded.
 * 
 * @return char* 
 */
__attribute__((visibility("default")))
char* get_plugin_name()
{
    char* tmp = malloc(sizeof(PLUGIN_NAME));
    
    if (tmp == NULL)
        return NULL;
    
    memcpy(tmp, PLUGIN_NAME, sizeof(PLUGIN_NAME));
    return tmp;
}

/**
 * @brief Validates the commands in the sequence.
 * 
 * This function returns as soon as the first warning is discovered in the sequence, meaning any commands following the command
 * that raised the warning are not validated.
 * 
 * Returns the bit number associated with a particular warning based on Table 8.4.1.2.1-1 from the VITA 49.2 2017 documentation.
 * If no warning is generated, 32 is returned.
 * 
 * @param ctx
 * @return int 
 */
__attribute__((visibility("default")))
int validate_sequence(struct iio_context *ctx)
{
    if (ctx == NULL)
        return -EBADARGS;

    int warning;

    for (size_t command = 0; command < sizeof(sequence)/sizeof(sequence[0]); command++)
    {
        // Field validity checks
        if (sequence[command].device_name == NULL || sequence[command].channel_name == NULL || sequence[command].attribute_name == NULL)
            return -EBADARGS;

        if (sequence[command].data_type == VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S)
            if (sequence[command].string_data == NULL)
                return -EBADARGS;

        // New attribute value checks
        switch (sequence[command].data_type)
        {
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
                if ((warning = validate_command_ll(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.ll)) < 0)
                    return warning;

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:
                if ((warning = validate_command_double_h(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.f)) < 0)
                    return warning;

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
                if ((warning = validate_command_ll(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.d)) < 0)
                    return warning;

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
                // Doesn't need validation since there's only 2 possible values and the data.b field MUST be initialized to either
                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:
                if ((warning = validate_command_s(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].string_data)) < 0)
                    return warning;

                break;
        }
    }

    return 32;
}

/**
 * @brief Executes the commands in the sequence.
 * 
 * This function returns as soon as the first execution error is generated in the sequence, meaning any commands following the command
 * that raised the warning are not executed.
 * 
 * Returns the bit number associated with a particular warning based on Table 8.4.1.2.1-1 from the VITA 49.2 2017 documentation.
 * If no warning is generated and all commands are executed, 32 is returned.
 * 
 * @param ctx  
 * @return int 
 */
__attribute__((visibility("default")))
int execute_sequence(struct iio_context *ctx)
{
    if (ctx == NULL)
        return -EBADARGS;

    int error;
    struct iio_device *dev;
	struct iio_channel *chn;
	const struct iio_attr *attr;

    for (size_t command = 0; command < sizeof(sequence)/sizeof(sequence[0]); command++)
    {
        // Field validity checks
        if (sequence[command].device_name == NULL || sequence[command].channel_name == NULL || sequence[command].attribute_name == NULL)
            return -EBADARGS;

        if (sequence[command].data_type == VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S)
            if (sequence[command].string_data == NULL)
                return -EBADARGS;


        // Finding the IIO device
        dev = iio_context_find_device(ctx, sequence[command].device_name);
		if (!dev) 
		{
            return -ENOFIELD;
        }

		attr = NULL;

		// Attribute we're modifying is associated with the device as a whole
        if (strcmp(sequence[command].channel_name, "device") == 0)
		{
			attr = iio_device_find_attr(dev, sequence[command].attribute_name);
		} 
		// Attribute we're modifying is a debug attribute (advanced configuration)
        else if (strcmp(sequence[command].channel_name, "debug") == 0)
        {
			attr = iio_device_find_debug_attr(dev, sequence[command].attribute_name);
        }        
		// Attribute we're modifying is associated with a specific channel so we must find that channel first.
        else
		{
			chn = iio_device_find_channel(dev, sequence[command].channel_name, sequence[command].is_output);
			if (!chn) 
			{
                return -ENOFIELD;
            }

			attr = iio_channel_find_attr(chn, sequence[command].attribute_name);
		} 

		if (!attr) 
		{
            return -ENOFIELD;
        }


        // Updating the IIO device value
        switch (sequence[command].data_type)
        {
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
            {
                printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %lld\n", sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.ll);
                error = iio_attr_write_longlong(attr, sequence[command].data.ll);
                if (error < 0)
                {
                    fprintf(stderr, "vita49_2_process: Failed to write %lld to %s\n", sequence[command].data.ll, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return -EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:
            {
                printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %.5f\n", sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.f);
                error = iio_attr_write_double(attr, (double)(sequence[command].data.f));
                if (error < 0)
                {
                    fprintf(stderr, "vita49_2_process: Failed to write %.5f to %s\n", sequence[command].data.f, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return -EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
            {
                printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %.5lf\n", sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.d);
                error = iio_attr_write_double(attr, sequence[command].data.d);
                if (error < 0)
                {
                    fprintf(stderr, "vita49_2_process: Failed to write %lf to %s\n", sequence[command].data.d, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return -EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }

                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
            {
                printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %d\n", sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.b);
                error = iio_attr_write_bool(attr, sequence[command].data.b);
                if (error < 0)
                {
                    fprintf(stderr, "vita49_2_process: Failed to write %d to %s\n", sequence[command].data.b, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return -EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:
            {
                printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %s\n", sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].string_data);
                error = iio_attr_write_string(attr, sequence[command].string_data);
                if (error < 0)
                {
                    fprintf(stderr, "vita49_2_process: Failed to write %s to %s\n", sequence[command].data.b, sequence[command].string_data);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return -EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
        }
    }
}


#ifdef __cplusplus
}
#endif