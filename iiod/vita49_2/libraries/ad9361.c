/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 *
 */

// This script offers a template for how a library file can be written to execute a sequence of commands
// using dynamic values that are passed to the library at runtime that are then issued over libiio.

// This example was designed for the ADALM Pluto.

// To exercise this functionality, run the simulator.c script in examples/vita49_2/ and when prompted, 
// choose to send a Control Packet to modify gain.

#include <vita49_2/vita49_2_packet_types.h>
#include "../vita49_2_iiod_helpers.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>

#define DEVICE_NAME "ad9361-phy"

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((visibility("default"))) bool identify(const struct iio_context* const ctx)
{
	if (iio_context_find_device(ctx, DEVICE_NAME) != NULL)
		return true;

    return false;
}

void _report_warning(struct vita49_2_warnings* const warnings, union vita49_2_warning_error_indicators* const warnings_buffer, enum vita49_2_cif_types cif_type, uint8_t cif_bit, enum vita49_2_warnings_error_codes warning)
{
    if (warnings == NULL || warnings_buffer == NULL || cif_bit > 31)
    {
        fprintf(stderr, "%s validate_control_packet(): Failed to report warning in AckV Packet because of invalid parameters.\n", DEVICE_NAME);
        return;
    }

    warnings_buffer[cif_bit].word = (1 << warning);

    switch (cif_type)
    {
        case CIF0:
            warnings->cif0_warnings.word |= (1 << cif_bit);
            break;

        case CIF1:
            if (warnings->cif1_warnings == NULL)
            {
                warnings->cif1_warnings = malloc(sizeof(union vita49_2_cif1_word));
                if (warnings->cif1_warnings == NULL)
                {
                    fprintf(stderr, "%s validate_control_packet(): Failed to allocate memory for CIF1 Warnings. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            warnings->cif1_warnings->word |= (1 << cif_bit);

            break;

        case CIF2:
            if (warnings->cif2_warnings == NULL)
            {
                warnings->cif2_warnings = malloc(sizeof(union vita49_2_cif2_word));
                if (warnings->cif2_warnings == NULL)
                {
                    fprintf(stderr, "%s validate_control_packet(): Failed to allocate memory for CIF2 Warnings. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            warnings->cif2_warnings->word |= (1 << cif_bit);

            break;

        case CIF3:
            if (warnings->cif3_warnings == NULL)
            {
                warnings->cif3_warnings = malloc(sizeof(union vita49_2_cif3_word));
                if (warnings->cif3_warnings == NULL)
                {
                    fprintf(stderr, "%s validate_control_packet(): Failed to allocate memory for CIF3 Warnings. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            warnings->cif3_warnings->word |= (1 << cif_bit);

            break;

        case CIF7:
            if (warnings->cif7_warnings == NULL)
            {
                warnings->cif7_warnings = malloc(sizeof(union vita49_2_cif7_word));
                if (warnings->cif7_warnings == NULL)
                {
                    fprintf(stderr, "%s validate_control_packet(): Failed to allocate memory for CIF7 Warnings. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            warnings->cif7_warnings->word |= (1 << cif_bit);

            break;

        default:
            fprintf(stderr, "%s validate_control_packet(): Invalid CIF type '%d'\n", DEVICE_NAME, cif_type);
            break;
    }
}

__attribute__((visibility("default"))) enum vita49_2_warnings_error_codes validate_control_packet(const struct iio_context* const ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_warnings* const warnings)
{
    if (ctx == NULL || control_packet == NULL || warnings == NULL)
        return EBADARGS;
    
    int warning;

    // For keeping track of which CIF fields have generated warnings
	union vita49_2_warning_error_indicators cif0_warnings_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif1_warnings_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif2_warnings_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif3_warnings_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif7_warnings_buffer[32] = {0};

    // Checking if any CIF0 fields are present
    if (control_packet->cif0.word.word != 0)
    {
        // CIF0 bits 9-0 are unused in Control Packets
        for (uint8_t cif_bit = 10; cif_bit < 32; cif_bit++)
        {
            if (((1 << cif_bit) & control_packet->cif0.word.word) == 0)
                continue;
            
            switch (cif_bit)
            {
                // Ephemeris Reference ID (unused at the moment)
                case 10:
                    break;

                // Sample Rate
                case 21:
                    
                    // Checking the RX first
                    if ((warning = validate_command_ll(ctx, "ad9361-phy", "voltage0", "sampling_frequency", false, control_packet->cif0.sample_rate)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);
                
                    // Checking the TX
                    if ((warning = validate_command_ll(ctx, "ad9361-phy", "voltage0", "sampling_frequency", true, control_packet->cif0.sample_rate)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);
                
                    break;

                // Gain
                case 23:

                    // Checking the RX first
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "voltage0", "hardwaregain", false, control_packet->cif0.gains.gain_stage_1)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);

                    // Checking the TX
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "voltage0", "hardwaregain", true, control_packet->cif0.gains.gain_stage_2)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);

                    break;

                // Reference Level (unused at the moment)
                case 24:   
                    break;

                // IF Band Offset (unused at the moment)
                case 25:
                    break;

                // RF Reference Frequency Offset (unused at the moment)
                case 26:
                    break;

                // RF Reference Frequency -> Maps to LO
                case 27:

                    // Checking the RX first
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "altvoltage0", "sampling_frequency", true, control_packet->cif0.rf_reference_frequency)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);

                    // Checking the TX
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "altvoltage1", "sampling_frequency", true, control_packet->cif0.rf_reference_frequency)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);

                    break;

                // IF Reference Frequency (unused as the AD9361 is 0-IF)
                case 28:
                    break;

                // Bandwidth
                case 29:

                      // Checking the RX first
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "voltage0", "rf_bandwidth", false, control_packet->cif0.bandwidth)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);

                    // Checking the TX
                    if ((warning = validate_command_d(ctx, "ad9361-phy", "voltage0", "rf_bandwidth", true, control_packet->cif0.bandwidth)) != ENONE)
                        _report_warning(warnings, cif0_warnings_buffer, CIF0, cif_bit, warning);
                    
                    break;

                // Reference Point Identifier (unused at the moment)
                case 30:
                    break;

                // Shouldn't get here
                default:
                    fprintf(stderr, "%s validate_control_packet(): Received an unsupported CIF bit (%d) for CIF0.\n", DEVICE_NAME, cif_bit);
                
            }
        }
    }

    // TODO: Logic for CIF1
    if (control_packet->cif1 != NULL && control_packet->cif1->word.word != 0)
    {
    }

    // TODO: Logic for CIF2
    if (control_packet->cif2 != NULL && control_packet->cif2->word.word != 0)
    {
    }

    // TODO: Logic for CIF3
    if (control_packet->cif3 != NULL && control_packet->cif3->word.word != 0)
    {
    }

    // TODO: Logic for CIF7
    if (control_packet->cif7 != NULL && control_packet->cif7->word.word != 0)
    {
    }

    // Copying the warnings
    uint8_t cif0_warnings = 0, cif1_warnings = 0, cif2_warnings = 0, cif3_warnings = 0, cif7_warnings = 0, num_warnings = 0;
    cif0_warnings = __builtin_popcount(warnings->cif0_warnings.word);

    if (warnings->cif1_warnings != NULL)
        cif1_warnings = __builtin_popcount(warnings->cif1_warnings->word); 
    
    if (warnings->cif2_warnings != NULL)
        cif2_warnings = __builtin_popcount(warnings->cif2_warnings->word);

    if (warnings->cif3_warnings != NULL)
        cif3_warnings = __builtin_popcount(warnings->cif3_warnings->word);

    if (warnings->cif7_warnings != NULL)
        cif7_warnings = __builtin_popcount(warnings->cif7_warnings->word);
        
    num_warnings = cif0_warnings + cif1_warnings + cif2_warnings + cif3_warnings + cif7_warnings;

    if (num_warnings > 0)
    {
        if (warnings->warnings_payload == NULL) 
        {
            warnings->warnings_payload = calloc(num_warnings, sizeof(union vita49_2_warning_error_indicators));
            if (warnings->warnings_payload == NULL)
            {
                fprintf(stderr, "vita49_2_process: Failed to allocate memory for the error indicators for an ackV Packet. (%d) %s\n", errno, strerror(errno));
                return -ENOMEM;
            }
        }
        // We may need to resize the buffer if we have more warnings compared to the last ackV Packet that was sent
        else
        {
            void* warnings_tmp = realloc(warnings->warnings_payload, num_warnings * sizeof(union vita49_2_warning_error_indicators));
            
            if (warnings_tmp == NULL)
            {
                fprintf(stderr, "vita49_2_process: Failed to allocate memory for the error indicators for an ackV Packet. (%d) %s\n", errno, strerror(errno));
                return -ENOMEM;
            }

            warnings->warnings_payload = (union vita49_2_warning_error_indicators*)(warnings_tmp);
        }

        uint8_t warnings_index = 0;
        if (cif0_warnings > 0)
        {
            for (int8_t i = 31; i > 0; i--)
            {
                if ((1 << i) & warnings->cif0_warnings.word)
                    warnings->warnings_payload[warnings_index++].word = cif0_warnings_buffer[i].word;
            }
        }

        if (cif1_warnings > 0)
        {
            for (int8_t i = 31; i > 0; i--)
            {
                if ((1 << i) & warnings->cif1_warnings->word)
                    warnings->warnings_payload[warnings_index++].word = cif1_warnings_buffer[i].word;
            }
        }

        if (cif2_warnings > 0)
        {
            for (int8_t i = 31; i > 0; i--)
            {
                if ((1 << i) & warnings->cif2_warnings->word)
                    warnings->warnings_payload[warnings_index++].word = cif2_warnings_buffer[i].word;
            }
        }
        
        if (cif3_warnings > 0)
        {
            for (int8_t i = 31; i > 0; i--)
            {
                if ((1 << i) & warnings->cif3_warnings->word)
                    warnings->warnings_payload[warnings_index++].word = cif3_warnings_buffer[i].word;
            }
        }

        if (cif7_warnings > 0)
        {
            for (int8_t i = 31; i > 0; i--)
            {
                if ((1 << i) & warnings->cif7_warnings->word)
                    warnings->warnings_payload[warnings_index++].word = cif7_warnings_buffer[i].word;
            }
        }
        warnings->warnings_payload_num_words = warnings_index;
    }
    else
        warnings->warnings_payload_num_words = 0;

    return ENONE;
}

enum vita49_2_warnings_error_codes _update_attribute(const struct iio_context* const ctx, const struct iio_attr* attr, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, enum vita49_2_control_data_types data_type, const void* const data)
{
    if (ctx == NULL || attr == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL)
        return EBADARGS;

    int error;

    // Finding the IIO device
    if ((error = vita49_2_find_iio_attribute(ctx, device_name, channel_name, attribute_name, is_output, &attr)) < 0)
    {
        fprintf(stderr, "%s execute_control_packet(): Failed to execute command. IIO attribute '%s' could not be found.\n", DEVICE_NAME, attribute_name);
        return -error;
    }

    // Updating the IIO device value
    switch (data_type)
    {
        case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL:
        {
            long long data_ll = *(long long*)(data);

            printf("%s: Executing attribute update: %s %s (%s) -> %lld\n", DEVICE_NAME, device_name, attribute_name, is_output ? "out" : "in", data_ll);
            if ((error = iio_attr_write(attr, data_ll)) < 0)
            {
                fprintf(stderr, "%s execute_control_packet(): Failed to write %lld to %s. (%d): %s\n", DEVICE_NAME, data_ll, attribute_name, error, strerror(-error));

                if (-error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                    return EDEVFAIL;
                else
                    return VITA49_2_ERRNO_MAP[-error];
            }
            break;
        }
        case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F:
        {
            float data_f = *(float *)(data);

            printf("%s: Executing attribute update: %s %s (%s) -> %.5f\n", DEVICE_NAME, device_name, attribute_name, is_output ? "out" : "in", data_f);
            error = iio_attr_write(attr, (double)(data_f));
            if (error < 0)
            {
                fprintf(stderr, "%s execute_control_packet(): Failed to write %.5f to %s. (%d): %s\n", DEVICE_NAME, data_f, attribute_name, error, strerror(-error));

                if (-error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                    return EDEVFAIL;
                else
                    return VITA49_2_ERRNO_MAP[-error];
            }
            break;
        }
        case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D:
        {
            double data_d = *(double *)(data); 

            printf("%s: Executing attribute update: %s %s (%s) -> %.5lf\n", DEVICE_NAME, device_name, attribute_name, is_output ? "out" : "in", data_d);
            error = iio_attr_write(attr, data_d);
            if (error < 0)
            {
                fprintf(stderr, "%s execute_control_packet(): Failed to write %lf to %s. (%d): %s\n", DEVICE_NAME, data_d, attribute_name, error, strerror(-error));

                if (-error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                    return EDEVFAIL;
                else
                    return VITA49_2_ERRNO_MAP[-error];
            }

            break;
        }
        case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B:
        {
            bool data_b = *(bool *)(data);

            printf("%s: Executing attribute update: %s %s (%s) -> %d\n", DEVICE_NAME, device_name, attribute_name, is_output ? "out" : "in", data_b);
            error = iio_attr_write(attr, data_b);
            if (error < 0)
            {
                fprintf(stderr, "%s execute_control_packet(): Failed to write %d to %s. (%d): %s\n", DEVICE_NAME, data_b, attribute_name, error, strerror(-error));

                if (-error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                    return EDEVFAIL;
                else
                    return VITA49_2_ERRNO_MAP[-error];
            }
            break;
        }
        case VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S:
        {
            char* data_s = (char *)(data);

            printf("%s: Executing attribute update: %s %s (%s) -> %s\n", DEVICE_NAME, device_name, attribute_name, is_output ? "out" : "in", data_s);
            error = iio_attr_write(attr, data_s);
            if (error < 0)
            {
                fprintf(stderr, "%s execute_control_packet(): Failed to write %s to %s. (%d): %s\n", DEVICE_NAME, data_s, attribute_name, error, strerror(-error));

                if (-error > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
                    return EDEVFAIL;
                else
                    return VITA49_2_ERRNO_MAP[-error];
            }
            break;
        }
    }

    return ENONE;
}

void _report_error(struct vita49_2_ackX_packet* const ackX_packet, union vita49_2_warning_error_indicators* const errors_buffer, enum vita49_2_cif_types cif_type, uint8_t cif_bit, enum vita49_2_warnings_error_codes error)
{
    if (ackX_packet == NULL || errors_buffer == NULL || cif_bit > 31)
    {
        fprintf(stderr, "%s execute_control_packet(): Failed to report error in AckX Packet because of invalid parameters.\n", DEVICE_NAME);
        return;
    }

    errors_buffer[cif_bit].word = ((1 << ENOEXECUTE) | (1 << error));

    switch (cif_type)
    {
        case CIF0:
            ackX_packet->errors.cif0_errors.word |= (1 << cif_bit);
            
            // Disabling the corresponding warning (can't have a warning and error for the same field)
            ackX_packet->warnings.cif0_warnings.word &= (~(1 << cif_bit));
            
            break;

        case CIF1:
            if (ackX_packet->errors.cif1_errors == NULL)
            {
                ackX_packet->errors.cif1_errors = malloc(sizeof(union vita49_2_cif1_word));
                if (ackX_packet->errors.cif1_errors == NULL)
                {
                    fprintf(stderr, "%s execute_control_packet(): Failed to allocate memory for CIF1 Errors. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            ackX_packet->errors.cif1_errors->word |= (1 << cif_bit);

            // Disabling the corresponding warning (can't have a warning and error for the same field)
            ackX_packet->warnings.cif1_warnings->word &= (~(1 << cif_bit));

            break;

        case CIF2:
            if (ackX_packet->errors.cif2_errors == NULL)
            {
                ackX_packet->errors.cif2_errors = malloc(sizeof(union vita49_2_cif2_word));
                if (ackX_packet->errors.cif2_errors == NULL)
                {
                    fprintf(stderr, "%s execute_control_packet(): Failed to allocate memory for CIF2 Errors. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            ackX_packet->errors.cif2_errors->word |= (1 << cif_bit);

            // Disabling the corresponding warning (can't have a warning and error for the same field)
            ackX_packet->warnings.cif2_warnings->word &= (~(1 << cif_bit));

            break;

        case CIF3:
            if (ackX_packet->errors.cif3_errors == NULL)
            {
                ackX_packet->errors.cif3_errors = malloc(sizeof(union vita49_2_cif3_word));
                if (ackX_packet->errors.cif3_errors == NULL)
                {
                    fprintf(stderr, "%s execute_control_packet(): Failed to allocate memory for CIF3 Errors. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            ackX_packet->errors.cif3_errors->word |= (1 << cif_bit);

            // Disabling the corresponding warning (can't have a warning and error for the same field)
            ackX_packet->warnings.cif3_warnings->word &= (~(1 << cif_bit));
            
            break;

        case CIF7:
            if (ackX_packet->errors.cif7_errors == NULL)
            {
                ackX_packet->errors.cif7_errors = malloc(sizeof(union vita49_2_cif7_word));
                if (ackX_packet->errors.cif7_errors == NULL)
                {
                    fprintf(stderr, "%s execute_control_packet(): Failed to allocate memory for CIF7 Errors. (%d) %s\n", DEVICE_NAME, errno, strerror(errno));
                    return;
                }
            }
            ackX_packet->errors.cif7_errors->word |= (1 << cif_bit);

            // Disabling the corresponding warning (can't have a warning and error for the same field)
            ackX_packet->warnings.cif7_warnings->word &= (~(1 << cif_bit));
            
            break;

        default:
            fprintf(stderr, "%s execute_control_packet(): Invalid CIF type '%d'\n", DEVICE_NAME, cif_type);
            break;
    }
}

__attribute__((visibility("default"))) enum vita49_2_warnings_error_codes execute_control_packet(const struct iio_context* const ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_ackX_packet* const ackX_packet)
{
    // Not checking AckX as it's an optional argument that's only used if we want to report errors
    if (ctx == NULL || control_packet == NULL)
        return -EINVAL;

    // For keeping track of which CIF fields have generated what errors
	union vita49_2_warning_error_indicators cif0_errors_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif1_errors_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif2_errors_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif3_errors_buffer[32] = {0};
	union vita49_2_warning_error_indicators cif7_errors_buffer[32] = {0};

    int error;
	const struct iio_attr *attr;

    // If an AckX packet was requested (i.e. the ackX_packet pointer isn't NULL), then we need to validate the commands for any warnings
    // since AckX contains warning and error fields
    if (ackX_packet != NULL)
        if ((error = validate_control_packet(ctx, control_packet, &ackX_packet->warnings)) != ENONE)
            fprintf(stderr, "%s execute_control_packet(): Failed to generate warnings for AckX. (%d)", DEVICE_NAME, error);

    // Checking if any CIF0 fields are present
    if (control_packet->cif0.word.word != 0)
    {
        // CIF0 bits 9-0 are unused in Control Packets
        for (uint8_t cif_bit = 10; cif_bit < 32; cif_bit++)
        {
            if (((1 << cif_bit) & control_packet->cif0.word.word) == 0)
                continue;
            
            switch (cif_bit)
            {
                // Ephemeris Reference ID (unused at the moment)
                case 10:
                    break;

                // Sample Rate
                case 21:
                    
                    // Updating the RX first
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "sampling_frequency", false, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.sample_rate))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                
                    // Updating the TX
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "sampling_frequency", true, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.sample_rate))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                
                    break;

                // Gain
                case 23:

                    // Setting the RX gain first. This requires us to go into manual gain control mode
                    if ((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "gain_control_mode", false, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S, (void *)("manual"))) != ENONE)
                    {
                        if (ackX_packet != NULL)
                            _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                    }
                    else if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "hardwaregain", false, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F, (void *)(&control_packet->cif0.gains.gain_stage_1))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);

                    // Setting the TX gain
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "hardwaregain", true, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F, (void *)(&control_packet->cif0.gains.gain_stage_2))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);

                    break;

                // Reference Level (unused at the moment)
                case 24:   
                    break;

                // IF Band Offset (unused at the moment)
                case 25:
                    break;

                // RF Reference Frequency Offset (unused at the moment)
                case 26:
                    break;

                // RF Reference Frequency -> Maps to LO
                case 27:

                    // Updating the RX first
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "altvoltage0", "sampling_frequency", true, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.rf_reference_frequency))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                
                    // Updating the TX
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "altvoltage1", "sampling_frequency", true, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.rf_reference_frequency))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);

                    break;

                // IF Reference Frequency (unused as the AD9361 is 0-IF)
                case 28:
                    break;

                // Bandwidth
                case 29:

                    // Updating the RX first
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "rf_bandwidth", false, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.bandwidth))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                    
                    // Updating the TX first
                    if (((error = _update_attribute(ctx, attr, "ad9361-phy", "voltage0", "rf_bandwidth", true, VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D, (void *)(&control_packet->cif0.bandwidth))) != ENONE) && (ackX_packet != NULL))
                        _report_error(ackX_packet, cif0_errors_buffer, CIF0, cif_bit, error);
                    
                    break;

                // Reference Point Identifier (unused at the moment)
                case 30:
                    break;

                default:
                    fprintf(stderr, "%s execute_control_packet(): Received an unsupported CIF bit (%d) for CIF0.\n", DEVICE_NAME, cif_bit);
                
            }
        }
    }

    // TODO: Logic for CIF1
    if (control_packet->cif1 != NULL && control_packet->cif1->word.word != 0)
    {
    }

    // TODO: Logic for CIF2
    if (control_packet->cif2 != NULL && control_packet->cif2->word.word != 0)
    {
    }

    // TODO: Logic for CIF3
    if (control_packet->cif3 != NULL && control_packet->cif3->word.word != 0)
    {
    }

    // TODO: Logic for CIF7
    if (control_packet->cif7 != NULL && control_packet->cif7->word.word != 0)
    {
    }

    // If a valid pointer to an AckX packet was provided, we'll copy the errors buffer if any of the CIF fields have reported errors
    if (ackX_packet != NULL)
    {
        uint8_t cif0_errors = 0, cif1_errors = 0, cif2_errors = 0, cif3_errors = 0, cif7_errors = 0, num_errors = 0;
        cif0_errors = __builtin_popcount(ackX_packet->errors.cif0_errors.word);

        if (ackX_packet->errors.cif1_errors != NULL)
            cif1_errors = __builtin_popcount(ackX_packet->errors.cif1_errors->word); 
        
        if (ackX_packet->errors.cif2_errors != NULL)
            cif2_errors = __builtin_popcount(ackX_packet->errors.cif2_errors->word);

        if (ackX_packet->errors.cif3_errors != NULL)
            cif3_errors = __builtin_popcount(ackX_packet->errors.cif3_errors->word);

        if (ackX_packet->errors.cif7_errors != NULL)
            cif7_errors = __builtin_popcount(ackX_packet->errors.cif7_errors->word);
            
        num_errors = cif0_errors + cif1_errors + cif2_errors + cif3_errors + cif7_errors;

        if (num_errors > 0)
        {
            if (ackX_packet->errors.errors_payload == NULL) 
            {
                ackX_packet->errors.errors_payload = calloc(num_errors, sizeof(union vita49_2_warning_error_indicators));
                if (ackX_packet->errors.errors_payload == NULL)
                {
                    fprintf(stderr, "vita49_2_process: Failed to allocate memory for the error indicators for an AckX Packet. (%d) %s\n", errno, strerror(errno));
                    return -ENOMEM;
                }
            }
            // We may need to resize the buffer if we have more errors compared to the last AckX Packet that was sent
            else
            {
                void* errors_tmp = realloc(ackX_packet->errors.errors_payload, num_errors * sizeof(union vita49_2_warning_error_indicators));
                
                if (errors_tmp == NULL)
                {
                    fprintf(stderr, "vita49_2_process: Failed to allocate memory for the error indicators for an AckX Packet. (%d) %s\n", errno, strerror(errno));
                    return -ENOMEM;
                }

                ackX_packet->errors.errors_payload = (union vita49_2_warning_error_indicators*)(errors_tmp);
            }

            uint8_t errors_index = 0;
            if (cif0_errors > 0)
            {
                for (int8_t i = 31; i > 0; i--)
                {
                    if ((1 << i) & ackX_packet->errors.cif0_errors.word)
                        ackX_packet->errors.errors_payload[errors_index++].word = cif0_errors_buffer[i].word;
                }
            }

            if (cif1_errors > 0)
            {
                for (int8_t i = 31; i > 0; i--)
                {
                    if ((1 << i) & ackX_packet->errors.cif1_errors->word)
                        ackX_packet->errors.errors_payload[errors_index++].word = cif1_errors_buffer[i].word;
                }
            }

            if (cif2_errors > 0)
            {
                for (int8_t i = 31; i > 0; i--)
                {
                    if ((1 << i) & ackX_packet->errors.cif2_errors->word)
                        ackX_packet->errors.errors_payload[errors_index++].word = cif2_errors_buffer[i].word;
                }
            }
            
            if (cif3_errors > 0)
            {
                for (int8_t i = 31; i > 0; i--)
                {
                    if ((1 << i) & ackX_packet->errors.cif3_errors->word)
                        ackX_packet->errors.errors_payload[errors_index++].word = cif3_errors_buffer[i].word;
                }
            }

            if (cif7_errors > 0)
            {
                for (int8_t i = 31; i > 0; i--)
                {
                    if ((1 << i) & ackX_packet->errors.cif7_errors->word)
                        ackX_packet->errors.errors_payload[errors_index++].word = cif7_errors_buffer[i].word;
                }
            }
            ackX_packet->errors.errors_payload_num_words = errors_index;
        }
        else
            ackX_packet->errors.errors_payload_num_words = 0;
    }

    return 0;
}


#ifdef __cplusplus
}
#endif