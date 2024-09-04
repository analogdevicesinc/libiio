classdef channel < handle
    %% channel methods
    methods (Static)
        function devPtr = iio_channel_get_device(chnPtr)
            % Retrieve a pointer to the iio_device structure
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   A pointer to an iio_device structure
            %
            % libiio function: iio_channel_get_device

            if coder.target('MATLAB')
                devPtr = adi.libiio.helpers.calllibADI('iio_channel_get_device', chnPtr);
            else
                devPtr = coder.opaque('const struct iio_device*', 'NULL');
                devPtr = coder.ceval('iio_channel_get_device', chnPtr);
            end
        end

        function id = iio_channel_get_id(chnPtr)
            % Retrieve the channel ID (e.g. voltage0)
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   A pointer to a static NULL-terminated string
            %
            % libiio function: iio_channel_get_id

            if coder.target('MATLAB')
                id = adi.libiio.helpers.calllibADI('iio_channel_get_id', chnPtr);
            else
                id = coder.ceval('iio_channel_get_id', chnPtr);
            end
        end

        function name = iio_channel_get_name(chnPtr)
            % Retrieve the channel name (e.g. vccint)
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   A pointer to a static NULL-terminated string
            %
            % NOTE:
            %   If the channel has no name, NULL is returned.
            %
            % libiio function: iio_channel_get_name

            if coder.target('MATLAB')
                name = adi.libiio.helpers.calllibADI('iio_channel_get_name', chnPtr);
            else
                name = coder.ceval('iio_channel_get_name', chnPtr);
            end
        end

        function status = iio_channel_is_output(chnPtr)
            % Return True if the given channel is an output channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   True if the channel is an output channel, False otherwise
            %
            % libiio function: iio_channel_is_output

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_channel_is_output', chnPtr);
            else
                status = coder.ceval('iio_channel_is_output', chnPtr);
            end
        end

        function status = iio_channel_is_scan_element(chnPtr)
            % Return True if the given channel is a scan element
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   True if the channel is a scan element, False otherwise
            %
            % NOTE:
            %   A channel that is a scan element is a channel that can
            %   generate samples (for an input channel) or receive samples 
            %   (for an output channel) after being enabled.
            %
            % libiio function: iio_channel_is_scan_element

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_channel_is_scan_element', chnPtr);
            else
                status = coder.ceval('iio_channel_is_scan_element', chnPtr);
            end
        end

        function count = iio_channel_get_attrs_count(chnPtr)
            % Enumerate the channel-specific attributes of the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            % 
            % Returns:
            %   The number of channel-specific attributes found
            %
            % libiio function: iio_channel_get_attrs_count

            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_channel_get_attrs_count', chnPtr);
            else
                count = coder.ceval('iio_channel_get_attrs_count', chnPtr);
            end
        end

        function attrPtr = iio_channel_get_attr(chnPtr, index)
            % Get the channel-specific attribute present at the given index
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   index: The index corresponding to the attribute
            % 
            % Returns:
            %   On success, a pointer to a static NULL-terminated string.
            %   If the index is invalid, NULL is returned
            %
            % libiio function: iio_channel_get_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_channel_get_attr', chnPtr, index);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_channel_get_attr', chnPtr, index);
            end
        end

        function attrPtr = iio_channel_find_attr(chnPtr, name)
            % Try to find a channel-specific attribute by its name
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   name: A NULL-terminated string corresponding to the name 
            %       of the attribute
            % 
            % Returns:
            %   On success, a pointer to a static NULL-terminated string.
            %   If the name does not correspond to any known attribute of the given
            %   channel, NULL is returned
            %
            % libiio function: iio_channel_find_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_channel_find_attr', chnPtr, name);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_channel_find_attr', chnPtr, adi.libiio.helpers.ntstr(name));
            end
        end

        function iio_channel_enable(chnPtr, maskPtr)
            % Enable the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   maskPtr: The channels mask to manipulate
            %
            % libiio function: iio_channel_enable

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channel_enable', chnPtr, maskPtr);
            else
                coder.ceval('iio_channel_enable', chnPtr, maskPtr);
            end
        end

        function iio_channel_disable(chnPtr, maskPtr)
            % Disable the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   maskPtr: The channels mask to manipulate
            %
            % libiio function: iio_channel_disable

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channel_disable', chnPtr, maskPtr);
            else
                coder.ceval('iio_channel_disable', chnPtr, maskPtr);
            end
        end

        function status = iio_channel_is_enabled(chnPtr, maskPtr)
            % Returns True if the channel is enabled
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   maskPtr: The channels mask to manipulate
            % 
            % Returns:
            %   True if the channel is enabled, False otherwise
            %
            % libiio function: iio_channel_is_enabled

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_channel_is_enabled', chnPtr, maskPtr);
            else
                status = coder.ceval('iio_channel_is_enabled', chnPtr, maskPtr);
            end
        end

        function size = iio_channel_read(chnPtr, blockPtr, dstPtr, len, raw)
            % Demultiplex and convert the samples of a given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   blockPtr: A pointer to an iio_block structure
            %   dstPtr: A pointer to the memory area where the converted 
            %       data will be stored
            %   len: The available length of the memory area, in bytes
            %   raw: True to read samples in the hardware format, false to read
            %       converted samples
            % 
            % Returns:
            %   The size of the converted data, in bytes
            %
            % libiio function: iio_channel_read

            if coder.target('MATLAB')
                size = adi.libiio.helpers.calllibADI('iio_channel_read', chnPtr, blockPtr, dstPtr, len, raw);
            else
                size = coder.ceval('iio_channel_read', chnPtr, blockPtr, dstPtr, len, raw);
            end
        end

        function size = iio_channel_write(chnPtr, blockPtr, dstPtr, len, raw)
            % Convert and multiplex the samples of a given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   blockPtr: A pointer to an iio_block structure
            %   srcPtr: A pointer to the memory area where the sequential data will
            %       be read from
            %   len: The available length of the memory area, in bytes
            %   raw: True if the samples are already in hardware format, false if they
            %       need to be converted
            % 
            % Returns:
            %   The number of bytes actually converted and multiplexed
            %
            % libiio function: iio_channel_write

            if coder.target('MATLAB')
                size = adi.libiio.helpers.calllibADI('iio_channel_write', chnPtr, blockPtr, dstPtr, len, raw);
            else
                size = coder.ceval('iio_channel_write', chnPtr, blockPtr, dstPtr, len, raw);
            end
        end

        function iio_channel_set_data(chnPtr, dataPtr)
            % Associate a pointer to an iio_channel structure
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %   dataPtr: The pointer to be associated
            %
            % libiio function: iio_channel_set_data

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channel_set_data', chnPtr, dataPtr);
            else
                coder.ceval('iio_channel_set_data', chnPtr, dataPtr);
            end
        end

        function dataPtr = iio_channel_get_data(chnPtr)
            % Retrieve a previously associated pointer of an iio_channel structure
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %
            % Returns:
            %   The pointer previously associated if present, or NULL
            %
            % libiio function: iio_channel_get_data

            if coder.target('MATLAB')
                dataPtr = adi.libiio.helpers.calllibADI('iio_channel_get_data', chnPtr);
            else
                dataPtr = coder.opaque('void*', 'NULL'); 
                dataPtr = coder.ceval('iio_channel_get_data', chnPtr);
            end
        end

        function chnType = iio_channel_get_type(chnPtr)
            % Get the type of the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %
            % Returns:
            %   The type of the channel
            %
            % libiio function: iio_channel_get_type

            if coder.target('MATLAB')
                chnType = adi.libiio.helpers.calllibADI('iio_channel_get_type', chnPtr);
            else
                chnType = coder.ceval('iio_channel_get_type', chnPtr);
            end
        end

        function modType = iio_channel_get_modifier(chnPtr)
            % Get the modifier type of the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure
            %
            % Returns:
            %   The modifier type of the channel
            %
            % libiio function: iio_channel_get_modifier

            if coder.target('MATLAB')
                modType = adi.libiio.helpers.calllibADI('iio_channel_get_modifier', chnPtr);
            else
                modType = coder.ceval('iio_channel_get_modifier', chnPtr);
            end
        end
    end

    % wrappers to maintain backwards-compatibility
    methods (Hidden, Static)
        function [status, value] = iio_channel_attr_read_bool(chanPtr,attr)
            attrPtr = adi.libiio.channel.iio_channel_find_attr(chanPtr,attr);
            % cstatus(status,['Attribute: ' attr ' not found']);            
            [status, value] = adi.libiio.attribute.iio_attr_read_bool(attrPtr);
        end

        function [status, value] = iio_channel_attr_read_longlong(chanPtr,attr)
            attrPtr = adi.libiio.channel.iio_channel_find_attr(chanPtr,attr);
            % cstatus(status,['Attribute: ' attr ' not found']);            
            [status, value] = adi.libiio.attribute.iio_attr_read_longlong(attrPtr);
        end

        function status = iio_channel_attr_write_bool(chanPtr,attr,value)
            attrPtr = adi.libiio.channel.iio_channel_find_attr(chanPtr,attr);
            % cstatus(status,['Attribute: ' attr ' not found']);
            status = adi.libiio.attribute.iio_attr_write_bool(attrPtr, value);
        end

        function status = iio_channel_attr_write_longlong(chanPtr,attr,value)
            attrPtr = adi.libiio.channel.iio_channel_find_attr(chanPtr,attr);
            % cstatus(status,['Attribute: ' attr ' not found']);
            status = adi.libiio.attribute.iio_attr_write_longlong(attrPtr, value);
        end

        function nBytes = iio_channel_attr_write(chanPtr, attr, src)
            attrPtr = adi.libiio.channel.iio_channel_find_attr(chanPtr,attr);
            % cstatus(status,['Attribute: ' attr ' not found']);
            nBytes = adi.libiio.attribute.iio_attr_write_string(attrPtr, src);
        end
    end
end