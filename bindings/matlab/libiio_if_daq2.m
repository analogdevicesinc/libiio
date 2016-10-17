% Copyright 2014(c) Analog Devices, Inc.
%
%  All rights reserved.
%
%  Redistribution and use in source and binary forms, with or without modification,
%  are permitted provided that the following conditions are met:
%      - Redistributions of source code must retain the above copyright
%        notice, this list of conditions and the following disclaimer.
%      - Redistributions in binary form must reproduce the above copyright
%        notice, this list of conditions and the following disclaimer in
%        the documentation and/or other materials provided with the
%        distribution.
%      - Neither the name of Analog Devices, Inc. nor the names of its
%        contributors may be used to endorse or promote products derived
%        from this software without specific prior written permission.
%      - The use of this software may or may not infringe the patent rights
%        of one or more patent holders.  This license does not release you
%        from the requirement that you obtain separate licenses from these
%        patent holders to use this software.
%      - Use of the software either in source or binary form or filter designs
%        resulting from the use of this software, must be connected to, run
%        on or loaded to an Analog Devices Inc. component.
%
%  THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
%  INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A
%  PARTICULAR PURPOSE ARE DISCLAIMED.
%
%  IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
%  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, INTELLECTUAL PROPERTY
%  RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
%  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
%  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
%  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

