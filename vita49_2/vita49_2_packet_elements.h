/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 * 
 * Contributors:
 * 		- Praveen Perera <praveen.perera@analog.com>
 */

#ifndef __VITA49_2_PACKET_ELEMENTS_H__
#define __VITA49_2_PACKET_ELEMENTS_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

// "Organizationally Unique Identifier". ADI has several OUIs, I just chose the first one: https://regauth.standards.ieee.org/standards-ra-web/pub/view.html#registries
// OUI is one of the component fields of the Class ID field.
#define OUI 0x64F9C0 

// =============================================================================
// VITA 49.2 PACKET TYPES
// =============================================================================
enum vita49_2_packet_type {
	VITA49_2_PKT_TYPE_IF_DATA_NO_SID 		= 0x0,
	VITA49_2_PKT_TYPE_IF_DATA_WITH_SID 		= 0x1,
	VITA49_2_PKT_TYPE_EXT_DATA_NO_SID 		= 0x2,
	VITA49_2_PKT_TYPE_EXT_DATA_WITH_SID 	= 0x3,
	VITA49_2_PKT_TYPE_IF_CONTEXT 			= 0x4,
	VITA49_2_PKT_TYPE_EXT_CONTEXT 			= 0x5,
	VITA49_2_PKT_TYPE_COMMAND 				= 0x6,
	VITA49_2_PKT_TYPE_EXT_COMMAND 			= 0x7,
};

// =============================================================================
// TSI - TIMESTAMP INTEGER
// =============================================================================
enum vita49_2_tsi {
	VITA49_2_TSI_NONE 		= 0,
	VITA49_2_TSI_UTC 		= 1,
	VITA49_2_TSI_GPS 		= 2,
	VITA49_2_TSI_OTHER 		= 3,
};

// =============================================================================
// TSF - TIMESTAMP FRACTIONAL
// =============================================================================
enum vita49_2_tsf {
	VITA49_2_TSF_NONE 			= 0,
	VITA49_2_TSF_SAMPLE_COUNT 	= 1,
	VITA49_2_TSF_REAL_TIME 		= 2,
	VITA49_2_TSF_FREE_RUNNING 	= 3,
};

// =============================================================================
// ACTION MODE BITS (CONTROL PACKET SPECIFIC)
// =============================================================================
enum vita49_2_control_action_modes {
	VITA49_2_CTRL_NO_ACT 	= 0,
	VITA49_2_CTRL_DRY_RUN	= 1,
	VITA49_2_CTRL_EXECUTE	= 2,
	VITA49_2_CTRL_RESERVED	= 3
};

// ================================================================================================
// FIELD-SPECIFIC PREDEFINED WARNINGS AND ERRORS (see Table 8.4.1.2.1-1 in the VITA 49.2 2017)
// ================================================================================================
enum vita49_2_warnings_error_codes {
	ENOEXECUTE 	= 31,		// The field was NOT executed because of a Warning or Error (ENOEXEC is a conflicting name)
	EDEVFAIL 	= 30,		// The field was NOT executed properly because of a device (such as hardware) failure
	ENOFIELD	= 29,		// The device does NOT accept this particular Control field
	EOUTRANGE	= 28,		// The supplied field value is beyond the capability or operational range of this device
	EPRECISION	= 27,		// The supplied field value specifies a level of precision beyond the capability of this device
	EINVALID	= 26,		// This field had an invalid setting beyond those specified above
	ETIMESTAMP	= 25,		// The Controllee was unable to meet the timestamp requirement specified by the [T2,T1,T0] bits for the specified field.
	EHAZPOWER	= 24,		// The supplied field will cause transmission of hazardous power levels.
	EDISTORTION = 23,		// The supplied field will cause components to be over driven leading to distortion. This applies to both receive and transmit.
	EINBPOWER	= 22,		// The supplied field will place the in-band power levels out of compliance
	EOUTBPOWER 	= 21,		// The supplied field will place the out-of-band power levels out of compliance
	ECOSINTRF	= 20,		// The supplied field will cause co-site interference between transmitter and receiver at same location
	EREGINTRF	= 19,		// The supplied field will cause interference between devices in the same operational region

	// 18-13 are reserved

	// 12-1 are user-defined
	EBADARGS	= 2,		// Bad arguments to the function call (null pointer, etc.)
	EGENERIC	= 1,		// Generic failure
	
	// 0 is reserved, I'll use this as the no error indicator
	ENONE		= 0			// No warnings/errors were produced
};

// =============================================================================
// PACKET CLASS CODES
// =============================================================================

// VITA 49.2 specifies that an organization must come up with Packet Classes which define
// what information is present in VITA 49.2 packets and how they're structured/formatted.

// Each of these Packet Classes are assigned codes. I've already generated some
// Packet Classes and Packet Class Codes as specified in my "VITA 49.2 Information Structures" document.

// Packet Class Code is a 16-bit field.
enum vita49_2_packet_class_codes {

	// IMPORTANT: As more classes are defined in the future, please make to use explicit enum values
	// rather than relying on implicit values. I've organized the attributes below by Packet Class type
	// rather than by the order in which they were added to the enum.
	// If you add a Data Packet Class in the future and don't explicity specify its Packet Class code, 
	// that can make its value unclear.

	// IMPORTANT: Packet Class Code 0x0000 is reserved. I'm doing this on purpose in case the host forgets to populate
	// that field because they may have forgotten other information as well, that way the device throws that packet out.

	// Signal Data Packet Classes
	VITA49_2_PKT_CLASS_TIME_DATA 				= 0x0001,		// Transmit 16-bit I/Q data
	
	// Context Packet Classes
	VITA49_2_PKT_CLASS_GENERIC_CONTEXT 			= 0x0002,		// Generic Context Packets. The fields that are present in the payload are provided by the CIF word.

	// Control Packet Classes
	VITA49_2_PKT_CLASS_GENERIC_CONTROL 			= 0x0003,		// Generic Control Packet, not all the CIF fields are applicable to Command Packets so not all of them are used.
	VITA49_2_PKT_CLASS_REFILL_TIME_REQUEST		= 0x0004,		// Control Packet specifically for requesting more time data packets

	// Acknowledge Packet Classes
	VITA49_2_PKT_CLASS_ACKV_ACKX				= 0x0005,		// Acknowledgement indicating validity of commands (AckV) or which commands were executed properly (AckX)
	VITA49_2_PKT_CLASS_ACKS						= 0x0006,		// Acknowledgement indicating the new values after the controls from a Control Packet were issued (simmilar to a Context Packet)

	// Control Extension Packet Classes
	VITA49_2_PKT_CLASS_CTRL_EXT_IMPLICIT		= 0x0007,		// Control Extension Packet that uses a payload structure as described by the "implicit" struct in the vita49_2_control_extension_description union
	VITA49_2_PKT_CLASS_CTRL_EXT_EXPLICIT		= 0x0008,		// Control Extension Packet that uses a payload structure as described by the "explicit" struct in the vita49_2_control_extension_description union
};

// =============================================================================
// INFORMATION CLASS CODES
// =============================================================================

// A VITA 49.2 Information Class is a specification of a structure consisting of one or more Packet Classes.
// It basically a logical organization that groups packets together. It says "I want to accomplish this task 
// and I'm using these Packet Classes to do that."

// For example, if I want to convey 70-MHz signal data from a component in my signal processing chain,
// I'd create an Information Class that uses Signal Data Packets, Context Packets, and Control Packets.

