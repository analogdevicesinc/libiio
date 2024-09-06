classdef scan < handle
    methods (Static)
        %% scan methods
        function scanPtr = iio_scan(ctxParamsPtr, backends)
            % Scan backends for IIO contexts
            %
            % Args:
            %   ctxParamsPtr: A pointer to a iio_context_params structure 
            %       that contains context creation information; can be
            %       NULL.
            %   backends: A NULL-terminated string containing a comma-separated list
            %       of the backends to be scanned for contexts. If NULL, all the available
            %       backends are scanned.
            % 
            % Returns:
            %   On success, a pointer to an iio_scan structure.
            %   On failure, a pointer-encoded error is returned.
            %
            % NOTE: It is possible to provide backend-specific information.
            %   For instance, "local,usb=0456:*" will scan the local backend and
            %   limit scans on USB to vendor ID 0x0456, and accept all product IDs.
            %   The "usb=0456:b673" string would limit the scan to the device with
            %   this particular VID/PID. Both IDs are expected in hexadecimal, no 0x
            %   prefix needed.
            %
            % libiio function: iio_scan
            
            if coder.target('MATLAB')
                scanPtr = adi.libiio.helpers.calllibADI('iio_scan', ctxParamsPtr, backends);
            else
                scanPtr = coder.opaque('struct iio_context*', 'NULL');
                scanPtr = coder.ceval('iio_scan', ctxParamsPtr, adi.libiio.helpers.ntstr(backends));
            end
        end

        function iio_scan_destroy(scanPtr)
            % Destroy the given scan context
            %
            % Args:
            %   scanPtr: A pointer to an iio_scan structure.
            %
            % NOTE: 
            %   After that function, the iio_scan pointer shall be invalid.
            %
            % libiio function: iio_scan_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_scan_destroy', scanPtr);
            else
                coder.ceval('iio_scan_destroy', scanPtr);
            end
        end

        function count = iio_scan_get_results_count(scanPtr)
            % Get number of results of a scan operation
            %
            % Args:
            %   scanPtr: A pointer to an iio_scan structure.
            % 
            % Returns:
            %   The number of results of the scan operation.
            %
            % libiio function: iio_scan_get_results_count
            
            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_scan_get_results_count', scanPtr);
            else
                count = coder.ceval('iio_scan_get_results_count', scanPtr);
            end
        end

        function desc = iio_scan_get_description(scanPtr, idx)
            % Get description of scanned context
            %
            % Args:
            %   scanPtr: A pointer to an iio_scan structure.
            %   idx: The index of the scanned context.
            % 
            % Returns:
            %   On success, a pointer to a NULL-terminated string.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_scan_get_description

            if coder.target('MATLAB')
                desc = adi.libiio.helpers.calllibADI(obj.libName, 'iio_scan_get_description', scanPtr, idx);
            else
                desc = coder.nullcopy(adi.libiio.helpers.ntstr(''));
                desc = coder.ceval('iio_scan_get_description', scanPtr, idx);
            end
        end

        function uri = iio_scan_get_uri(scanPtr, idx)
            % Get URI of scanned context
            %
            % Args:
            %   scanPtr: A pointer to an iio_scan structure.
            %   idx: The index of the scanned context.
            % 
            % Returns:
            %   On success, a pointer to a NULL-terminated string.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_scan_get_uri

            if coder.target('MATLAB')
                uri = adi.libiio.helpers.calllibADI(obj.libName, 'iio_scan_get_uri', scanPtr, idx);
            else
                uri = coder.nullcopy(adi.libiio.helpers.ntstr(''));
                uri = coder.ceval('iio_scan_get_uri', scanPtr, idx);
            end
        end
    end
end