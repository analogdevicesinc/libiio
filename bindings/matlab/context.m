classdef context < handle
    methods (Static)
        function out = test(a)
            % Test the context class
            %
            % :param a: An input value
            % :return: An output value
            out = a + 1;
        end

        %% Context Methods
        % function ctxPtr = iio_create_context(useCalllib, ctxParamsPtr, uri)
        function ctxPtr = iio_create_context(ctxParamsPtr, uri)
            % Create a context from a URI description
            %
            % Args:
            %   ctxParamsPtr: A pointer to a iio_context_params structure 
            %       that contains context creation information; can be NULL
            %   uri: a URI describing the context location. If NULL, the 
            %       backend will be created using the URI string present in 
            %       the IIOD_REMOTE environment variable, or if not set, a 
            %       local backend is created.
            % 
            % Returns:
            %   On success, a pointer to a iio_context structure
            %   On failure, a pointer-encoded error is returned
            %
            % libiio function: iio_create_context
            
            if coder.target('MATLAB')
                ctxPtr = calllib(adi.libiio.context.getIIOLibName(), 'iio_create_context', ctxParamsPtr, uri);
            else
                ctxPtr = coder.opaque('struct iio_context*', 'NULL');                
                ctxPtr = coder.ceval('iio_create_context', ctxParamsPtr, adi.libiio.context.ntstr(uri));
            end
        end

        function devPtr = iio_context_find_device(ctxPtr, name)
            % Try to find a device structure by its ID, label or name
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure
            %   name: A NULL-terminated string corresponding to the ID, 
            %   label or nameof the device to search for
            % 
            % Returns:
            %   On success, a pointer to a iio_device structure
            %   If the parameter does not correspond to the ID, label or 
            %   name of any known device, NULL is returned
            %
            % libiio function: iio_context_find_device

            if coder.target('MATLAB')
                devPtr = calllib(adi.libiio.context.getIIOLibName(), 'iio_context_find_device', ctxPtr, name);
            else
                devPtr = coder.opaque('struct iio_device*', 'NULL');                
                devPtr = coder.ceval('iio_context_find_device', ctxPtr, adi.libiio.context.ntstr(name));
            end
        end
    end

    %%Helpers
    methods (Hidden, Access = private, Static)
        function libName = getIIOLibName()
            libName = 'libiio1';
        end

        function strout = ntstr(strin)
            % Appends a null character to terminate the string.
            % This is crutial to code generation as MATALB character arrays are not
            % automatically null terminated in code generation.
            strout = [uint8(strin) uint8(0)];
        end
    end
end