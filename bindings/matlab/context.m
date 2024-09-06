classdef context < handle
    methods (Static)
        %% context methods
        function ctxPtr = iio_create_context(ctxParamsPtr, uri)
            % Create a context from a URI description
            %
            % Args:
            %   ctxParamsPtr: A pointer to a iio_context_params structure 
            %       that contains context creation information; can be
            %       NULL.
            %   uri: a URI describing the context location. If NULL, the 
            %       backend will be created using the URI string present in 
            %       the IIOD_REMOTE environment variable, or if not set, a 
            %       local backend is created.
            % 
            % Returns:
            %   On success, a pointer to a iio_context structure.
            %   On failure, a pointer-encoded error is returned.
            %
            % NOTE: The following URIs are supported based on compile time backend
            % support:
            % - Local backend, "local:"
            %   Does not have an address part. For example "local:"
            % - XML backend, "xml:"
            %   Requires a path to the XML file for the address part.
            %   For example "xml:/home/user/file.xml"
            % - Network backend, "ip:"
            %   Requires a hostname, IPv4, or IPv6 to connect to
            %   a specific running IIO Daemon or no address part for automatic discovery
            %   when library is compiled with ZeroConf support. For example
            %   "ip:192.168.2.1", or "ip:localhost", or "ip:"
            %   or "ip:plutosdr.local". To support alternative port numbers the
            %   standard ip:host:port format is used. A special format is required as
            %   defined in RFC2732 for IPv6 literal hostnames, (adding '[]' around the host)
            %   to use a ip:[x:x:x:x:x:x:x:x]:port format.
            %   Valid examples would be:
            %     - ip:                                               Any host on default port
            %     - ip::40000                                         Any host on port 40000
            %     - ip:analog.local                                   Default port
            %     - ip:brain.local:40000                              Port 40000
            %     - ip:192.168.1.119                                  Default Port
            %     - ip:192.168.1.119:40000                            Port 40000
            %     - ip:2601:190:400:da:47b3:55ab:3914:bff1            Default Port
            %     - ip:[2601:190:400:da:9a90:96ff:feb5:acaa]:40000    Port 40000
            %     - ip:fe80::f14d:3728:501e:1f94%eth0                 Link-local through eth0, default port
            %     - ip:[fe80::f14d:3728:501e:1f94%eth0]:40000         Link-local through eth0, port 40000
            % - USB backend, "usb:"
            %   When more than one usb device is attached, requires
            %   bus, address, and interface parts separated with a dot. For example
            %   "usb:3.32.5". Where there is only one USB device attached, the shorthand
            %   "usb:" can be used.
            % - Serial backend, "serial:"
            %   Requires:
            %     - a port (/dev/ttyUSB0),
            %     - baud_rate (default 115200)
            %     - serial port configuration
            %        - data bits (5 6 7 8 9)
            %        - parity ('n' none, 'o' odd, 'e' even, 'm' mark, 's' space)
            %        - stop bits (1 2)
            %        - flow control ('\0' none, 'x' Xon Xoff, 'r' RTSCTS, 'd' DTRDSR)
            % 
            % For example "serial:/dev/ttyUSB0,115200" or "serial:/dev/ttyUSB0,115200,8n1"
            %
            % libiio function: iio_create_context
            
            if coder.target('MATLAB')
                ctxPtr = adi.libiio.helpers.calllibADI('iio_create_context', ctxParamsPtr, uri);
            else
                ctxPtr = coder.opaque('struct iio_context*', 'NULL');
                ctxPtr = coder.ceval('iio_create_context', ctxParamsPtr, adi.libiio.helpers.ntstr(uri));
            end
        end

        function iio_context_destroy(ctxPtr)
            % Destroy the given context
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            %
            % libiio function: iio_context_destroy

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_context_destroy', ctxPtr);
            else
                coder.ceval('iio_context_destroy', ctxPtr);
            end
        end

        function major = iio_context_get_version_major(ctxPtr)
            % Get the major number of the library version
            %
            % Args:
            %   ctxPtr: Optional pointer to a iio_context structure.
            %
            % Returns:
            %   The major number.
            %
            % NOTE: 
            %   If ctx is non-null, it will return the major version of 
            %   the remote library, if running remotely.
            %
            % libiio function: iio_context_get_version_major

            if coder.target('MATLAB')
                major = adi.libiio.helpers.calllibADI('iio_context_get_version_major', ctxPtr);
            else
                major = coder.ceval('iio_context_get_version_major', ctxPtr);
            end
        end

        function minor = iio_context_get_version_minor(ctxPtr)
            % Get the minor number of the library version
            %
            % Args:
            %   ctxPtr: Optional pointer to a iio_context structure.
            %
            % Returns:
            %   The minor number.
            %
            % NOTE: 
            %   If ctx is non-null, it will return the major version of 
            %   the remote library, if running remotely.
            %
            % libiio function: iio_context_get_version_minor

            if coder.target('MATLAB')
                minor = adi.libiio.helpers.calllibADI('iio_context_get_version_minor', ctxPtr);
            else
                minor = coder.ceval('iio_context_get_version_minor', ctxPtr);
            end
        end

        function vtag = iio_context_get_version_tag(ctxPtr)
            % Get the git hash string of the library version
            %
            % Args:
            %   ctxPtr: Optional pointer to a iio_context structure.
            %
            % Returns:
            %   A NULL-terminated string that contains the git tag or hash.
            %
            % NOTE: 
            %   If ctx is non-null, it will return the major version of 
            %   the remote library, if running remotely.
            %
            % libiio function: iio_context_get_version_tag

            if coder.target('MATLAB')
                vtag = adi.libiio.helpers.calllibADI('iio_context_get_version_tag', ctxPtr);
            else
                vtag = coder.nullcopy(adi.libiio.scan.ntstr(''));
                vtag = coder.ceval('iio_context_get_version_tag', ctxPtr);
            end
        end

        function xml = iio_context_get_xml(ctxPtr)
            % Obtain a XML representation of the given context
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            %
            % Returns:
            %   On success, an allocated string. Must be deallocated with free().
            %   On failure, a pointer-encoded error is returned.
            %
            % libiio function: iio_context_get_xml

            if coder.target('MATLAB')
                xml = adi.libiio.helpers.calllibADI('iio_context_get_xml', ctxPtr);
            else
                xml = coder.nullcopy(adi.libiio.scan.ntstr(''));
                xml = coder.ceval('iio_context_get_xml', ctxPtr);
            end
        end

        function name = iio_context_get_name(ctxPtr)
            % Get the name of the given context
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            %
            % Returns:
            %   A pointer to a static NULL-terminated string.
            %
            % NOTE: 
            %   The returned string will be local, xml or network 
            %   when the context has been created with the local, 
            %   xml and network backends respectively.
            %
            % libiio function: iio_context_get_name

            if coder.target('MATLAB')
                name = adi.libiio.helpers.calllibADI('iio_context_get_name', ctxPtr);
            else
                name = coder.nullcopy(adi.libiio.scan.ntstr(''));
                name = coder.ceval('iio_context_get_name', ctxPtr);
            end
        end

        function descr = iio_context_get_description(ctxPtr)
            % Get a description of the given context
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            %
            % Returns:
            %   A pointer to a static NULL-terminated string.
            %
            % NOTE: 
            %   The returned string will contain human-readable information 
            %   about the current context.
            %
            % libiio function: iio_context_get_description

            if coder.target('MATLAB')
                descr = adi.libiio.helpers.calllibADI('iio_context_get_description', ctxPtr);
            else
                descr = coder.nullcopy(adi.libiio.scan.ntstr(''));
                descr = coder.ceval('iio_context_get_description', ctxPtr);
            end
        end

        function count = iio_device_get_channels_count(ctxPtr)
            % Get the number of context-specific attributes
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            % 
            % Returns:
            %   The number of context-specific attributes.
            %
            % libiio function: iio_device_get_channels_count
            
            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_device_get_channels_count', ctxPtr);
            else
                count = coder.ceval('iio_device_get_channels_count', ctxPtr);
            end
        end

        function attrPtr = iio_context_get_attr(ctxPtr, idx)
            % Retrieve the context-specific attribute at the given index
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   idx: The index corresponding to the attribute.
            % 
            % Returns:
            %   On success, a pointer to an iio_attr structure.
            %   If the index is out-of-range, NULL is returned.
            %
            % libiio function: iio_context_get_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_context_get_attr', ctxPtr, idx);
            else
                attrPtr = coder.opaque('struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_context_get_attr', ctxPtr, idx);
            end
        end

        function attrPtr = iio_context_find_attr(ctxPtr, name)
            % Try to find a device structure by its ID, label or name
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   name: A NULL-terminated string corresponding to the name 
            %       of the attribute.
            % 
            % Returns:
            %   On success, a pointer to a iio_attr structure.
            %   If the name does not correspond to any known attribute of 
            %   the given context, NULL is returned.
            %
            % libiio function: iio_context_find_attr

            if coder.target('MATLAB')
                attrPtr = adi.libiio.helpers.calllibADI('iio_context_find_attr', ctxPtr, name);
            else
                attrPtr = coder.opaque('struct iio_attr*', 'NULL');
                attrPtr = coder.ceval('iio_context_find_attr', ctxPtr, adi.libiio.helpers.ntstr(name));
            end
        end

        function count = iio_context_get_devices_count(ctxPtr)
            % Enumerate the devices found in the given context
            %
            % Args:
            %   ctxPtr: A pointer to a iio_context structure.
            % 
            % Returns:
            %   The number of devices found.
            %
            % libiio function: iio_context_get_devices_count
            
            if coder.target('MATLAB')
                count = adi.libiio.helpers.calllibADI('iio_context_get_devices_count', ctxPtr);
            else
                count = coder.ceval('iio_context_get_devices_count', ctxPtr);
            end
        end

        function devPtr = iio_context_get_device(ctxPtr, idx)
            % Get the device present at the given index
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   idx: The index corresponding to the device.
            % 
            % Returns:
            %   On success, a pointer to a iio_device structure.
            %   If the index is invalid, NULL is returned.
            %
            % libiio function: iio_context_get_device

            if coder.target('MATLAB')
                devPtr = adi.libiio.helpers.calllibADI('iio_context_get_device', ctxPtr, idx);
            else
                devPtr = coder.opaque('struct iio_device*', 'NULL');
                devPtr = coder.ceval('iio_context_get_device', ctxPtr, idx);
            end
        end

        function devPtr = iio_context_find_device(ctxPtr, name)
            % Try to find a device structure by its ID, label or name
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   name: A NULL-terminated string corresponding to the ID, 
            %       label or nameof the device to search for.
            % 
            % Returns:
            %   On success, a pointer to a iio_device structure.
            %   If the parameter does not correspond to the ID, label or 
            %   name of any known device, NULL is returned.
            %
            % libiio function: iio_context_find_device

            if coder.target('MATLAB')
                devPtr = adi.libiio.helpers.calllibADI('iio_context_find_device', ctxPtr, name);
            else
                devPtr = coder.opaque('struct iio_device*', 'NULL');
                devPtr = coder.ceval('iio_context_find_device', ctxPtr, adi.libiio.helpers.ntstr(name));
            end
        end

        function status = iio_context_set_timeout(ctxPtr, timeout_ms)
            % Set a timeout for I/O operations
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   timeout_ms: A positive integer representing the time in 
            %       milliseconds after which a timeout occurs. A value of 0 
            %       is used to specify that no timeout should occur.
            % 
            % Returns:
            %   On success, 0 is returned.
            %   On error, a negative errno code is returned.
            %
            % libiio function: iio_context_set_timeout

            if coder.target('MATLAB')
                status = adi.libiio.helpers.calllibADI('iio_context_set_timeout', ctxPtr, timeout_ms);
            else
                status = coder.opaque('struct iio_device*', 'NULL');
                status = coder.ceval('iio_context_set_timeout', ctxPtr, timeout_ms);
            end
        end

        function ctxParamsPtr = iio_context_get_params(ctxPtr)
            % Get a pointer to the params structure
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            % 
            % Returns:
            %   A pointer to the context's iio_context_params structure.
            %
            % libiio function: iio_context_get_params

            if coder.target('MATLAB')
                ctxParamsPtr = adi.libiio.helpers.calllibADI('iio_context_get_params', ctxPtr);
            else
                ctxParamsPtr = coder.opaque('struct iio_context_params*', 'NULL');
                ctxParamsPtr = coder.ceval('iio_context_get_params', ctxPtr);
            end
        end

        function iio_context_set_data(ctxPtr, dataPtr)
            % Associate a pointer to an iio_context structure
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            %   dataPtr: The pointer to be associated.
            % 
            % Returns:
            %   A pointer to the context's iio_context_params structure.
            %
            % libiio function: iio_context_set_data

            if coder.target('MATLAB')
                adi.libiio.helpers.calllibADI('iio_context_set_data', ctxPtr, dataPtr);
            else
                coder.ceval('iio_context_set_data', ctxPtr, dataPtr);
            end
        end

        function dataPtr = iio_context_get_data(ctxPtr)
            % Retrieve a previously associated pointer of an iio_context structure
            %
            % Args:
            %   ctxPtr: A pointer to an iio_context structure.
            % 
            % Returns:
            %   The pointer previously associated if present, or NULL.
            %
            % libiio function: iio_context_get_data

            if coder.target('MATLAB')
                dataPtr = adi.libiio.helpers.calllibADI('iio_context_set_data', ctxPtr);
            else
                dataPtr = coder.opaque('void*', 'NULL');
                dataPtr = coder.ceval('iio_context_set_data', ctxPtr);
            end
        end
    end
end