// Information Class Code is a 16-bit field.
enum vita49_2_information_class_codes {

	// IMPORTANT: Same warning that I gave for the Packet Class Code. Read that message.
	// IMPORTANT: Information Class Code 0x0000 is reserved. Same reason as for the Packet Classes (read that message).

	// For more information about these Information Classes, see my "VITA 49.2 Information Structures" document

	// Purpose: Query signal time data from the module.
	// Packets: 
		// 1. 16-Bit Time Data (Signal Data Packet)
		// 2. Generic Context Packet (Context Packet)
		// 3. Generic Control Packet (Command Packet)
		// 4. Time Data Refill Request (Command Packet) 
		// 5. AckV/AckX Packet (Command Packet)
		// 6. AckS Packet (Command Packet)
	VITA49_2_INFO_CLASS_MODULE_TIME_DATA		= 0x0001,		

	// Purpose: Send signal time data from the host to the module.
	// Packets:
		// 1. 16-Bit Time Data (Signal Data Packet)
		// 2. Generic Context Packet (Context Packet)
		// 3. Generic Control Packet (Command Packet)
		// 4. AckV/AckX Packet (Command Packet)
		// 5. AckS Packet (Command Packet)
	VITA49_2_INFO_CLASS_HOST_TIME_DATA			= 0x0002

};

// =============================================================================
// CONTROL EXTENSION PACKET DATA TYPES
// =============================================================================

// The Control Extension Packet is for issuing controls that don't map well to CIF fields, such as the "frequency-division-duplex-mode-enable"
// debug attribute for the AD9361. Because there's so many different attributes across ADI devices, there's no conceivable way to
// create CIF-like structures, thus we have to rely on indicator bits to tell us how to interpret the payload in a Control Extension Packet
// such as the datatype for one of the attributes we're trying to modify.
enum vita49_2_control_extension_data_types {
	VITA49_2_CONTROL_EXTENSION_DATA_TYPE_LL 	= 0,	// long long
	VITA49_2_CONTROL_EXTENSION_DATA_TYPE_F		= 1,	// float
	VITA49_2_CONTROL_EXTENSION_DATA_TYPE_D		= 2,	// double
	VITA49_2_CONTROL_EXTENSION_DATA_TYPE_B		= 3,	// bool
	VITA49_2_CONTROL_EXTENSION_DATA_TYPE_S		= 4		// Means the attribute we're modifying has a "string" type value.
};

// =============================================================================
// CONTROL EXTENSION PACKET ENCODING TYPES
// =============================================================================

enum vita49_2_control_extension_encoding_types {
	VITA49_2_CONTROL_EXTENSION_ENCODING_NONE 	= 0,	// No special encoding
	VITA49_2_CONTROL_EXTENSION_ENCODING_9_7		= 1,	// 9.7 for floats
	VITA49_2_CONTROL_EXTENSION_ENCODING_10_6	= 2,	// 10.6 for floats
	VITA49_2_CONTROL_EXTENSION_ENCODING_44_20	= 3,	// 44.20 for doubles
};

/**
 * @struct vita49_2_header
 * @brief VITA 49.2 Packet Header (32 bits)
 * 
 * This structure contains the subfields composing the Header field existing in every VITA 49.2 packet.
 */
struct vita49_2_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t packet_size_words:16;  /* Packet Size in 32-bit words (including the header)*/
	uint32_t packet_count:4;        /* Packet Count (modulo-15 sequence counter) */
	uint32_t ts_fractional_format:2; /* Timestamp Fractional (TSF) Format, MUST use values from the vita49_2_tsi enum */
	uint32_t ts_integer_format:2;   /* Timestamp Integer (TSI) Format, MUST use values from the vita49_2_tsf enum */
	uint32_t indicators:3;			/* Packet Specific Indicator Bits*/
	uint32_t has_class_id:1;        /* Class ID Included Indicator (C bit) */
	uint32_t packet_type:4;         /* VITA 49.2 Packet Type */
#else
	uint32_t packet_type:4;         /* VITA 49.2 Packet Type */
	uint32_t has_class_id:1;        /* Class ID Included Indicator (C bit) */
	uint32_t indicators:3;			/* Packet Specific Indicator Bits*/
	uint32_t ts_integer_format:2;   /* Timestamp Integer (TSI) Format */
	uint32_t ts_fractional_format:2; /* Timestamp Fractional (TSF) Format */
	uint32_t packet_count:4;        /* Packet Count (modulo-15 sequence counter) */
	uint32_t packet_size_words:16;  /* Packet Size in 32-bit words (including the header)*/
#endif
};

/**
 * @struct vita_4_2_trailer
 * @brief VITA 49.2 Trailer (32 bits)
 * 
 * This structure contains the subfields composing the optional Trailer field in Signal Data Packets.
 */
struct vita49_2_trailer {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

	uint32_t associated_context_packet_count:7;      /* Count of linked Context packets */
	uint32_t context_packet_count_enable:1;          /* E bit: Associated Context Packet Count is valid */
	uint32_t state_and_event_indicators:12;          /* State and Event indicators (e.g., AGC, Cal Error) */
	uint32_t indicator_enables:12;                   /* Enables: Validates corresponding indicators */

#else

	uint32_t indicator_enables:12;                   /* Enables: Validates corresponding indicators */
	uint32_t state_and_event_indicators:12;          /* State and Event indicators (e.g., AGC, Cal Error) */
	uint32_t context_packet_count_enable:1;          /* E bit: Associated Context Packet Count is valid */
	uint32_t associated_context_packet_count:7;      /* Count of linked Context packets */

#endif
};

/**
 * @struct vita49_2_class_id
 * @brief VITA 49.2 Class ID (64 bits)
 * 
 * This structure contains the individual fields composing the Class ID field.
 */
struct vita49_2_class_id {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	
	// Organizing into words to make encoding into a buffer simpler
	struct {
		uint32_t oui:24;           				/* Organizationally Unique Identifier (OUI) */
		uint32_t reserved:3;
		uint32_t pad_bit_count:5;  				/* Number of padding bits appended to the end of the payload to ensure the payload is a multiple of 32-bit words*/
	} lower_word;

	struct {
		uint32_t packet_class_code:16; 			/* Packet Class Code - Identifies the particular Packet Class, MUST use values from the vita49_2_packet_class_codes enum*/
		uint32_t information_class_code:16; 	/* Information Class Code - Identifies the particular Information Class, MUST use valuse from the vita49_2_information_class_codes enum */
	} upper_word;

#else

	struct {
		uint32_t pad_bit_count:5;  				/* Number of padding bits appended to the end of the payload to ensure the payload is a multiple of 32-bit words*/
		uint32_t reserved:3;
		uint32_t oui:24;           				/* Organizationally Unique Identifier (OUI) */
	} lower_word;

	struct {
		uint32_t information_class_code:16; 	/* Information Class Code - Identifies the particular Information Class, MUST use valuse from the vita49_2_information_class_codes enum */	} upper_word;
		uint32_t packet_class_code:16; 			/* Packet Class Code - Identifies the particular Packet Class, MUST use values from the vita49_2_packet_class_codes enum*/
	
#endif
};


/**
 * @struct vita49_2_prologue
 * @brief VITA 49.2 Common Packet Prologue
 * 
 * This structure contains a common set of fields that exist in every VITA 49.2 packet.
 * 
 * 	Consists of:
		- Header			(Required)
		- Stream ID			(Required)
		- Class ID			(Required)
		- Timestamp
			- Int 			(Required)
			- Fractional 	(Optional)
 */
