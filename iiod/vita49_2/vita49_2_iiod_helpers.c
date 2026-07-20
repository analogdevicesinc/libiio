#include "vita49_2_iiod_helpers.h"

#include <errno.h>
#include <string.h>

enum vita49_2_warnings_error_codes get_available_attribute_values(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, char* available_range, size_t buffer_size)
{
    if (ctx == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL || available_range == NULL || buffer_size == 0)
        return -EBADARGS;

	// Find device and channel
	struct iio_device *device;
	struct iio_channel *channel;
	struct iio_attr *attribute;

	device = iio_context_find_device(ctx, device_name);
	if (!device) 
	{
		fprintf(stderr, "vita49_2_process: Device %s not found for mapping.\n", device_name);
		return -ENOFIELD;
	}

	int ret_value;

	// Appending "_available" to access the fd containing the valid range of values for this attribute
	char available_options_fd_name[74];
	snprintf(available_options_fd_name, sizeof(available_options_fd_name), "%s_available", attribute_name);

    // Attribute we're modifying is associated with the device as a whole
	if (strcmp(channel_name, "device") == 0)
	{
    	attribute = iio_device_find_attr(device, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find device attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from device attribute '%s' failed.\n", available_options_fd_name);
		}
    }
    // Attribute we're modifying is a debug attribute (advanced configuration)
    else if (strcmp(channel_name, "debug") == 0)
    {
        attribute = iio_device_find_debug_attr(device, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find debug attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from debug attribute '%s' failed.\n", available_options_fd_name);
		}
    }
	// Attribute we're modifying is associated with a specific channel so we must find that channel first.
    else
	{
		channel = iio_device_find_channel(device, channel_name, is_output);
		if (!channel)
		{
			fprintf(stderr, "vita49_2_process: Channel %s (%s) not found.\n", available_options_fd_name, is_output ? "out" : "in");
			return -ENOFIELD;
		}

		// Now to find the channel attribute
		attribute = iio_channel_find_attr(channel, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find channel attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from channel attribute '%s' failed.\n", available_options_fd_name);
		}
	} 

	if (ret_value == -ENOSYS || ret_value == -ENOENT)	
		return -ENOFIELD;
	else
		return ret_value;
}

enum vita49_2_warnings_error_codes validate_command_ll(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, long long new_value)
{
	if (ctx == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[256];
	
	int ret_value = get_available_attribute_values(ctx, device_name, channel_name, attribute_name, is_output, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		uint32_t values[3];
		int successes = sscanf(available_range, "[%lld %lld %lld]", &values[0], &values[1], &values[2]);

		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		if (((new_value - values[0]) % values[1]) != 0)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		uint32_t values[10];
		int successes = sscanf(available_range, "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some kind of error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_d(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, double new_value)
{
	if (ctx == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[256];
	
	int ret_value = get_available_attribute_values(ctx, device_name, channel_name, attribute_name, is_output, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		double values[3];
		int successes = sscanf(available_range, "[%lf %lf %lf]", &values[0], &values[1], &values[2]);
		
		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		// With double/fps, one extra step is to check if it's below some threshold to qualify as 0.
		double remainder = fmod((new_value - values[0]), values[1]);
		if (fabs(remainder) > 1e-9)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		double values[10];
		int successes = sscanf(available_range, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < successes; i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_s(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, const char* const new_value)
{
	if (ctx == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL || new_value)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[256];
	
	int ret_value = get_available_attribute_values(ctx, device_name, channel_name, attribute_name, is_output, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Since we're dealing with string arguments, we should NOT see the first option
	if (available_range[0] == '[')
	{
		return -EOUTRANGE;
	}
	// A set of discrete values is what we expect for strings
	else
	{
		uint32_t values[10][30];
		int successes = sscanf(available_range, "%s %s %s %s %s %s %s %s %s %s",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
		{
			if (strcmp(new_value, values[i]) == 0)
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some kind of error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes vita49_2_find_iio_attribute(const struct iio_context* const ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, const struct iio_attr** attribute)
{
	// Arguments check
	if (ctx == NULL || device_name == NULL || channel_name == NULL || attribute_name == NULL)
		return -EBADARGS;

	struct iio_device* device = iio_context_find_device(ctx, device_name);
	if (!device)
	{
		return -ENOFIELD;
	}

	*attribute = NULL;

	// Attribute we're modifying is associated with the device as a whole
	if (strcmp(channel_name, "device") == 0)
	{
		*attribute = iio_device_find_attr(device, attribute_name);
	} 
	// Attribute we're modifying is a debug attribute (advanced configuration)
	else if (strcmp(channel_name, "debug") == 0)
	{
		*attribute = iio_device_find_debug_attr(device, attribute_name);
	}        
	// Attribute we're modifying is associated with a specific channel so we must find that channel first.
	else
	{
		struct iio_channel* channel = iio_device_find_channel(device, channel_name, is_output);
		if (!channel) 
		{
			return -ENOFIELD;
		}

		*attribute = iio_channel_find_attr(channel, attribute_name);
	} 

	if (*attribute == NULL) 
	{
		return -ENOFIELD;
	}

	return ENONE;
}