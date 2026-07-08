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
#include <stdio.h>
#include <errno.h>

#define PLUGIN_NAME "tdd_setup" // Corresponds to the name of the plugin that can be referenced in a Control Extension Packet

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct _command 
 * @brief Contains information about a command that is to be executed.
 * 
 * This structure is internal to the library so this template can be modified.
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
        .attribute_name = "initialize",
        .is_output = false
    },
};

/**
 * @brief Returns a pointer to heap allocated memory containing the plugin name associated with this command sequence library. 
 * It's used during startup of the VITA 49.2 subsystem when all of the libraries are loaded.
 * 
 * This function interface (name + arguments) can NOT be modified as it is part of the standard interface that the VITA 49.2 subsystem expects.
 * 
 * @return char* 
 */
__attribute__((visibility("default"))) char* get_plugin_name()
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
 * Returns a positive bit number associated with a particular warning based on Table 8.4.1.2.1-1 from the VITA 49.2 2017 documentation.
 * See the vita49_2_warnings_error_codes enum definition in vita49_2_packet_elements.h
 * If no warning is generated and all commands are executed, ENONE (0) is returned.
 *  
 * This function interface (name + arguments) can NOT be modified as it is part of the standard interface that the VITA 49.2 subsystem expects. * @param ctx
 *
 * @return int 
 */
__attribute__((visibility("default"))) enum vita49_2_warnings_error_codes validate_sequence(const struct iio_context* const ctx)
{
    if (ctx == NULL)
        return EBADARGS;

    int warning = ENONE;

    for (size_t command = 0; command < sizeof(sequence)/sizeof(sequence[0]); command++)
    {
        // Field validity checks
        if (sequence[command].device_name == NULL || sequence[command].channel_name == NULL || sequence[command].attribute_name == NULL)
            return EBADARGS;

        if (sequence[command].data_type == VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S && sequence[command].string_data == NULL)
        {
            fprintf(stderr, "%s validate_sequence(): Command with string attribute has null pointer to string data.\n", PLUGIN_NAME);
            return EBADARGS;
        }

        // New attribute value checks
        switch (sequence[command].data_type)
        {
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
                warning = validate_command_ll(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.ll);

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:
                warning = validate_command_double_h(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.f);

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
                warning = validate_command_ll(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].data.d);

                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
                // Doesn't need validation since there's only 2 possible values and the data.b field MUST be initialized to either
                break;

            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:
                warning = validate_command_s(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, sequence[command].string_data);

                break;
        }
    }

    return warning;
}

/**
 * @brief Executes the commands in the sequence.
 * 
 * This function returns as soon as the first execution error is generated in the sequence, meaning any commands following the command
 * that raised the warning are not executed.
 * 
 * Returns the positive bit number associated with a particular warning based on Table 8.4.1.2.1-1 from the VITA 49.2 2017 documentation.
 * See the vita49_2_warnings_error_codes enum definition in vita49_2_packet_elements.h
 * If no warning is generated and all commands are executed, ENONE (0) is returned.
 * 
 * This function interface (name + arguments) can NOT be modified as it is part of the standard interface that the VITA 49.2 subsystem expects.
 * 
 * @param ctx  
 * @return vita49_2_warnings_error_codes 
 */
__attribute__((visibility("default"))) enum vita49_2_warnings_error_codes execute_sequence(const struct iio_context* const ctx)
{
    if (ctx == NULL)
        return EBADARGS;

    int error;
	const struct iio_attr *attr;

    for (size_t command = 0; command < sizeof(sequence)/sizeof(sequence[0]); command++)
    {
        // Field validity checks
        if (sequence[command].device_name == NULL || sequence[command].channel_name == NULL || sequence[command].attribute_name == NULL)
        {
            fprintf(stderr, "%s execute_sequence(): Bad arguments provided.\n", PLUGIN_NAME);
            return EBADARGS;
        }

        if (sequence[command].data_type == VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S && sequence[command].string_data == NULL)
        {
            fprintf(stderr, "%s execute_sequence(): Command with string attribute has null pointer to string data.\n", PLUGIN_NAME);
            return EBADARGS;
        }

        // Finding the IIO device
        if ((error = vita49_2_find_iio_attribute(ctx, sequence[command].device_name, sequence[command].channel_name, sequence[command].attribute_name, sequence[command].is_output, attr)) < 0)
        {
            fprintf(stderr, "%s execute_sequence(): Failed to execute command. IIO attribute '%s' could not be found.\n", PLUGIN_NAME, sequence[command].attribute_name);
            return -error;
        }

        // Updating the IIO device value
        switch (sequence[command].data_type)
        {
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
            {
                printf("%s: Executing attribute update: %s %s (%s) -> %lld\n", PLUGIN_NAME, sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.ll);
                if ((error = iio_attr_write_longlong(attr, sequence[command].data.ll)) < 0)
                {
                    fprintf(stderr, "%s execute_sequence(): Failed to write %lld to %s\n", PLUGIN_NAME, sequence[command].data.ll, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (abs(error) > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:
            {
                printf("%s: Executing attribute update: %s %s (%s) -> %.5f\n", PLUGIN_NAME, sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.f);
                error = iio_attr_write_double(attr, (double)(sequence[command].data.f));
                if (error < 0)
                {
                    fprintf(stderr, "%s execute_sequence(): Failed to write %.5f to %s\n", PLUGIN_NAME, sequence[command].data.f, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (abs(error) > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
            {
                printf("%s: Executing attribute update: %s %s (%s) -> %.5lf\n", PLUGIN_NAME, sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.d);
                error = iio_attr_write_double(attr, sequence[command].data.d);
                if (error < 0)
                {
                    fprintf(stderr, "%s execute_sequence(): Failed to write %lf to %s\n", PLUGIN_NAME, sequence[command].data.d, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (abs(error) > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }

                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
            {
                printf("%s: Executing attribute update: %s %s (%s) -> %d\n", PLUGIN_NAME, sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].data.b);
                error = iio_attr_write_bool(attr, sequence[command].data.b);
                if (error < 0)
                {
                    fprintf(stderr, "%s execute_sequence(): Failed to write %d to %s\n", sequence[command].data.b, sequence[command].attribute_name);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (abs(error) > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
            case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:
            {
                printf("%s: Executing attribute update: %s %s (%s) -> %s\n", PLUGIN_NAME, sequence[command].device_name, sequence[command].attribute_name, sequence[command].is_output ? "out" : "in", sequence[command].string_data);
                error = iio_attr_write_string(attr, sequence[command].string_data);
                if (error < 0)
                {
                    fprintf(stderr, "%s execute_sequence(): Failed to write %s to %s\n", PLUGIN_NAME, sequence[command].data.b, sequence[command].string_data);
                    fprintf(stderr, "Error %d: %s\n", error, strerror(error));

                    if (abs(error) > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                        return EDEVFAIL;
                    else
                        return VITA49_2_ERRNO_MAP[error];
                }
                break;
            }
        }
    }

    return ENONE;
}


#ifdef __cplusplus
}
#endif