struct vita49_2_prologue {

	struct vita49_2_header header;
	uint32_t stream_id;       					/* Stream Identifier, required for ADI's implementation */
	struct vita49_2_class_id class_id;        	/* 64-bit Class Identifier, required for ADI's implementation */
	uint32_t timestamp_int;   					/* Integer Timestamp, required for ADI's implementation */
	uint64_t timestamp_frac; 					/* Optional Fractional Timestamp */

	// I've added these bools just in case we decide to make some of the required fields I mentioned above optional again
	bool has_stream_id;       /**< True if stream_id is populated */
	bool has_class_id;        /**< True if class_id is populated */
	bool has_timestamp_int;   /**< True if timestamp_int is populated */
	bool has_timestamp_frac;  /**< True if timestamp_frac is populated */
};

/**
 * @struct vita49_2_iq_item
 * @brief VITA 49.2 Item Packing Field for 16-bit I/Q Data (32 bits)
 * 
 * This structure defines the format of Item Packing Fields for 16-bit I/Q samples. These structs
 * will be used to populate the payload of a Signal-Time Data Packet (specifically for 16-bit I/Q).
 */
struct vita49_2_iq_item {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	
	// Currently we're not implementing channel and event tags as that would cause Item Packing Fields to extend beyond 32 bits,
	// however ADI retains the right to implement them in the future.

	// uint32_t channel_tags:8; 	/* Channel Tags associate a Data Item with a particular channel*/
	// uint32_t event_tags:8; 		/* Event Tags are unused in ADI's implementation, however we retain the right to support them at a later time */
	
	uint32_t quadrature:16; 		/* 16-bit signed Q component */
	uint32_t in_phase:16; 			/* 16-bit signed I component */

#else

	uint32_t in_phase:16; 			/* 16-bit signed I component */
	uint32_t quadrature:16; 		/* 16-bit signed Q component */
	
	// uint32_t event_tags:8; 		/* Event Tags are unused in ADI's implementation, however we retain the right to support them at a later time */
	// uint32_t channel_tags:8; 	/* Channel Tags associate a Data Item with a particular channel*/

#endif
};

/**
 * @struct vita49_2_cam_field
 * @brief VITA 49.2 Control/Acknowledge Mode (CAM) Field (32 bits)
 * 
 * This structure contains bits representing the Control/Acknowledge Mode (CAM) field specifically for Control Packets.
 * Another variation exists for Acknowledge Packets.
 */
struct vita49_2_control_cam_field {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__	

	// See Table 8.3.1-1 in the full VITA 49.2 specification for details on the meaning of each bit in this field
	uint32_t reserved_7_0:8; 				/* Reserved bits - should be set to 0 on generation and ignored on parsing */
	
	uint32_t ack_bits:4;					/* Set to 0 for Control Packets */
	
	uint32_t timing_control:3;				/* See Table 8.3.1.7-1 in the full VITA 49.2 specification document */

	uint32_t reserved_15:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t request_error_x:1;				/* Requests Error fields be sent in the AckX packet */
	uint32_t request_warning_x:1;			/* Requests Warning fields be sent in the AckX packet */

	uint32_t request_ack_s:1;				/* Requests an AckS packet in response to a Command packet */
	uint32_t request_ack_x:1;				/* Requests an AckX packet in response to a Command packet */
	uint32_t request_ack_v:1;				/* Requests an AckV packet in response to a Command packet */

	uint32_t reserved_21:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t nack:1;						/* NACK bit - set to 1 in a Control Packet to indicate that AckV and/or AckX packets are generated ONLY when Warnings or Errors have occured. If set to 0, AckV and AckX are provided in all cases (assuming request_ack_x or request_ack_v have been asserted). */	

	uint32_t action_bits:2;					/* 00 = No-Action Mode, 01 = Dry Run Mode which is unsupported by ADI, 10 = Execute Mode, 11 = Reserved */

	uint32_t errors:1;						/* Permit execution of fields that generate Errors */
	uint32_t warnings:1;					/* Permit execution of fields that generate Warnings */

	uint32_t partial_execution:1;			/* Execute any fields that do NOT have any warnings or errors, and attempt execution of warning and error fields as governed by Ctrl-W and Ctrl-Er. */

	uint32_t controller_id_format:1;		/* 1 = Controller Identifier field uses 128-bit UUID, 0 = Controller Identifier field uses 32-bit ID */
	uint32_t has_controller_id:1; 			/* 1 = Controller Identifier field is present, 0 = Controller Identifier field is not present */

	// ADI does not use controllee ID/UUID, however we retain the right to support it at a later time.
	uint32_t controllee_id_format:1;		/* 1 = Controllee Identifier field uses 128-bit UUID, 0 = Controllee Identifier field uses 32-bit ID */	
	uint32_t has_controllee_id:1; 			/* 1 = Controllee Identifier field is present, 0 = Controllee Identifier field is not present */

#else

	uint32_t has_controllee_id:1; 			/* 1 = Controllee Identifier field is present, 0 = Controllee Identifier field is not present */
	uint32_t controllee_id_format:1;		/* 1 = Controllee Identifier field uses 128-bit UUID, 0 = Controllee Identifier field uses 32-bit ID */	

	uint32_t has_controller_id:1; 			/* 1 = Controller Identifier field is present, 0 = Controller Identifier field is not present */
	uint32_t controller_id_format:1;		/* 1 = Controller Identifier field uses 128-bit UUID, 0 = Controller Identifier field uses 32-bit ID */

	uint32_t partial_execution:1;			/* Execute any fields that do NOT have any warnings or errors, and attempt execution of warning and error fields as governed by Ctrl-W and Ctrl-Er. */

	uint32_t warnings:1;					/* Permit execution of fields that generate Warnings */
	uint32_t errors:1;						/* Permit execution of fields that generate Errors */

	uint32_t action_bits:2;					/* 00 = No-Action Mode, 01 = Dry Run Mode which is unsupported by ADI, 10 = Execute Mode, 11 = Reserved */

	uint32_t nack:1;						/* NACK bit - set to 1 in a Control Packet to indicate that AckV and/or AckX packets are generated ONLY when Warnings or Errors have occured. If set to 0, AckV and AckX are provided in all cases (assuming request_ack_x or request_ack_v have been asserted). */	

	uint32_t reserved_21:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t request_ack_v:1;				/* Requests an AckV packet in response to a Command packet */
	uint32_t request_ack_x:1;				/* Requests an AckX packet in response to a Command packet */
	uint32_t request_ack_s:1;				/* Requests an AckS packet in response to a Command packet */

	uint32_t request_warning_x:1;			/* Requests Warning fields be sent in the AckX packet */
	uint32_t request_error_x:1;				/* Requests Error fields be sent in the AckX packet */

	uint32_t reserved_15:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t timing_control:3;				/* See Table 8.3.1.7-1 in the full VITA 49.2 specification document */

	uint32_t ack_bits:4;					/* Set to 0 for Control Packets */
	
	uint32_t reserved_7_0:8; 				/* Reserved bits - should be set to 0 on generation and ignored on parsing */

#endif
};

/**
 * @struct vita49_2_cam_field
 * @brief VITA 49.2 Control/Acknowledge Mode (CAM) Field (32 bits)
 * 
 * This structure contains bits representing the Control/Acknowledge Mode (CAM) field specifically for Control Packets.
 * Another variation exists for Acknowledge Packets.
 */
