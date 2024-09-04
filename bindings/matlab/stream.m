classdef stream < handle
    %% stream methods
    methods (Static)
        function streamPtr = iio_buffer_create_stream(buffPtr, nbBlocks, samplesCount)
            % Create a iio_stream object for the given iio_buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure.
            %   nbBlocks: The number of iio_block objects to create, 
            %       internally. In doubt, a good value is 4.
            %   samplesCount: The size of the iio_block objects, in
            %   samples.
            % 
            % Returns:
            %   On success, a pointer to an iio_stream structure
            %   On failure, a pointer-encoded error is returned
            %
            % libiio function: iio_buffer_create_stream

            if coder.target('MATLAB')
                streamPtr = adi.libiio.helpers.calllibADI('iio_buffer_create_stream', buffPtr, nbBlocks, samplesCount);
            else
                streamPtr = coder.opaque('struct iio_stream*', 'NULL');
                streamPtr = coder.ceval('iio_buffer_create_stream', buffPtr, nbBlocks, samplesCount);
            end
        end

        function iio_stream_destroy(streamPtr)
            % Destroy the given stream object
            %
            % Args:
            %   streamPtr: A pointer to an iio_stream structure
            %
            % libiio function: iio_stream_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_stream_destroy', streamPtr);
            else
                coder.ceval('iio_stream_destroy', streamPtr);
            end
        end

        function blockPtr = iio_stream_get_next_block(streamPtr)
            % Get a pointer to the next data block
            %
            % Args:
            %   streamPtr: A pointer to an iio_stream structure
            % 
            % Returns:
            %   On success, a pointer to an iio_block structure
            %   On failure, a pointer-encoded error is returned
            %
            % libiio function: iio_stream_get_next_block

            if coder.target('MATLAB')
                blockPtr = adi.libiio.helpers.calllibADI('iio_stream_get_next_block', streamPtr);
            else
                blockPtr = coder.opaque('const struct iio_block*', 'NULL');
                blockPtr = coder.ceval('iio_stream_get_next_block', streamPtr);
            end
        end
    end
end