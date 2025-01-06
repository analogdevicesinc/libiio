classdef hwmon < handle
    %% hwmon methods
    methods (Static)
        function hwmonChnType = hwmon_channel_get_type(chnPtr)
            % Get the type of the given hwmon channel
            %
            % Args:
            %   chnPtr: A pointer to an iio_channel structure.
            % 
            % Returns:
            %   The type of the hwmon channel.
            %
            % libiio function: hwmon_channel_get_type

            if coder.target('MATLAB')
                hwmonChnType = adi.libiio.helpers.calllibADI('hwmon_channel_get_type', chnPtr);
            else
                hwmonChnType = coder.ceval('hwmon_channel_get_type', chnPtr);
            end
        end

        function status = iio_device_is_hwmon(devPtr)
            % Get whether or not the device is a hardware monitoring device
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            % 
            % Returns:
            %   True if the device is a hardware monitoring device, 
            %   false if it is a IIO device.
            %
            % libiio function: iio_device_is_hwmon

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_device_is_hwmon', devPtr);
            else
                status = coder.ceval('iio_device_is_hwmon', devPtr);
            end
        end
    end
end