struct vita49_2_ack_cam_field {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__	

	// See Table 8.4.1-1 in the full VITA 49.2 specification for details on the meaning of each bit in this field
	uint32_t reserved_9_0:10; 				/* Reserved bits - should be set to 0 on generation and ignored on parsing */

	uint32_t action_scheduled:1;			/* Action Scheduled or Executed, indicates whether actions can be scheduled or were scheduled*/
	uint32_t partial_action:1;				/* Indicate whether an action was partially executed */

	uint32_t timing_ack:3;					/* See Table 8.4.1.5-1 in the full VITA 49.2 specification document */

	uint32_t reserved_15:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t errors_present:1;				/* Applicable to AckX Packets only. Indicates errors were generated. */
	uint32_t warnings_present:1;			/* Applicable to AckV/AckX Packets. Indicates warnings were generated. */

	uint32_t ackS_request:1;				/* Set to 1 if this packet is an AckS Packet */
	uint32_t ackX_request:1;				/* Set to 1 if this packet is an AckX Packet */
	uint32_t ackV_request:1;				/* Set to 1 if this packet is an AckV Packet */

	/* Set these bits to the same values as the bits in the corresponding Control Packet's CAM (specified in Table 8.4.1-1) */
	uint32_t reserved_21:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t nack:1;						/* NACK bit - set to 1 in a Control Packet to indicate that AckV and/or AckX packets are generated ONLY when Warnings or Errors have occured. If set to 0, AckV and AckX are provided in all cases (assuming request_ack_x or request_ack_v have been asserted). */	

	uint32_t action_bits:2;					/* 00 = No-Action Mode, 01 = Dry Run Mode which is unsupported by ADI, 10 = Execute Mode, 11 = Reserved */

	uint32_t errors:1;						/* Permit execution of fields that generate Errors */
	uint32_t warnings:1;					/* Permit execution of fields that generate Warnings */
	
	uint32_t partial_execution:1;			/* Execute any fields that do NOT have any warnings or errors, and attempt execution of warning and error fields as governed by Ctrl-W and Ctrl-Er. */
	uint32_t controller_id_format:1;		/* 1 = Controller Identifier field uses 128-bit UUID, 0 = Controller Identifier field uses 32-bit ID */
	uint32_t has_controller_id:1; 			/* 1 = Controller Identifier field is present, 0 = Controller Identifier field is not present */

	uint32_t controllee_id_format:1;		/* 1 = Controllee Identifier field uses 128-bit UUID, 0 = Controllee Identifier field uses 32-bit ID */	
	uint32_t has_controllee_id:1; 			/* 1 = Controllee Identifier field is present, 0 = Controllee Identifier field is not present */
	/* ======================================================================================= */

#else

	/* Set these bits to the same values as the bits in the corresponding Control Packet's CAM */
	uint32_t has_controllee_id:1; 			/* 1 = Controllee Identifier field is present, 0 = Controllee Identifier field is not present */
	uint32_t controllee_id_format:1;		/* 1 = Controllee Identifier field uses 128-bit UUID, 0 = Controllee Identifier field uses 32-bit ID */	

	uint32_t has_controller_id:1; 			/* 1 = Controller Identifier field is present, 0 = Controller Identifier field is not present */
	uint32_t controller_id_format:1;		/* 1 = Controller Identifier field uses 128-bit UUID, 0 = Controller Identifier field uses 32-bit ID */

	uint32_t partial_execution:1;			/* Execute any fields that do NOT have any warnings or errors, and attempt execution of warning and error fields as governed by Ctrl-W and Ctrl-Er. */

	uint32_t warnings:1;					/* Permit execution of fields that generate Warnings */
	uint32_t errors:1;						/* Permit execution of fields that generate Errors */

	uint32_t action_bits:2;					/* 00 = No-Action Mode, 01 = Dry Run Mode which is unsupported by ADI, 10 = Execute Mode, 11 = Reserved */

	uint32_t nack:1;						/* NACK bit - set to 1 in a Control Packet to indicate that AckV and/or AckX packets are generated ONLY when Warnings or Errors have occured. If set to 0, AckV and AckX are provided in all cases (assuming request_ack_x or request_ack_v have been asserted). */	

	uint32_t reserved_21:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */
	/* ======================================================================================= */


	uint32_t ackV_request:1;				/* Set to 1 if this packet is an AckV Packet */
	uint32_t ackX_request:1;				/* Set to 1 if this packet is an AckX Packet */
	uint32_t ackS_request:1;				/* Set to 1 if this packet is an AckS Packet */

	uint32_t warnings_present:1;			/* Applicable to AckV/AckX Packets. Indicates warnings were generated. */
	uint32_t errors_present:1;				/* Applicable to AckX Packets only. Indicates errors were generated. */

	uint32_t reserved_15:1;					/* Reserved bit - should be set to 0 on generation and ignored on parsing */

	uint32_t timing_ack:3;					/* See Table 8.4.1.5-1 in the full VITA 49.2 specification document */

	uint32_t partial_action:1;				/* Indicate whether an action was partially executed */
	uint32_t action_scheduled:1;			/* Action Scheduled or Executed, indicates whether actions can be scheduled or were scheduled*/

	uint32_t reserved_9_0:10; 				/* Reserved bits - should be set to 0 on generation and ignored on parsing */

#endif
};

/**
 * @struct vita49_2_command_prologue
 * @brief VITA 49.2 Command Packet Prologue (exists in all Command-type packets)
 * 
 * This structure contains a common set of fields used in all Command-type packets.
 */
struct vita49_2_command_prologue {

	struct vita49_2_prologue common_prologue; 	/* Common Prologue fields (Header, Stream ID, Class ID, Timestamp) */

	// There's a difference in the CAM fields for Control Packets vs. Acknowledge Packets. So instead of embedding the structs,
	// I'm deciding to use 2 pointers. I could technically just use a single void pointer, but that doesn't work well with intellisense.
	struct vita49_2_control_cam_field* control_cam;		/* Control/Acknowledge Mode (CAM) field for Control Packets specifically */
	struct vita49_2_ack_cam_field* ack_cam;				/* Control/Acknowledge Mode (CAM) field for Acknowledge Packets specifically */
	
	uint32_t message_id;						/* Message ID field - used to correlate Control Packets with their corresponding Acknowledge Packets */

	// ADI doesn't use controllee ID, however we retain the right to support it at a later time.
	// IMPORTANT: The CAM contains indicator bits to indicate if Controllee ID/UUID and Controller ID/UUID are present

	// ADI doesn't use 128-bit UUIDs, however we retain the right to support it at a later time.
	union controllee_id {
		uint32_t id32;
		uint32_t uuid128[4];
	} controllee_id;

	union controller_id {
		uint32_t id32;
		uint32_t uuid128[4];
	} controller_id;

};

/**
 * @struct vita49_2_warning_error_indicators
 * @brief VITA 49.2 Warning/Error Indicators (32 bits) 
 * 
 * This structure holds event indicator bits that can be asserted if a warning or error occurs during
 * execution of a command/control.
 */
struct vita49_2_warning_error_indicators {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

	// See Table 8.4.1.2.1-1 in the full VITA 49.2 specification for details on the meaning of each bit in this field
	uint32_t reserved_0:1;						/* Reserved bit - should be set to 0 on generation and ignored on parsing */
	uint32_t user_defined:12;					/* User may specify 12 custom error types */
	uint32_t reserved_18_13:6;					/* Reserved bits - should be set to 0 on generation and ignored on parsing */

