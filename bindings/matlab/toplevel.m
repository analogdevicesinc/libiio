classdef toplevel < handle
    methods (Static)
        %% top-level methods
        function iio_strerror(err, dstPtr, len)
            % Get a string description of an error code
            %
            % Args:
            %   err: The error code. Can be positive or negative.
            %   dst: A pointer to the memory area where the NULL-terminated string
            %       corresponding to the error message will be stored.
            %   len: The available length of the memory area, in bytes.
            %
            % libiio function: iio_strerror

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_strerror', err, dstPtr, len);
            else
                coder.ceval('iio_strerror', err, dstPtr, len);
            end
        end

        function hasBackend = iio_has_backend(varargin)
            % Check if the specified backend is available
            %
            % Args:
            %   (optional) ctxParamsPtr: A pointer to a iio_context_params 
            %       structure that contains context creation information; 
            %       can be NULL.
            %   backend: The name of the backend to query.
            % 
            % Returns:
            %   True if the backend is available, false otherwise.
            %
            % libiio function: iio_has_backend
            
            if nargin == 1
                if coder.target('MATLAB')
                    ctxParamsPtr = libpointer;
                else
                    ctxParamsPtr = coder.opaque('const struct iio_context_params*', 'NULL');
                end
                backend = varargin{1};
            elseif nargin == 2
                ctxParamsPtr = varargin{1};
                backend = varargin{2};
            end

            if coder.target('MATLAB')
                hasBackend = adi.libiio.helpers.calllibADI('iio_has_backend', ctxParamsPtr, backend);
            else
                hasBackend = coder.ceval('iio_has_backend', ctxParamsPtr, adi.libiio.helpers.ntstr(backend));
            end
        end

        function count = iio_get_builtin_backends_count()
            % Get the number of available built-in backends
            % 
            % Returns:
            %   True if the backend is available, false otherwise.
            %
            % libiio function: iio_has_backend
            
            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_get_builtin_backends_count');
            else
                count = coder.ceval('iio_get_builtin_backends_count');
            end
        end

        function name = iio_get_builtin_backend(idx)
            % Retrieve the name of a given built-in backend
            %
            % Args:
            %   idx: The index corresponding to the backend.
            % 
            % Returns:
            %   On success, a pointer to a NULL-terminated string.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_get_builtin_backend

            if coder.target('MATLAB')
                name = adi.libiio.helpers.calllibADI(obj.libName, 'iio_get_builtin_backend', idx);
            else
                name = coder.nullcopy(adi.libiio.helpers.ntstr(''));
                name = coder.ceval('iio_get_builtin_backend', idx);
            end
        end
    end
end