classdef libiio_if < handle
    % libiio_if Interface object for for IIO devices

    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %% Protected properties
    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    properties (Access = protected)
        libname         = 'libiio';
        hname           = 'iio.h';
        dev_name        = '';
        data_ch_no      = 0;
        data_ch_size    = 0;
        dev_type        = '';
        iio_ctx         = {};
        iio_dev         = {};
        iio_buffer      = {};
        iio_channel     = {};
        iio_buf_size    = 8192;
        iio_scan_elm_no = 0;
        if_initialized  = 0;
    end

    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %% Static private methods
    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    methods (Static, Access = private)
        function out = modInstanceCnt(val)
            % Manages the number of object instances to handle proper DLL unloading
            persistent instance_cnt;
            if isempty(instance_cnt)
                instance_cnt = 0;
            end
            instance_cnt = instance_cnt + val;
            out = instance_cnt;
        end
    end

    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %% Protected methods
    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    methods (Access = protected)
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Creates the network context
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = createNetworkContext(obj, ip_address)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Create the network context
            obj.iio_ctx = calllib(obj.libname, 'iio_create_network_context', ip_address);

            % Check if the network context is valid
            if (isNull(obj.iio_ctx))
                obj.iio_ctx = {};
                err_msg = 'Could not connect to the IIO server!';
                return;
            end

            % Increase the object's instance count
            libiio_if.modInstanceCnt(1);
            msg_log = [msg_log sprintf('%s: Connected to IP %s\n', class(obj), ip_address)];

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Releases the network context and unload the libiio library
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function releaseContext(obj)
            calllib(obj.libname, 'iio_context_destroy', obj.iio_ctx);
            obj.iio_ctx = {};
            instCnt = libiio_if.modInstanceCnt(-1);
            if(instCnt == 0)
                unloadlibrary(obj.libname);
            end
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Checks the compatibility of the different software modules.
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = checkVersions(obj)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Create a set of pointers to read the iiod and dll versions
            data = zeros(1, 10);
            remote_pMajor = libpointer('uint32Ptr', data(1));
            remote_pMinor = libpointer('uint32Ptr', data(2));
            remote_pGitTag = libpointer('int8Ptr', [int8(data(3:end)) 0]);
            local_pMajor = libpointer('uint32Ptr', data(1));
            local_pMinor = libpointer('uint32Ptr', data(2));
            local_pGitTag = libpointer('int8Ptr', [int8(data(3:end)) 0]);

            % get remote libiio version
            calllib(obj.libname, 'iio_context_get_version', obj.iio_ctx, remote_pMajor, remote_pMinor, remote_pGitTag);
            % extract git hash without trailing null char
            remote_githash = remote_pGitTag.Value(1:7);
            remote_version_str = sprintf('Remote libiio version: %d.%d, (git-%s)', remote_pMajor.Value, remote_pMinor.Value, remote_githash);
            msg_log = [msg_log sprintf('%s: %s\n', class(obj), remote_version_str)];

            % get local libiio version
            calllib(obj.libname, 'iio_library_get_version', local_pMajor, local_pMinor, local_pGitTag);
            local_githash = local_pGitTag.Value(1:7);
            local_version_str = sprintf('Local libiio version: %d.%d, (git-%s)', local_pMajor.Value, local_pMinor.Value, local_githash);
            msg_log = [msg_log sprintf('%s: %s\n', class(obj), local_version_str)];

            if(remote_pMajor.Value < local_pMajor.Value)
                err_msg = ['The libiio version running on the device is outdated! ' ...
                    'Run the adi_update_tools.sh script to get libiio up to date.'];
                return;
            elseif(remote_pMajor.Value > local_pMajor.Value)
                err_msg = ['The libiio version on the local host is outdated! ' ...
                    'On Windows, reinstall the dll using the latest installer ' ...
                    'from the Analog Devices wiki.'];
                return;
            end

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Detect if the specified device is present in the system
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = initDevice(obj, dev_name)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Store the device name
            obj.dev_name = dev_name;

            % Get the number of devices
            nb_devices = calllib(obj.libname, 'iio_context_get_devices_count', obj.iio_ctx);

            % If no devices are present return with error
            if(nb_devices == 0)
                err_msg = 'No devices were detected in the system!';
                return;
            end
            msg_log = [msg_log sprintf('%s: Found %d devices in the system\n', class(obj), nb_devices)];

            % Detect if the targeted device is installed
            dev_found = 0;
            for i = 0 : nb_devices - 1
                dev = calllib(obj.libname, 'iio_context_get_device', obj.iio_ctx, i);
                name = calllib(obj.libname, 'iio_device_get_name', dev);
                if(strcmp(name, dev_name))
                    obj.iio_dev = dev;
                    dev_found = 1;
                    break;
                end
                clear dev;
            end

            % Check if the target device was detected
            if(dev_found == 0)
                err_msg = 'Could not find target configuration device!';
                return;
            end

            msg_log = [msg_log sprintf('%s: %s was found in the system\n', class(obj), obj.dev_name)];

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Initializes the output data channels
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = initOutputDataChannels(obj, ch_no, ch_size)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Save the number of channels and size
            obj.data_ch_no = ch_no;
            obj.data_ch_size = ch_size;

            % Get the number of channels that the device has
            nb_channels = calllib(obj.libname, 'iio_device_get_channels_count', obj.iio_dev);
            if(nb_channels == 0)
                err_msg = 'The selected device does not have any channels!';
                return;
            end

            % Enable the data channels
            if(ch_no ~= 0)
                % Check if the device has output channels. The
                % logic here assumes that a device can have
                % only input or only output channels
                obj.iio_channel{1} = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, 0);
                is_output = calllib(obj.libname, 'iio_channel_is_output', obj.iio_channel{1});
                if(is_output == 0)
                    err_msg = 'The selected device does not have output channels!';
                    return;
                end
                % Enable all the channels
                for j = 0 : nb_channels - 1
                    obj.iio_channel{j+1} = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, j);
                    if(j < ch_no)
                        calllib(obj.libname, 'iio_channel_enable', obj.iio_channel{j+1});
                        is_scan_element = calllib(obj.libname, 'iio_channel_is_scan_element', obj.iio_channel{j+1});
                        if(is_scan_element == 1)
                            obj.iio_scan_elm_no = obj.iio_scan_elm_no + 1;
                        end
                    else
                        calllib(obj.libname, 'iio_channel_disable', obj.iio_channel{j+1});                        
                    end
                end
                msg_log = [msg_log sprintf('%s: Found %d output channels for the device %s\n', class(obj), obj.iio_scan_elm_no, obj.dev_name)];

                % Check if the number of channels in the device
                % is greater or equal to the system object
                % input channels
                if(obj.iio_scan_elm_no < ch_no)
                    obj.iio_channel = {};
                    err_msg = 'The selected device does not have enough output channels!';
                    return;
                end

                % Enable the DAC buffer output
                obj.if_initialized = 1;
                ret = writeAttributeString(obj, 'altvoltage0*raw', '0');
                obj.if_initialized = 0;
                if(ret < 0)
                    obj.iio_channel = {};
                    err_msg = 'Could not enable the DAC buffer output!';
                    return;
                end

                % Create the IIO buffer used to write data
                obj.iio_buf_size = obj.data_ch_size * obj.iio_scan_elm_no;
                obj.iio_buffer = calllib(obj.libname, 'iio_device_create_buffer', obj.iio_dev,...
                                         obj.data_ch_size, 1);
            end

            msg_log = [msg_log sprintf('%s: %s output data channels successfully initialized\n', class(obj), obj.dev_name)];

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Initializes the input data channels
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = initInputDataChannels(obj, ch_no, ch_size)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Save the number of channels and size
            obj.data_ch_no = ch_no;
            obj.data_ch_size = ch_size;

            % Get the number of channels that the device has
            nb_channels = calllib(obj.libname, 'iio_device_get_channels_count', obj.iio_dev);
            if(nb_channels == 0)
                err_msg = 'The selected device does not have any channels!';
                return;
            end

            % Enable the system object output channels
            if(ch_no ~= 0)
                % Check if the device has input channels. The
                % logic here assumes that a device can have
                % only input or only output channels
                obj.iio_channel{1} = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, 0);
                is_output = calllib(obj.libname, 'iio_channel_is_output', obj.iio_channel{1});
                if(is_output == 1)
                    err_msg = 'The selected device does not have input channels!';
                    return;
                end
                msg_log = [msg_log sprintf('%s: Found %d input channels for the device %s\n', class(obj), nb_channels, obj.dev_name)];

                % Check if the number of channels in the device
                % is greater or equal to the system object
                % output channels
                if(nb_channels < ch_no)
                    obj.iio_channel = {};
                    err_msg = 'The selected device does not have enough input channels!';
                    return;
                end

                % Enable the channels
                for j = 0 : ch_no - 1
                    obj.iio_channel{j+1} = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, j);
                    calllib(obj.libname, 'iio_channel_enable', obj.iio_channel{j+1});
                end
                for j = ch_no : nb_channels - 1
                    obj.iio_channel{j+1} = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, j);
                    calllib(obj.libname, 'iio_channel_disable', obj.iio_channel{j+1});
                end
                % Create the IIO buffer used to read data
                obj.iio_buf_size = obj.data_ch_size * obj.data_ch_no;
                obj.iio_buffer = calllib(obj.libname, 'iio_device_create_buffer', obj.iio_dev, obj.iio_buf_size, 0);
            end

            msg_log = [msg_log sprintf('%s: %s input data channels successfully initialized\n', class(obj), obj.dev_name)];

            % Set the return code to success
            ret = 0;
        end

    end


    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    %% Public methods
    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    methods
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Constructor
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function obj = libiio_if()
            % Constructor
            obj.if_initialized = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Destructor
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function delete(obj)
            % Release any resources used by the system object.
            if((obj.if_initialized == 1) && libisloaded(obj.libname))
                if(~isempty(obj.iio_buffer))
                    calllib(obj.libname, 'iio_buffer_destroy', obj.iio_buffer);
                end
                if(~isempty(obj.iio_ctx))
                    calllib(obj.libname, 'iio_context_destroy', obj.iio_ctx);
                end
                obj.iio_buffer = {};
                obj.iio_channel = {};
                obj.iio_dev = {};
                obj.iio_ctx = {};
                instCnt = libiio_if.modInstanceCnt(-1);
                if(instCnt == 0)
                    unloadlibrary(obj.libname);
                end
            end
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Initializes the libiio interface
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, err_msg, msg_log] = init(obj, ip_address, ...
                                                dev_name, dev_type, ...
                                                data_ch_no, data_ch_size)
            % Initialize the return values
            ret = -1;
            err_msg = '';
            msg_log = [];

            % Save the device type
            obj.dev_type = dev_type;

            % Set the initialization status to fail
            obj.if_initialized = 0;

            % Load the libiio library
            if(~libisloaded(obj.libname))
                try
                    [notfound, warnings] = loadlibrary(obj.libname, obj.hname);
                catch exception
                    err_msg = exception.message;
                    return;
                end
            end

            if(~libisloaded(obj.libname))
                err_msg = 'Could not load the libiio library!';
                return;
            end

            % Create the network context
            [ret, err_msg, msg_log] = createNetworkContext(obj, ip_address);
            if(ret < 0)
                return;
            end

            % Check the software versions
            [ret, err_msg, msg_log_new] = checkVersions(obj);
            msg_log = [msg_log msg_log_new];
            if(ret < 0)
                releaseContext(obj);
                return;
            end

            % Initialize the device
            [ret, err_msg, msg_log_new] = initDevice(obj, dev_name);
            msg_log = [msg_log msg_log_new];
            if(ret < 0)
                releaseContext(obj);
                return;
            end

            % Initialize the output data channels
            if(strcmp(dev_type, 'OUT'))
                [ret, err_msg, msg_log_new] = initOutputDataChannels(obj, data_ch_no, data_ch_size);
                msg_log = [msg_log msg_log_new];
                if(ret < 0)
                    releaseContext(obj);
                    return;
                end
            end

            % Initialize the input data channels
            if(strcmp(dev_type, 'IN'))
                [ret, err_msg, msg_log_new] = initInputDataChannels(obj, data_ch_no, data_ch_size);
                msg_log = [msg_log msg_log_new];
                if(ret < 0)
                    releaseContext(obj);
                    return;
                end
            end

            % Set the initialization status to success
            obj.if_initialized = 1;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Implement the data capture flow
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, data] = readData(obj)
            % Initialize the return values
            ret = -1;
            data = cell(1, obj.data_ch_no);
            for i = 1 : obj.data_ch_no
                data{i} = zeros(obj.data_ch_size, 1);
            end

            % Check if the interface is initialized
            if(obj.if_initialized == 0)
                return;
            end

            % Check if the device type is output
            if(~strcmp(obj.dev_type, 'IN'))
                return;
            end

            % Read the data
            calllib(obj.libname, 'iio_buffer_refill', obj.iio_buffer);
            buffer = calllib(obj.libname, 'iio_buffer_first', obj.iio_buffer, obj.iio_channel{1});
            setdatatype(buffer, 'int16Ptr', obj.iio_buf_size);
            for i = 1 : obj.data_ch_no
                data{i} = double(buffer.Value(i:obj.data_ch_no:end));
            end

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Implement the data transmit flow
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function ret = writeData(obj, data)
            % Initialize the return values
            ret = -1;

            % Check if the interface is initialized
            if(obj.if_initialized == 0)
                return;
            end

            % Check if the device type is input
            if(~strcmp(obj.dev_type, 'OUT'))
                return;
            end

            % Destroy the buffer
            calllib(obj.libname, 'iio_buffer_destroy', obj.iio_buffer);
            obj.iio_buffer = {};

            % Enable the DAC buffer output
            ret = writeAttributeString(obj, 'altvoltage0*raw', '0');
            if(ret < 0)
                obj.iio_channel = {};
                err_msg = 'Could not enable the DAC buffer output!';
                return;
            end

            % Create the IIO buffer used to write data
            obj.iio_buf_size = obj.data_ch_size * obj.iio_scan_elm_no;
            obj.iio_buffer = calllib(obj.libname, 'iio_device_create_buffer', obj.iio_dev,...
                                     obj.data_ch_size, 1);

            % Transmit the data
            buffer = calllib(obj.libname, 'iio_buffer_start', obj.iio_buffer);
            setdatatype(buffer, 'int16Ptr', obj.iio_buf_size);
            for i = 1 : obj.data_ch_no
                buffer.Value(i : obj.iio_scan_elm_no : obj.iio_buf_size) = int16(data{i});
            end
            for i = obj.data_ch_no + 1 : obj.iio_scan_elm_no
                buffer.Value(i : obj.iio_scan_elm_no : obj.iio_buf_size) = 0;
            end
            calllib(obj.libname, 'iio_buffer_push', obj.iio_buffer);

            % Set the return code to success
            ret = 0;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Find an attribute based on the name. The name can contain wildcard '*' characters
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, ch, attr] = findAttribute(obj, attr_name)
            % Initialize the return values
            ret = -1;
            ch = 0;
            attr = '';

            % Check if the interface is initialized
            if(obj.if_initialized == 0)
                return;
            end

            % Check if this is a device attribute
            name = calllib(obj.libname, 'iio_device_find_attr', obj.iio_dev, attr_name);
            if(~isempty(name))
                ret = 0;
                return;
            end

            % This is a channel attribute, search for the corresponding channel
            chn_no = calllib(obj.libname, 'iio_device_get_channels_count', obj.iio_dev);
            for k = 0 : chn_no - 1
                ch = calllib(obj.libname, 'iio_device_get_channel', obj.iio_dev, k);
                attr_no = calllib(obj.libname, 'iio_channel_get_attrs_count', ch);
                attr_found = 0;
                for l = 0 : attr_no - 1
                    attr = calllib(obj.libname, 'iio_channel_get_attr', ch, l);
                    name = calllib(obj.libname, 'iio_channel_attr_get_filename', ch, attr);
                    % The attribute to find can contain wildcard '*' characters,
                    % search for all the substrings in the attribute name
                    str_find = strsplit(attr_name, '*');
                    str_find = str_find(find(~strcmp(str_find, '')));
                    has_wildcard = ~isempty(strfind(attr_name, '*'));
                    attr_found = 1;
                    for i = 1 : length(str_find)
                        if(has_wildcard == 0)
                            ret = strcmp(name, str_find{i});
                            if(ret == 0)
                                ret = [];
                            end
                        else
                            ret = strfind(name, str_find{i});
                        end
                        if(isempty(ret))
                            attr_found = 0;
                            break;
                        end
                    end
                    if(attr_found == 1)
                        break;
                    end
                    clear attr;
                end
                % Check if the attribute was found
                if(attr_found == 0)
                    clear ch;
                else
                    ret = 1;
                    break;
                end
            end
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Read an attribute as a double value
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, val] = readAttributeDouble(obj, attr_name)
            % Find the attribute
            [ret, ch, attr] = findAttribute(obj, attr_name);
            if(ret < 0)
                val = 0;
                return;
            end

            % Create a double pointer to be used for data read
            data = zeros(1, 10);
            pData = libpointer('doublePtr', data(1));

            % Read the attribute value
            if(ret > 0)
                calllib(obj.libname, 'iio_channel_attr_read_double', ch, attr, pData);
                clear ch;
                clear attr;
            else
                calllib(obj.libname, 'iio_device_attr_read_double', obj.iio_dev, attr_name, pData);
            end
            val = pData.Value;
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Read an attribute as a string value
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function [ret, val] = readAttributeString(obj, attr_name)
            % Find the attribute
            [ret, ch, attr] = findAttribute(obj, attr_name);
            if(ret < 0)
                val = '';
                return;
            end

            % Create a pointer to be used for data read
            data = char(ones(1, 512));
            pData = libpointer('stringPtr', data);

            % Read the attribute value
            if(ret > 0)
                [~, ~, ~, val] = calllib(obj.libname, 'iio_channel_attr_read', ch, attr, pData, 512);
                clear ch;
                clear attr;
            else
                [~, ~, ~, val] = calllib(obj.libname, 'iio_device_attr_read', obj.iio_dev, attr_name, pData, 512);
            end
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Write a string double value
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function ret = writeAttributeDouble(obj, attr_name, val)
            % Find the attribute
            [ret, ch, attr] = findAttribute(obj, attr_name);
            if(ret < 0)
                return;
            end

            % Write the attribute
            if(ret > 0)
                calllib(obj.libname, 'iio_channel_attr_write_double', ch, attr, val);
                clear ch;
                clear attr;
            else
                calllib(obj.libname, 'iio_device_attr_write_double', obj.iio_dev, attr_name, val);
            end
        end

        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        %% Write a string attribute value
        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        function ret = writeAttributeString(obj, attr_name, str)
            % Find the attribute
            [ret, ch, attr] = findAttribute(obj, attr_name);
            if(ret < 0)
                return;
            end

            % Write the attribute
            if(ret > 0)
                calllib(obj.libname, 'iio_channel_attr_write', ch, attr, str);
                clear ch;
                clear attr;
            else
                calllib(obj.libname, 'iio_device_attr_write', obj.iio_dev, attr_name, str);
            end
        end
    end
end