	uint32_t regional_interference:1;			/* Supplied field will cause interference between devices in the same operational region */
	uint32_t cosite_interference:1;				/* Supplied field will cause co-site interference between the transmitter and receiver at the same location.*/
	
	uint32_t out_of_band_power_compliance:1; 	/* Supplied field will place the out-of-band power levels out of compiance. */
	uint32_t in_band_power_compliance:1;		/* Supplied field will place the in-band power levels out of compliance. */

	uint32_t distortion:1; 						/* Supplied field will cause components to be over-driven, leading to distortion. Applies to receive and transmit. */
	uint32_t hazardous_power_levels:1;			/* Supplied field will cause transmission of hazardous power levels. */

	uint32_t timestamp_problem:1;				/* Indicates that the Controllee was unable to meet the timestamp requirement specified by [T2-T0] for the specified field. */
	
	uint32_t field_value_invalid:1;				/* Supplied field has an invalid value. */
	uint32_t parameter_precision:1;				/* Supplied field has a level of precision beyond the capability of the device. */
	uint32_t parameter_range:1;					/* Supplied field has a value beyond the capability/operational range of the device. */
	uint32_t erroneous_field:1;					/* Supplied field does not accept this particular Control field. */

	uint32_t device_failure:1;					/* Supplied field was not executed properly because of adevice failure. */
	uint32_t field_not_executed:1;				/* Supplied filed was not executed because of a warning or error. */

#else

	uint32_t field_not_executed:1;				/* Supplied filed was not executed because of a warning or error. */
	uint32_t device_failure:1;					/* Supplied field was not executed properly because of adevice failure. */

	uint32_t erroneous_field:1;					/* Supplied field does not accept this particular Control field. */
	uint32_t parameter_range:1;					/* Supplied field has a value beyond the capability/operational range of the device. */
	uint32_t parameter_precision:1;				/* Supplied field has a level of precision beyond the capability of the device. */
	uint32_t field_value_invalid:1;				/* Supplied field has an invalid value. */

	uint32_t timestamp_problem:1;				/* Indicates that the Controllee was unable to meet the timestamp requirement specified by [T2-T0] for the specified field. */

	uint32_t hazardous_power_levels:1;			/* Supplied field will cause transmission of hazardous power levels. */
	uint32_t distortion:1; 						/* Supplied field will cause components to be over-driven, leading to distortion. Applies to receive and transmit. */

	uint32_t in_band_power_compliance:1;		/* Supplied field will place the in-band power levels out of compliance. */
	uint32_t out_of_band_power_compliance:1; 	/* Supplied field will place the out-of-band power levels out of compiance. */

	uint32_t cosite_interference:1;				/* Supplied field will cause co-site interference between the transmitter and receiver at the same location.*/
	uint32_t regional_interference:1;			/* Supplied field will cause interference between devices in the same operational region */
	
	uint32_t reserved_18_13:6;					/* Reserved bits - should be set to 0 on generation and ignored on parsing */
	uint32_t user_defined:12;					/* User may specify 12 custom error types */
	uint32_t reserved_0:1;						/* Reserved bit - should be set to 0 on generation and ignored on parsing */

#endif
};

/**
 * @union
 * @brief Not all of the CIF0 fields translate well to attributes that can be modified on ADI devices.
 * To resolve that, Command/Control Extension Packets are used, but we must also define a custom word format to
 * represent those unique commands in the payload of the Control Extension Packet. 
 */
union vita49_2_control_extension_description {
	uint32_t word;

	// There's 2 options for the Control Extension Description contents.
		
		// Option 1 (Implicit):
			// The first is designed for referencing CIF Extensions that are declared in the hardware mapping file (such as pluto_vrt_mapping.conf).
			// It describes the format of the data and CIF bits that reference the correct mapping, however it doesn't contain the
			// attribute path (device name + channel name + attribute name) as that information is expected to be present in the hardware mapping file.
			
			// This option occupies less data per packet because a lot of that information is written into the hardware mapping file present on the device.

		// Option 2 (Explicit):
		// The second is designed for explicitly referencing device attributes by providing strings for the device name, channel name, and attribute name.
		
		// This option is likely preferred if the user does not want to modify the hardware mapping file because the device is in operation, or is unable
		// to access the device to see what the available options for an attribute are.

	struct {
		#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			uint32_t reserved:16;
			uint32_t option:3;		// Some attributes have a fixed set of string values/options, such as "gain_control_mode" which can be "manual", "fast_attack", "slow_attack", or "hybrid". The "option" field specifies which option is being used. For example, "manual" would correspond to 0, "fast_attack" would be 1, etc... This numbering is based on the order of the options when printing the "<attribute name>_available" file descriptor (EX: gain_control_mode_available)
			uint32_t mapping:8;		// The CIF mapping bit this corresponds to in the <device_name>_mapping.conf, EX: 0, 1, 2, 3...
			uint32_t encoding:2;	// 0 = None, 1 = 9.7, 2 = 10.6, 3 = 44.20 (see the vita49_2_control_extension_encoding_types enum)
			uint32_t data_type:3;	// 0 = Long Long, 1 = Float, 2 = Double, 3 = Bool, 4 = String (see the vita49_2_control_extension_data_types enum)

				// Explanation of "4 = String":
					// Some attributes have a fixed set of string values/options. Rather than packing a string into a VITA packet,
					// if data_type is set to "String" (4), then the VITA backend will look at the "option" field to determine what string
					// attribute value from the list of values in the "<attribute name>_available" fd to use.

					// See a further explanation in the comments for the "option" bitfield above.
		#else
			uint32_t data_type:3;	// 0 = Long Long, 1 = Float, 2 = Double, 3 = Bool, 4 = String
			uint32_t encoding:2;	// 0 = None, 1 = 9.7, 2 = 44.20
			uint32_t mapping:8;		// The CIF mapping bit this corresponds to in the <device_name>_mapping.conf, EX: 0, 1, 2, 3...
			uint32_t option:3;		// Some attributes have a fixed set of string values/options, such as "gain_control_mode" which can be "manual", "fast_attack", "slow_attack", or "hybrid". The "option" field specifies which option is being used. For example, "manual" would correspond to 0, "fast_attack" would be 1, etc... This numbering is based on the order of the options when printing the "<attribute name>_available" file descriptor (EX: gain_control_mode_available)
			uint32_t reserved:16;
		#endif
	} implicit;

	struct {
		#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			
			// Data Metadata
			uint32_t data_length:8;				// Used if the data type is a string and thus the length is otherwise unknown.
			uint32_t encoding:2;				// 0 = None, 1 = 9.7, 2 = 10.6, 3 = 44.20 (see the vita49_2_control_extension_encoding_types enum)
			uint32_t data_type:3;				// 0 = Long Long, 1 = Float, 2 = Double, 3 = Bool, 4 = String (see the vita49_2_control_extension_data_types enum)

			// Attribute Metadata
			uint32_t is_output:1;				// True if the attribute corresponds to an output
			uint32_t attribute_name_length: 6;
			uint32_t channel_name_length: 6;
			uint32_t device_name_length: 6;

		#else

			// Attribute Metadata
			uint32_t device_name_length: 6;
			uint32_t channel_name_length: 6;
			uint32_t attribute_name_length: 6;
			uint32_t is_output:1;				// True if the attribute corresponds to an output

