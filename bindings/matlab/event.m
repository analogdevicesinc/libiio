classdef event < handle
    %% event methods
    methods (Static)
        function eventType = iio_event_get_type(eventPtr)
            % Get the type of a given IIO event
            %
            % Args:
            %   eventPtr: A pointer to an iio_event structure.
            % 
            % Returns:
            %   An enum iio_event_type.
            %
            % NOTE:
            %   Corresponds to the IIO_EVENT_CODE_EXTRACT_TYPE macro of
            %   <linux/iio/events.h>.
            %
            % libiio function: iio_event_get_type

            if coder.target('MATLAB')
                eventType = adi.libiio.helpers.calllibADI('iio_event_get_type', eventPtr);
            else
                eventType = coder.ceval('iio_event_get_type', eventPtr);
            end
        end

        function eventDir = iio_event_get_direction(eventPtr)
            % Get the direction of a given IIO event
            %
            % Args:
            %   eventPtr: A pointer to an iio_event structure.
            % 
            % Returns:
            %   An enum iio_event_direction.
            %
            % NOTE:
            %   Corresponds to the IIO_EVENT_CODE_EXTRACT_DIR macro of
            %   <linux/iio/events.h>.
            %
            % libiio function: iio_event_get_direction

            if coder.target('MATLAB')
                eventDir = adi.libiio.helpers.calllibADI('iio_event_get_direction', eventPtr);
            else
                eventDir = coder.ceval('iio_event_get_direction', eventPtr);
            end
        end

        function chanPtr = iio_event_get_channel(eventPtr, devPtr, diff)
            % Get a pointer to the IIO channel that corresponds to this event
            %
            % Args:
            %   eventPtr: A pointer to an iio_event structure.
            %   devPtr: A pointer to an iio_device structure.
            %   diff: If set, retrieve the differential channel.
            % 
            % Returns:
            %   On success, a pointer to an iio_channel structure.
            %   On failure, NULL is returned.
            %
            % libiio function: iio_event_get_channel

            validateattributes(diff, { 'logical' }, {'scalar', 'nonempty'});

            if coder.target('MATLAB')
                chanPtr = adi.libiio.helpers.calllibADI('iio_event_get_channel', eventPtr, devPtr, diff);
            else
                chanPtr = coder.opaque('const struct iio_channel*', 'NULL');
                chanPtr = coder.ceval('iio_event_get_channel', eventPtr, devPtr, diff);
            end
        end

        function eventStreamPtr = iio_device_create_event_stream(devPtr)
            % Create an events stream for the given IIO device
            %
            % Args:
            %   devPtr: A pointer to an iio_device structure.
            % 
            % Returns:
            %   On success, a pointer to an iio_event_stream structure.
            %   On failure, a pointer-encoded error is returned.
            %
            % libiio function: iio_device_create_event_stream

            if coder.target('MATLAB')
                eventStreamPtr = adi.libiio.helpers.calllibADI('iio_device_create_event_stream', devPtr);
            else
                eventStreamPtr = coder.opaque('struct iio_event_stream*', 'NULL');
                eventStreamPtr = coder.ceval('iio_device_create_event_stream', devPtr);
            end
        end

        function iio_event_stream_destroy(eventStreamPtr)
            % Destroy the given event stream.
            %
            % Args:
            %   eventStreamPtr: A pointer to an iio_event_stream structure.
            %
            % libiio function: iio_event_stream_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_event_stream_destroy', eventStreamPtr);
            else
                coder.ceval('iio_event_stream_destroy', eventStreamPtr);
            end
        end

        function status = iio_event_stream_read(eventStreamPtr, outEventPtr, nonBlock)
            % Read an event from the event stream
            %
            % Args:
            %   eventStreamPtr: A pointer to an iio_event_stream structure.
            %   outEventPtr: An pointer to an iio_event structure, that 
            %       will be filled by this function.
            %   nonBlock: if True, the operation won't block and return 
            %       -EBUSY if there is currently no event in the queue.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative errno code is returned.
            %
            % NOTE:
            %   It is possible to stop a blocking call of 
            %   iio_event_stream_read by calling iio_event_stream_destroy 
            %   in a different thread or signal handler. In that case, 
            %   iio_event_stream_read will return -EINTR.
            %
            % libiio function: iio_event_stream_read

            validateattributes(nonBlock, { 'logical' }, {'scalar', 'nonempty'});

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_event_stream_read', eventStreamPtr, outEventPtr, nonBlock);
            else
                status = coder.ceval('iio_event_stream_read', eventStreamPtr, outEventPtr, nonBlock);
            end
        end
    end
end