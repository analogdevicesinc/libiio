classdef (Abstract) lowlevel < handle
    methods (Static)
        %% low-level methods
        function chnsMaskPtr = iio_create_channels_mask(value)
            % Create a new empty channels mask
            %
            % Args:
            %   value: The number of channels in the mask.
            % 
            % Returns:
            %   On success, a pointer to an iio_channels_mask structure.
            %   On error, NULL is returned.
            %
            % libiio function: iio_create_channels_mask
            
            validateattributes(value, { 'double','single' }, ...
                {'real', 'scalar', 'finite', 'nonnan', 'nonempty', ...
                'nonnegative', 'integer'});
            
            if coder.target('MATLAB')
                chnsMaskPtr = adi.libiio.helpers.calllibADI('iio_create_channels_mask', value);
            else
                chnsMaskPtr = coder.opaque('struct iio_channels_mask*', 'NULL');
                chnsMaskPtr = coder.ceval('iio_create_channels_mask', value);
            end
        end
            
        function iio_channels_mask_destroy(maskPtr)
            % Destroy a channels mask
            %
            % Args:
            %   maskPtr: A pointer to an iio_channels_mask structure.
            %
            % libiio function: iio_channels_mask_destroy
            
            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channels_mask_destroy', maskPtr);
            else
                coder.ceval('iio_channels_mask_destroy', maskPtr);
            end
        end

        function value = iio_device_get_sample_size(devPtr, maskPtr)
            % Get the current sample size
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            %   maskPtr: A pointer to an iio_channels_mask structure.
            % 
            % Returns:
            %   On success, the sample size in bytes.
            %   On error, a negative errno code is returned.
            %
            % NOTE: The sample size is not constant and will change when
            %   channels get enabled or disabled.
            %
            % libiio function: iio_device_get_sample_size

            if coder.target('MATLAB')
                value = adi.libiio.helpers.calllibADI('iio_device_get_sample_size', devPtr, maskPtr);
            else
                value = coder.ceval('iio_device_get_sample_size', devPtr, maskPtr);
            end
        end

        function value = iio_channel_get_index(chnPtr)
            % Get the index of the given channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure.
            % 
            % Returns:
            %   On success, the index of the specified channel.
            %   On error, a negative errno code is returned.
            %
            % libiio function: iio_channel_get_index

            if coder.target('MATLAB')
                value = adi.libiio.helpers.calllibADI('iio_channel_get_index', chnPtr);
            else
                value = coder.ceval('iio_channel_get_index', chnPtr);
            end
        end

        function formatPtr = iio_channel_get_data_format(chnPtr)
            % Get a pointer to a channel's data format structure
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure.
            % 
            % Returns:
            %   A pointer to the channel's iio_data_format structure.
            %
            % libiio function: iio_channel_get_data_format

            if coder.target('MATLAB')
                formatPtr = adi.libiio.helpers.calllibADI('iio_channel_get_data_format', chnPtr);
            else
                formatPtr = coder.opaque('const struct iio_data_format*', 'NULL');
                formatPtr = coder.ceval('iio_channel_get_data_format', chnPtr);
            end
        end

        function iio_channel_convert(chnPtr, dstPtr, srcPtr)
            % Convert the sample from hardware format to host format
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure.
            %   dstPtr: A pointer to the destination buffer where the 
            %       converted sample should be written.
            %   srcPtr: A pointer to the source buffer containing the
            %       sample.
            %
            % libiio function: iio_channel_convert

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channel_convert', chnPtr, dstPtr, srcPtr);
            else
                coder.ceval('iio_channel_convert', chnPtr, dstPtr, srcPtr);
            end            
        end

        function iio_channel_convert_inverse(chnPtr, dstPtr, srcPtr)
            % Convert the sample from host format to hardware format
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure.
            %   dstPtr: A pointer to the destination buffer where the 
            %       converted sample should be written.
            %   srcPtr: A pointer to the source buffer containing the
            %       sample.
            %
            % libiio function: iio_channel_convert_inverse

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_channel_convert_inverse', chnPtr, dstPtr, srcPtr);
            else
                coder.ceval('iio_channel_convert_inverse', chnPtr, dstPtr, srcPtr);
            end
        end

        function count = iio_device_get_debug_attrs_count(devPtr)
            % Enumerate the debug attributes of the given device
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            % 
            % Returns:
            %   The number of debug attributes found.
            %
            % libiio function: iio_device_get_debug_attrs_count

            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_device_get_debug_attrs_count', devPtr);
            else
                count = coder.ceval('iio_device_get_debug_attrs_count', devPtr);
            end
        end

        function attrPtr = iio_device_get_debug_attr(devPtr, index)
            % Get the debug attribute present at the given index
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            %   index: The index corresponding to the debug attribute.
            % 
            % Returns:
            %   On success, a pointer to a static NULL-terminated string.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_device_get_debug_attr
            
            validateattributes(index, { 'double','single' }, ...
                {'real', 'scalar', 'finite', 'nonnan', 'nonempty', ...
                'nonnegative', 'integer'});

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_device_get_debug_attr', devPtr, index);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_device_get_debug_attr', devPtr, index);
            end
        end

        function attrPtr = iio_device_find_debug_attr(devPtr, name)
            % Try to find a debug attribute by its name
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            %   name: A NULL-terminated string corresponding to the name 
            %       of the debug attribute.
            % 
            % Returns:
            %   On success, a pointer to a static NULL-terminated string.
            %   If the name does not correspond to any known debug 
            %       attribute of the given device, NULL is returned.
            %
            % libiio function: iio_device_find_debug_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_device_find_debug_attr', devPtr, name);
            else
                attrPtr = coder.opaque('const struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_device_find_debug_attr', devPtr, adi.libiio.helpers.ntstr(name));
            end
        end

        function status = iio_device_reg_write(devPtr, address, value)
            % Set the value of a hardware register
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            %   address: The address of the register.
            %   value: The value to set the register to.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative errno code is returned.
            %
            % libiio function: iio_device_find_debug_attr
            
            validateattributes(value, { 'double','single' }, ...
                {'real', 'scalar', 'finite', 'nonnan', 'nonempty', ...
                'nonnegative', 'integer'});

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_device_reg_write', devPtr, address, value);
            else
                status = coder.ceval('iio_device_reg_write', devPtr, address, value);
            end
        end

        function value = iio_device_reg_read(devPtr, address)
            % Get the value of a hardware register
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            %   address: The address of the register.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative errno code is returned.
            %
            % libiio function: iio_device_find_debug_attr
            
            valPtr = libpointer('uint32Ptr', 0);
            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_device_reg_read', devPtr, address, valPtr);
            else
                status = coder.ceval('iio_device_reg_read', devPtr, address, valPtr);
            end

            if ~status
                value = valPtr.value;
            end
        end
    end
end