			// Data Metadata
			uint32_t data_type:3;				// 0 = Long Long, 1 = Float, 2 = Double, 3 = Bool, 4 = String (see the vita49_2_control_extension_data_types enum)
			uint32_t encoding:2;				// 0 = None, 1 = 9.7, 2 = 10.6, 3 = 44.20 (see the vita49_2_control_extension_encoding_types enum)
			uint32_t data_length:8;				// Used if the data type is a string and thus the length is otherwise unknown.

		#endif
	} explicit;
};

/**
 * @struct vita49_2_control_extension_word_node
 * @brief Node struct to be used as part of a linked list of Control Extension words.
 */
struct vita49_2_control_extension_word_node {

	union vita49_2_control_extension_description control_extension;

	// For containing the new attribute value that we want to write
	union {
		long long ll;
		double d;
		uint32_t u32;
		float f;
		bool b;
	} data;

	// For containing the attribute metadata for the Explicit version of the Control Extension Description struct.
	char* device_name;
	char* channel_name;
	char* attribute_name;

	// For the Explicit version of the Control Extension Description struct, we may need to store new attribute data as a string.
	// It can be unsafe to place a char pointer inside the data union, hence it'll be declared as a separate attribute.
	// The length of the string is contained within the "data_length" field of the explict struct.
	char* string_data;

	struct vita49_2_control_extension_word_node* next;

};

/**
 * @struct vita49_2_ackV_extension_word_node
 * @brief Node struct to be used as part of a linked list of AckV Extension words.
 */
struct vita49_2_ackV_extension_word_node {

	union vita49_2_control_extension_description control_extension;
	struct vita49_2_warning_error_indicators warning_indicators;
	struct vita49_2_ackV_extension_word_node* next;

};

/**
 * @struct vita49_2_ackX_extension_word_node
 * @brief Node struct to be used as part of a linked list of AckX Extension words.
 */
struct vita49_2_ackX_extension_word_node {

	union vita49_2_control_extension_description control_extension;
	struct vita49_2_warning_error_indicators warning_indicators;
	struct vita49_2_warning_error_indicators error_indicators;
	struct vita49_2_ackV_extension_word_node* next;
	
};

// TODO: Currently we don't support changing the Signal Data Packet Payload format dynamically while IIOD is in operation.
// This is however a feature that VITA 49.2 supports (read section 9.13.3 from the VITA 49.2 2017 document).
// Assuming we do support that in the future, the necessary logic needs to be implemented to use the struct I've defined below.
/**
 * @struct vita49_2_data_payload_format
 * @brief VITA 49.2 Data Payload Format Field for Redefining Item Packing Fields (64 bits)
 * 
 * This structure defines a new format for Item Packing Fields that go in the Payload of Signal Data Packets.
 * VITA 49.2 uses this format to all users to redefine the structure of their Signal Data Packet's payload on the fly (dynamically).
 */
struct vita49_2_data_payload_format {

	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

	// See Table 9.13.3-1 in the VITA 49.2 2017 document
	struct {

		uint32_t data_item_size:6;
		uint32_t item_packing_field_size:6;
		uint32_t data_item_fraction_size:4;
		uint32_t channel_tag_size:4;
		uint32_t event_tag_size:3;
		uint32_t sample_component_repeat_indicator:1;
		uint32_t data_item_format:5;
		uint32_t real_complex_type:2;
		uint32_t packing_method:1;

	} lower_word;

	// See Table 9.13.3-2 in the VITA 49.2 2017 doucment
	struct {

		uint32_t vector_size:16;
		uint32_t repeat_count:16;

	} upper_word;

	#else

	// See Table 9.13.3-1 in the VITA 49.2 2017 document
	struct {

		uint32_t packing_method:1;
		uint32_t real_complex_type:2;
		uint32_t data_item_format:5;
		uint32_t sample_component_repeat_indicator:1;
		uint32_t event_tag_size:3;
		uint32_t channel_tag_size:4;
		uint32_t data_item_fraction_size:4;
		uint32_t item_packing_field_size:6;
		uint32_t data_item_size:6;

	} lower_word;

	// See Table 9.13.3-2 in the VITA 49.2 2017 doucment
	struct {

		uint32_t repeat_count:16;
		uint32_t vector_size:16;
	
	} upper_word;

	#endif

};

// TODO: Define the subfields for the Formatted GPS/INS struct (if necessary, ADI may not even be able to 
// support this based on the capabilities of our SoMs).
// See Section 9.4.5 in the VITA 49.2 2017 document for more information about this field and its subfields.
struct vita49_2_formatted_gps_ins{};

// TODO: Define the subfields for the ASCII GPS struct (only if necessary like with the Formatted GPS/INS struct).
// See Section 9.4.7 in the VITA 49.2 2017 document for more information.
struct vita49_2_gps_ascii{};

// TODO: Define the subfields for the ECEF/Relative Ephemeris struct (if necessary).
// See Section 9.4.3 in the VITA 49.2 2017 document for more information.
struct vita49_2_ecef_relative_ephemeris{};

// TODO: Define the subfields for the Context Association Lists struct (if necessary).
// See Section 9.13.2 in the VITA 49.2 2017 document for more information.
struct vita49_2_context_association_lists{};

struct vita49_2_device_identifier {

	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	
	struct {
		
		uint32_t oui:24;				/* OUI code, either IEEE-defined or provided by VITA */
		uint32_t reserved_24_31:8; 		/* Reserved bits should be zeroed out. */
	
	} lower_word;

	struct {
		
		uint32_t device_code:16;		/* Unique device identifier such as a serial number */
		uint32_t reserved_16_31:16;		/* Reserved bits should be zeroed out. */
	
	} upper_word;

	#else

	struct {
		
		uint32_t reserved_24_31:8; 		/* Reserved bits should be zeroed out. */
		uint32_t oui:24;				/* OUI code, either IEEE-defined or provided by VITA */
	
	} lower_word;

	struct {
		
		uint32_t reserved_16_31:16		/* Reserved bits should be zeroed out. */
		uint32_t device_code:16;		/* Unique device identifier such as a serial number */
	
	} upper_word;

	#endif
};

/**
 * @struct vita49_2_cif0_fields
 * @brief Parsed representation of Context Indicator Field 0 (CIF0) payload.
 *
 * This structure holds the decoded information from an IF Context packet's
 * payload, representing varying system and signal state attributes.
 */
struct vita49_2_cif0_fields {

	// See Table 9.1-1 in the full VITA 49.2 Specification

