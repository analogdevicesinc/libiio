classdef buffer < handle
    %% buffer methods
    methods (Static)
        function devPtr = iio_buffer_get_device(buffPtr)
            % Retrieve a pointer to the iio_device structure
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            % 
            % Returns:
            %   A pointer to an iio_device structure
            %
            % libiio function: iio_buffer_get_device

            if coder.target('MATLAB')
                devPtr = adi.libiio.helpers.calllibADI('iio_buffer_get_device', buffPtr);
            else
                devPtr = coder.opaque('const struct iio_device*', 'NULL');
                devPtr = coder.ceval('iio_buffer_get_device', buffPtr);
            end
        end

        function count = iio_buffer_get_attrs_count(buffPtr)
            % Enumerate the attributes of the given buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            % 
            % Returns:
            %   The number of buffer-specific attributes found
            %
            % libiio function: iio_buffer_get_attrs_count

            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_buffer_get_attrs_count', buffPtr);
            else
                count = coder.ceval('iio_buffer_get_attrs_count', buffPtr);
            end
        end

        function attrPtr = iio_buffer_get_attr(buffPtr, index)
            % Get the buffer-specific attribute present at the given index
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %   index: The index corresponding to the attribute
            % 
            % Returns:
            %   On success, a pointer to an iio_attr structure.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_buffer_get_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_buffer_get_attr', buffPtr, index);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_buffer_get_attr', buffPtr, index);
            end
        end

        function attrPtr = iio_buffer_find_attr(buffPtr, name)
            % Try to find a buffer-specific attribute by its name
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %   name: A NULL-terminated string corresponding to the name 
            %       of the attribute
            % 
            % Returns:
            %   On success, a pointer to a static NULL-terminated string.
            %   If the name does not correspond to any known attribute of the given
            %   channel, NULL is returned
            %
            % libiio function: iio_buffer_find_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_buffer_find_attr', buffPtr, name);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_buffer_find_attr', buffPtr, adi.libiio.helpers.ntstr(name));
            end
        end

        function buffPtr = iio_device_create_buffer(devPtr, idx, maskPtr)
            % Create an input or output buffer associated to the given device
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure
            %   idx: The index of the hardware buffer. Should be 0 in 
            %       most cases.
            %   maskPtr: A pointer to an iio_channels_mask structure
            % 
            % Returns:
            %   On success, a pointer to an iio_buffer structure.
            %   On failure, a pointer-encoded error is returned.
            %
            % libiio function: iio_device_create_buffer

            if coder.target('MATLAB')
                buffPtr = adi.libiio.helpers.calllibADI('iio_device_create_buffer', devPtr, idx, maskPtr);
            else
                buffPtr = coder.opaque('struct iio_buffer*', 'NULL');
                buffPtr = coder.ceval('iio_device_create_buffer', devPtr, idx, maskPtr);
            end
        end

        function iio_buffer_set_data(buffPtr, dataPtr)
            % Associate a pointer to an iio_buffer structure
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %   dataPtr: The pointer to be associated
            %
            % libiio function: iio_buffer_set_data

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_buffer_set_data', buffPtr, dataPtr);
            else
                coder.ceval('iio_buffer_set_data', buffPtr, dataPtr);
            end
        end

        function dataPtr = iio_buffer_get_data(buffPtr)
            % Retrieve a previously associated pointer of an iio_channel structure
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % Returns:
            %   The pointer previously associated if present, or NULL
            %
            % libiio function: iio_buffer_get_data

            if coder.target('MATLAB')
                dataPtr = adi.libiio.helpers.calllibADI('iio_buffer_get_data', buffPtr);
            else
                dataPtr = coder.opaque('void*', 'NULL'); 
                dataPtr = coder.ceval('iio_buffer_get_data', buffPtr);
            end
        end

        function iio_buffer_destroy(buffPtr)
            % Destroy the given buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % libiio function: iio_buffer_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_buffer_destroy', buffPtr);
            else
                coder.ceval('iio_buffer_destroy', buffPtr);
            end
        end

        function iio_buffer_cancel(buffPtr)
            % This function cancels all outstanding buffer operations 
            % previously scheduled. This means that any pending 
            % iio_block_enqueue() or iio_block_dequeue() operation will 
            % abort and return immediately, any further invocation of these
            % functions on the same buffer will return immediately with an 
            % error.
            %
            % Usually iio_block_dequeue() will block until all data has 
            % been transferred or a timeout occurs. This can depending on 
            % the configuration take a significant amount of time. 
            % iio_buffer_cancel() is useful to bypass these conditions if 
            % the buffer operation is supposed to be stopped in response to
            % an external event (e.g. user input).
            %
            % To be able to transfer additional data after calling this 
            % function the buffer should be destroyed and then re-created.
            %
            % This function can be called multiple times for the same 
            % buffer, but all but the first invocation will be without 
            % additional effect.
            %
            % This function is thread-safe, but not signal-safe, i.e. it 
            % must not be called from a signal handler.
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % libiio function: iio_buffer_cancel

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_buffer_cancel', buffPtr);
            else
                coder.ceval('iio_buffer_cancel', buffPtr);
            end
        end

        function status = iio_buffer_enable(buffPtr)
            % Enable the buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % Returns:
            %   On success, 0
            %   On error, a negative error code is returned
            %
            % libiio function: iio_buffer_enable

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_buffer_enable', buffPtr);
            else
                status = coder.ceval('iio_buffer_enable', buffPtr);
            end
        end

        function status = iio_buffer_disable(chnPtr, maskPtr)
            % Disable the buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % Returns:
            %   On success, 0
            %   On error, a negative error code is returned
            %
            % libiio function: iio_buffer_disable

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_buffer_disable', chnPtr, maskPtr);
            else
                status = coder.ceval('iio_buffer_disable', chnPtr, maskPtr);
            end
        end

        function channelsMaskPtr = iio_buffer_get_channels_mask(chnPtr, maskPtr)
            % Retrieve a mask of the channels enabled for the given buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure
            %
            % Returns:
            %   A pointer to an iio_channels_mask structure
            %
            % NOTE: The mask returned may contain more enabled channels 
            %   than the mask used for creating the buffer.
            %
            % libiio function: iio_buffer_get_channels_mask

            if coder.target('MATLAB')
                channelsMaskPtr = adi.libiio.helpers.calllibADI('iio_buffer_get_channels_mask', chnPtr, maskPtr);
            else
                channelsMaskPtr = coder.opaque('const struct iio_channels_mask*', 'NULL');
                channelsMaskPtr = coder.ceval('iio_buffer_get_channels_mask', chnPtr, maskPtr);
            end
        end
    end
end