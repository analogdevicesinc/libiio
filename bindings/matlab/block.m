classdef block < handle
    %% block methods
    methods (Static)
        function blockPtr = iio_buffer_create_block(buffPtr, size)
            % Create a data block for the given buffer
            %
            % Args:
            %   buffPtr: A pointer to an iio_buffer structure.
            %   size: The size of the block to create, in bytes.
            % 
            % Returns:
            %   On success, a pointer to an iio_block structure.
            %   On failure, a pointer-encoded error is returned.
            %
            % libiio function: iio_buffer_create_block

            if coder.target('MATLAB')
                blockPtr = adi.libiio.helpers.calllibADI('iio_buffer_create_block', buffPtr, size);
            else
                blockPtr = coder.opaque('const struct iio_block*', 'NULL');
                blockPtr = coder.ceval('iio_buffer_create_block', buffPtr, size);
            end
        end

        function iio_block_destroy(blockPtr)
            % Destroy the given block
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            %
            % libiio function: iio_block_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_block_destroy', blockPtr);
            else
                coder.ceval('iio_block_destroy', blockPtr);
            end
        end

        function startAddr = iio_block_start(blockPtr)
            % Get the start address of the block
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            % 
            % Returns:
            %   A pointer corresponding to the start address of the block.
            %
            % libiio function: iio_block_start

            if coder.target('MATLAB')
                startAddr = adi.libiio.helpers.calllibADI('iio_block_start', blockPtr);
            else
                startAddr = coder.opaque('void*', 'NULL');
                startAddr = coder.ceval('iio_block_start', blockPtr);
            end
        end

        function addr = iio_block_first(blockPtr, chnPtr)
            % Find the first sample of a channel in a block
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            %   chnPtr: A pointer to an iio_channel structure.
            % 
            % Returns:
            %   A pointer to the first sample found, or to the end of the 
            %   block if no sample for the given channel is present in the 
            %   block.
            %
            % NOTE:
            %   This function, coupled with iio_block_end, can be used to
            %   iterate on all the samples of a given channel present in 
            %   the block, doing the following:
            %
            %   for (void *ptr = iio_block_first(block, chn);
            %          ptr < iio_block_end(block);
            %       ptr += iio_device_get_sample_size(dev, mask)) {
            %       ....
            %   }
            % 
            % The iio_channel passed as argument must be from the 
            % iio_device that was used to create the iio_buffer and then 
            % the iio_block, otherwise the result is undefined.
            %
            % libiio function: iio_block_first

            if coder.target('MATLAB')
                addr = adi.libiio.helpers.calllibADI('iio_block_first', blockPtr, chnPtr);
            else
                addr = coder.opaque('void*', 'NULL');
                addr = coder.ceval('iio_block_first', blockPtr, chnPtr);
            end
        end

        function endAddr = iio_block_end(blockPtr)
            % Get the address after the last sample in a block
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            % 
            % Returns:
            %   A pointer corresponding to the address that follows the 
            %   last sample present in the buffer.
            %
            % libiio function: iio_block_end

            if coder.target('MATLAB')
                endAddr = adi.libiio.helpers.calllibADI('iio_block_end', blockPtr);
            else
                endAddr = coder.opaque('void*', 'NULL');
                endAddr = coder.ceval('iio_block_end', blockPtr);
            end
        end
        %{
        function endAddr = iio_block_foreach_sample(blockPtr, maskPtr)
            % Call the supplied callback for each sample found in a block
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            %   maskPtr: A pointer to the iio_channels_mask structure that 
            %       represents the list of channels for which we want samples.
            %   callback: A pointer to a function to call for each sample
            %       found.
            %   dataPtr: A user-specified pointer that will be passed to
            %       the callback.
            % 
            % Returns:
            %   Number of bytes processed
            %
            % NOTE: 
            %   The callback receives four arguments:
            %   a pointer to the iio_channel structure corresponding to the sample,
            %   a pointer to the sample itself,
            %   the length of the sample in bytes,
            %   the user-specified pointer passed to iio_block_foreach_sample.
            %
            % libiio function: iio_block_foreach_sample

            if coder.target('MATLAB')
                endAddr = adi.libiio.helpers.calllibADI('iio_block_foreach_sample', blockPtr, maskPtr);
            else
                endAddr = coder.ceval('iio_block_foreach_sample', blockPtr, maskPtr);
            end
        end

        % /** @brief 
        %  * @param block A pointer to an iio_block structure
        %  * @param mask A pointer to the iio_channels_mask structure that represents
        %  *   the list of channels for which we want samples
        %  * @param callback A pointer to a function to call for each sample found
        %  * @param data A user-specified pointer that will be passed to the callback
        %  * @return number of bytes processed.
        %  *
        %  * <b>NOTE:</b> The callback receives four arguments:
        %  * * A pointer to the iio_channel structure corresponding to the sample,
        %  * * A pointer to the sample itself,
        %  * * The length of the sample in bytes,
        %  * * The user-specified pointer passed to iio_block_foreach_sample. */
        % __api __check_ret ssize_t
        % iio_block_foreach_sample(const struct iio_block *block,
		% 	         const struct iio_channels_mask *mask,
		% 	         ssize_t (*callback)(const struct iio_channel *chn,
		% 			             void *src, size_t bytes, void *d),
		% 	         void *data);
        %}

        function status = iio_block_enqueue(blockPtr, bytesUsed, cyclic)
            % Enqueue the given iio_block to the buffer's queue
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure
            %   bytesUsed: The amount of data in bytes to be transferred 
            %       (either transmitted or received). If zero, the size of 
            %       the block is used.
            %   cyclic: If True, enable cyclic mode. The block's content 
            %       will be repeated on the hardware's output until the 
            %       buffer is cancelled or destroyed.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative error code is returned.
            %
            % NOTE: After iio_block_enqueue is called, the block's data 
            %   must not be accessed until iio_block_dequeue successfully 
            %   returns.
            %
            % libiio function: iio_block_enqueue

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_block_enqueue', blockPtr, bytesUsed, cyclic);
            else
                status = coder.ceval('iio_block_enqueue', blockPtr, bytesUsed, cyclic);
            end
        end

        function status = iio_block_dequeue(blockPtr, nonBlock)
            % Dequeue the given iio_block from the buffer's queue
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            %   nonBlock: if True, the operation won't block and return 
            %       -EBUSY if the block is not ready for dequeue.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative error code is returned.
            %
            % libiio function: iio_block_dequeue

            validateattributes(nonBlock, { 'logical' }, {'scalar', 'nonempty'});

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_block_dequeue', blockPtr, nonBlock);
            else
                status = coder.ceval('iio_block_dequeue', blockPtr, nonBlock);
            end
        end

        function buffPtr = iio_block_get_buffer(blockPtr)
            % Retrieve a pointer to the iio_buffer structure
            %
            % Args:
            %   blockPtr: A pointer to an iio_block structure.
            % 
            % Returns:
            %   A pointer to an iio_buffer structure.
            %
            % libiio function: iio_block_get_buffer

            if coder.target('MATLAB')
                buffPtr = adi.libiio.helpers.calllibADI('iio_block_get_buffer', blockPtr);
            else
                buffPtr = coder.opaque('struct iio_buffer*', 'NULL');
                buffPtr = coder.ceval('iio_block_get_buffer', blockPtr);
            end
        end
    end
end