	/* The raw Context Indicator Field 0 word */
	struct cif0_word {
		#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		
		// These are fields that are unlikely to be used, however ADI retains the right to implement them in the future
		uint32_t reserved_0:1;							/* Reserved bit should be zeroed out */
		uint32_t cif1_enable:1;							/**< True if a CIF1 word and associated payload are present (Bit 1) */
		uint32_t cif2_enable:1;							/**< True if a CIF2 word and associated payload are present (Bit 2) */
		uint32_t cif3_enable:1;							/**< True if a CIF3 word and associated payload are present (Bit 3) */
		uint32_t reserved_6_4:3;						/* Reserved bits should be zeroed out */
		uint32_t cif7_enable:1;							/**< True if a CIF7 word and associated payload are present (Bit 7) */
		uint32_t has_context_association_lists:1;		/**< True if Context Association Lists are present (Bit 8) */
		uint32_t has_gps_ascii:1;						/**< True if the GPS ASCII payload is present (Bit 9) */
		uint32_t has_ephemeris_ref_id:1;				/**< True if Ephemeris Reference ID is present (Bit 10) */
		uint32_t has_relative_ephemeris:1;				/**< True if Relative Ephemeris is present (Bit 11) */
		uint32_t has_ecef_ephemeris:1;					/**< True if ECEF Ephemeris is present (Bit 12) */
		uint32_t has_formatted_ins:1;					/**< True if the Formatted INS payload is present (Bit 13) */
		uint32_t has_formatted_gps:1;					/**< True if the Formatted GPS payload is present (Bit 14) */
		uint32_t has_data_packet_payload_format:1; 		/**< True if Data Packet Payload Format is present (Bit 15) */
		uint32_t has_state_and_event_indicators:1; 		/**< True if State/Event Indicators are present (Bit 16) */
		uint32_t has_device_identifier:1;     			/**< True if Device Identifier is present (Bit 17) */
		uint32_t has_temperature:1;        				/**< True if Temperature is present (Bit 18) */
		uint32_t has_timestamp_calibration_time:1;     	/**< True if Timestamp Calibration Time is present (Bit 19) */
		uint32_t has_timestamp_adjustment:1; 			/**< True if Timestamp Adjustment is present (Bit 20) */
		uint32_t has_sample_rate:1;        				/**< True if Sample Rate is present (Bit 21) */
		uint32_t has_over_range_count:1;   				/**< True if Over-Range Count is present (Bit 22) */
		uint32_t has_gain:1;               				/**< True if Gain is present (Bit 23) */
		uint32_t has_reference_level:1;    				/**< True if Reference Level is present (Bit 24) */
		uint32_t has_if_band_offset:1;     				/**< True if IF Band Offset is present (Bit 25) */
		uint32_t has_rf_reference_frequency_offset:1; 	/**< True if RF Reference Frequency Offset is present (Bit 26) */
		uint32_t has_rf_reference_frequency:1; 			/**< True if RF Reference Frequency is present (Bit 27) */
		uint32_t has_if_reference_frequency:1; 			/**< True if IF Reference Frequency is present (Bit 28) */
		uint32_t has_bandwidth:1;          				/**< True if Bandwidth is present (Bit 29) */
		uint32_t has_reference_point_id:1; 				/**< True if Reference Point Identifier is present (Bit 30) */
		uint32_t context_field_change:1; 				/**< True if any context field has changed since the previous Context Packet was sent(Bit 31) */
		
		#else

		uint32_t context_field_change:1; 				/**< True if any context field has changed since the previous Context Packet was sent(Bit 31) */
		uint32_t has_reference_point_id:1; 				/**< True if Reference Point Identifier is present (Bit 30) */
		uint32_t has_bandwidth:1;          				/**< True if Bandwidth is present (Bit 29) */
		uint32_t has_if_reference_frequency:1; 			/**< True if IF Reference Frequency is present (Bit 28) */
		uint32_t has_rf_reference_frequency:1; 			/**< True if RF Reference Frequency is present (Bit 27) */
		uint32_t has_rf_reference_frequency_offset:1; 	/**< True if RF Reference Frequency Offset is present (Bit 26) */
		uint32_t has_if_band_offset:1;     				/**< True if IF Band Offset is present (Bit 25) */
		uint32_t has_reference_level:1;    				/**< True if Reference Level is present (Bit 24) */
		uint32_t has_gain:1;               				/**< True if Gain is present (Bit 23) */
		uint32_t has_over_range_count:1;   				/**< True if Over-Range Count is present (Bit 22) */
		uint32_t has_sample_rate:1;        				/**< True if Sample Rate is present (Bit 21) */
		uint32_t has_timestamp_adjustment:1; 			/**< True if Timestamp Adjustment is present (Bit 20) */
		uint32_t has_timestamp_calibration_time:1;     	/**< True if Timestamp Calibration Time is present (Bit 19) */
		uint32_t has_temperature:1;        				/**< True if Temperature is present (Bit 18) */
		uint32_t has_device_identifier:1;     			/**< True if Device Identifier is present (Bit 17) */
		uint32_t has_state_and_event_indicators:1; 		/**< True if State/Event Indicators are present (Bit 16) */
		uint32_t has_data_packet_payload_format:1; 		/**< True if Data Packet Payload Format is present (Bit 15) */
		uint32_t has_formatted_gps:1;					/**< True if the Formatted GPS payload is present (Bit 14) */
		uint32_t has_formatted_ins:1;					/**< True if the Formatted INS payload is present (Bit 13) */
		uint32_t has_ecef_ephemeris:1;					/**< True if ECEF Ephemeris is present (Bit 12) */
		uint32_t has_relative_ephemeris:1;				/**< True if Relative Ephemeris is present (Bit 11) */
		uint32_t has_ephemeris_ref_id:1;				/**< True if Ephemeris Reference ID is present (Bit 10) */
		uint32_t has_gps_ascii:1;						/**< True if the GPS ASCII payload is present (Bit 9) */
		uint32_t has_context_association_lists:1;		/**< True if Context Association Lists are present (Bit 8) */
		uint32_t cif7_enable:1;							/**< True if a CIF7 word and associated payload are present (Bit 7) */
		uint32_t reserved_6_4:3;						/* Reserved bits should be zeroed out */
		uint32_t cif3_enable:1;							/**< True if a CIF3 word and associated payload are present (Bit 3) */
		uint32_t cif2_enable:1;							/**< True if a CIF2 word and associated payload are present (Bit 2) */
		uint32_t cif1_enable:1;							/**< True if a CIF1 word and associated payload are present (Bit 1) */
		uint32_t reserved_0:1;							/* Reserved bit should be zeroed out */

		#endif
	} cif0_word;

	// NOTE: "Context Field Change Indicator" doesn't have an actual payload field. 
	// It's simply an indicator and can exist in the CIF0 word

	uint32_t reference_point_id;  										/**< Reference Point Identifier */

	double bandwidth;            			 							/**< Bandwidth in Hz */

	double if_reference_frequency;    									/**< IF Reference Frequency in Hz */
	double rf_reference_frequency;    									/**< RF Reference Frequency in Hz */	
	double rf_reference_frequency_offset;    							/**< RF Reference Frequency Offset in Hz */
	double if_band_offset;        										/**< IF Band Offset in Hz */

	float reference_level;        										/**< Reference Level in dBm */

	float gain_stage_1;           										/**< Gain Stage 1 in dB */
	float gain_stage_2;          										/**< Gain Stage 2 in dB */

	uint32_t over_range_count;    										/**< Over-Range Count */

	double sample_rate;          	 									/**< Sample Rate in Hz */

	int64_t timestamp_adjustment;  										/**< Timestamp Adjustment in picoseconds */

	uint32_t timestamp_calibration_time_int;  							/**< Integer part of Calibration Time */

	float temperature;            										/**< Temperature in degrees Celsius */

	struct vita49_2_device_identifier device_identifier;				/**< Device Identifier = OUI and Device Code */

	uint32_t state_and_event_indicators;  								/**< State and Event indicators bitmap */

	struct vita49_2_data_payload_format data_packet_payload_format;  	/**< Payload Format specific bits */

	struct vita49_2_formatted_gps_ins formatted_gps;					/**< Conveying GPS information */
	struct vita49_2_formatted_gps_ins formatted_ins;					/**< Conveying INS information */

	struct vita49_2_ecef_relative_ephemeris ecef_ephemeris;				/**< Conveying ECEF ephemeris */
	struct vita49_2_ecef_relative_ephemeris relative_ephemeris;			/**< Conveying relative ephemeris */
	uint32_t ephemeris_ref_id;											/**< When relative ephemeris is reported, ephemeris ref id is used to identify the object whose location serves as the origin for the relative ephemeris */

	struct vita49_2_gps_ascii gps_ascii;								/**< Some devices output information in formatted ASCII strings ("GPS sentences") */

	struct vita49_2_context_association_lists context_association_lists;	/**< CALs allow a data packet to be associated with multiple context packets from relevant reference points or systems */
};

// TODO: Define a struct to represent CIF1 similarly to how we've defined CIF0 below.
struct vita49_2_cif1_fields {
	struct cif1_word {} cif1_word;
};

// TODO: Define a struct to represent CIF2 similarly to how we've defined CIF0 below.
struct vita49_2_cif2_fields {
	struct cif2_word {} cif2_word;
};

// TODO: Define a struct to represent CIF3 similarly to how we've defined CIF0 below.
struct vita49_2_cif3_fields {
	struct cif3_word {} cif3_word;
};

// TODO: Define a struct to represent CIF7 similarly to how we've defined CIF0 below.
struct vita49_2_cif7_fields {
	struct cif7_word {} cif7_word;
};

/**
 * @struct vita49_2_warnings
 * @brief Contains CIF0-7 words as well as the payload where the warning indicator values will live.
 * 
 */
struct vita49_2_warnings {
	struct cif0_word cif0_warnings;	/* Indicates which of the CIF0 attributes had warnings */

	// Not currently in use, but ADI retains the right to implement these fields in the future.
	// Using pointers since embedding each of these structs would result in a lot of bloat if they're unused.
	struct cif1_word* cif1_warnings;	
	struct cif2_word* cif2_warnings;
	struct cif3_word* cif3_warnings;
	struct cif7_word* cif7_warnings;

	// Each CIF field that produced a warning gets a 32-bit warning indicator word in the payload
	// to indicate what kind of warning occured (see Table 8.4.1.2.1-1 in the VITA 49.2 2017 document
	// for the list of predefined warnings/errors)
	struct vita49_2_warning_error_indicators* warnings_payload; 	/* Pointer to the start of the payload words */
	uint16_t warnings_payload_num_words;     					/* Number of 32-bit words in the payload */
};

/**
 * @struct vita49_2_errors
 * @brief Contains CIF0-7 words as well as the payload where the error indicator values will live.
 * 
 */
struct vita49_2_errors {
	struct cif0_word cif0_errors;	/* Indicates which of the CIF0 had errors */

	// Not currently in use, but ADI retains the right to implement these fields in the future.
	// Using pointers since embedding each of these structs would result in a lot of bloat if they're unused.
	struct cif1_word* cif1_errors;	
	struct cif2_word* cif2_errors;
	struct cif3_word* cif3_errors;
	struct cif7_word* cif7_errors;

	// Each CIF field that produced a warning gets a 32-bit warning indicator word in the payload
	// to indicate what kind of warning occured (see Table 8.4.1.2.1-1 in the VITA 49.2 2017 document
	// for the list of predefined warnings/errors)
	struct vita49_2_warning_error_indicators* errors_payload;  	/* Pointer to the start of the payload words */
	uint16_t errors_payload_num_words;     						/* Number of 32-bit words in the payload */
};




#if defined(_WIN32)
#  if defined(LIBIIO_EXPORTS)
#    define __vrt_api __declspec(dllexport)
#  else
#    define __vrt_api __declspec(dllimport)
#  endif
#elif __GNUC__ >= 4 || defined(__clang__)
#  define __vrt_api __attribute__((visibility ("default")))
#else
#  define __vrt_api
#endif


// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Converts a double to a 64-bit integer in 44.20 format (meaning the radix point is to the left of the first 20 bits).
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api int64_t convert_to_44_20(double value);

/**
 * @brief Converts a 64-bit integer in 44.20 format (meaning the radix point is to the left of the first 20 bits) back to a double.
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api double convert_from_44_20(int64_t value);

/**
 * @brief Converts a 16-bit FP-value to a 16-bit integer in 10.6 format (meaning the radix point is to the left of the first 6 bits).
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api int16_t convert_to_10_6(float value);

/**
 * @brief Converts a 16-bit integer in 10.6 format (meaning the radix point is to the left of the first 6 bits) back to a float.
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api float convert_from_10_6(int16_t value);


/**
 * @brief Converts a 16-bit FP-value to a 16-bit integer in 9.7 format (meaning the radix point is to the left of the first 7 bits).
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api int16_t convert_to_9_7(float value);

/**
 * @brief Converts a 16-bit integer in 9.7 format (meaning the radix point is to the left of the first 7 bits) back to a float.
 * 
 * @param value 
 * @return __vrt_api 
 */
__vrt_api float convert_from_9_7(int16_t value);

/**
 * @brief Parses the CIF0 payload section.
 * Evaluates the flags present in CIF0 to sequentially decode the context payload.
 * 
 * IMPORTANT: The struct pointed to by the cif0 pointer MUST have it's CIF0 word populated beforehand.
 * 
 * Returns a negative value on error. Otherwise a value indicating the number of 32-bit words that were read from the buffer is returned.
 * 
 * @param payload_size Size of the payload minus any of the 32-bit CIF-words (CIF0/1/2/3/7).
 * @param payload Pointer to the payload section containing the CIF0 attribute values. Do NOT provide a pointer to the start of the CIF0 word.
 * @param cif0 Pointer to memory that's already been allocated for a vita49_2_cif0_fields struct.
 * @return ssize_t 
 */
__vrt_api ssize_t vita49_2_parse_cif0_payload(uint16_t payload_size, const uint32_t* const payload, struct vita49_2_cif0_fields *cif0);

/**
 * @brief Extracts a 32-bit word from the packet payload, handling network byte-order translation.
 * 
 * Returns a negative value on failure.
 * 
 * @param payload Pointer to the start of the payload. 
 * @param payload_size Size of the payload in 32-bit words.
 * @param offset Location of the field in 32-bit words relative to the payload pointer.
 * @param payload_word Pointer to a uint32_t that can be used to store the payload value.
 * @return int 
 */
__vrt_api int vita49_2_get_payload_word(const uint32_t* const payload, uint16_t payload_size, size_t offset, uint32_t* const payload_word);


/* Inserts a 32-bit word into a raw payload buffer in network byte-order. */
__vrt_api void vita49_2_set_payload_word(uint32_t *payload, size_t max_words, size_t offset, uint32_t val);

/**
 * @brief Extracts an IEEE 754 64-bit float from the packet payload, handling network byte-order translation.
 * 
 * @param payload 
 * @param payload_size 
 * @param offset 
 * @return double 
 */
__vrt_api double vita49_2_get_payload_double(const uint32_t* const payload, uint16_t payload_size, size_t offset);

/**
 * @brief Extracts a 64-bit signed integer from the packet payload, handling network byte-order translation.
 * 
 * @param payload 
 * @param payload_size 
 * @param offset 
 * @return int64_t 
 */
__vrt_api int64_t vita49_2_get_payload_int64(const uint32_t* const payload, uint16_t payload_size, size_t offset);


/* Inserts an IEEE 754 64-bit float into a raw payload buffer in network byte-order. */
__vrt_api void vita49_2_set_payload_double(uint32_t *payload, size_t max_words, size_t offset, double val);

#endif /* __VITA49_2_PACKET_ELEMENTS_H__ */
