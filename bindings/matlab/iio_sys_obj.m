% Copyright 2014-15(c) Analog Devices, Inc.
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

classdef iio_sys_obj < matlab.System & matlab.system.mixin.Propagates ...
        & matlab.system.mixin.CustomIcon
    % iio_sys_obj System Object block for IIO devices
    
    properties (Nontunable)
        % Public, non-tunable properties.
        
        %ip_address IP address
        ip_address = '';
        
        %dev_name Device name
        dev_name = '';
        
        %in_ch_no Number of input data channels
        in_ch_no = 0;
        
        %in_ch_size Input data channel size [samples]
        in_ch_size = 8192;
        
        %out_ch_no Number of output data channels
        out_ch_no = 0;
        
        %out_ch_size Output data channel size [samples]
        out_ch_size = 8192;
    end
    
    properties (Access = protected)
        % Protected class properties.
        
        %iio_dev_cfg Device configuration structure
        iio_dev_cfg = [];
    end
    
    properties (Access = private)
        % Private class properties.
        
        %libiio_data_in_dev libiio IIO interface object for the input data device
        libiio_data_in_dev = {};
        
        %libiio_data_out_dev libiio IIO interface object for the output data device
        libiio_data_out_dev = {};
        
        %libiio_ctrl_dev libiio IIO interface object for the control device
        libiio_ctrl_dev  = {};
        
        %sys_obj_initialized Holds the initialization status of the system object
        sys_obj_initialized = 0;
    end
    
    properties (DiscreteState)
        % Discrete state properties.
        
        %num_cfg_in Numeric type input control channels data
        num_cfg_in;
        
        %str_cfg_in String type input control channels data
        str_cfg_in;
    end
    
    methods
        %% Constructor
        function obj = iio_sys_obj(varargin)
            % Construct the libiio interface objects
            obj.libiio_data_in_dev = libiio_if();
            obj.libiio_data_out_dev = libiio_if();
            obj.libiio_ctrl_dev = libiio_if();
            
            % Support name-value pair arguments when constructing the object.
            setProperties(obj,nargin,varargin{:});
        end
    end
    
    methods (Access = protected)
        %% Utility functions
        
        function config = getObjConfig(obj)
            % Read the selected device configuration
            
            % Open the configuration file
            fname = sprintf('%s.cfg', obj.dev_name);
            fp_cfg = fopen(fname);
            if(fp_cfg < 0)
                config = {};
                return;
            end
            
            % Build the object configuration structure
            config = struct('data_in_device', '',... 	% Pointer to the data input device
                'data_out_device', '',...	% Pointer to the data output device
                'ctrl_device', '',... 	 	% Pointer to the control device
                'cfg_ch', [],...      	 	% Configuration channels list
                'mon_ch', []);        	 	% Monitoring channels list
            
            % Build the configuration/monitoring channels structure
            ch_cfg = struct('port_name', '',...         % Name of the port to be displayed on the object block
                'port_attr', '',...         % Associated device attribute name
                'ctrl_dev_name', '',...     % Control device name
                'ctrl_dev', 0);             % Pointer to the control device object
            
            % Read the object's configuration
            while(~feof(fp_cfg))
                line = fgets(fp_cfg);
                if(strfind(line,'#'))
                    continue;
                end
                if(~isempty(strfind(line, 'channel')))
                    % Get the associated configuration/monitoring channels
                    idx = strfind(line, '=');
                    line = line(idx+1:end);
                    line = strsplit(line, ',');
                    ch_cfg.port_name = strtrim(line{1});
                    ch_cfg.port_attr = strtrim(line{3});
                    if(length(line) > 4)
                        ch_cfg.ctrl_dev_name = strtrim(line{4});
                    else
                        ch_cfg.ctrl_dev_name = 'ctrl_device';
                    end
                    if(strcmp(strtrim(line{2}), 'IN'))
                        config.cfg_ch = [config.cfg_ch ch_cfg];
                    elseif(strcmp(strtrim(line{2}), 'OUT'))
                        config.mon_ch = [config.mon_ch ch_cfg];
                    end
                elseif(~isempty(strfind(line, 'data_in_device')))
                    % Get the associated data input device
                    idx = strfind(line, '=');
                    tmp = line(idx+1:end);
                    tmp = strtrim(tmp);
                    config.data_in_device = tmp;
                elseif(~isempty(strfind(line, 'data_out_device')))
                    % Get the associated data output device
                    idx = strfind(line, '=');
                    tmp = line(idx+1:end);
                    tmp = strtrim(tmp);
                    config.data_out_device = tmp;
                elseif(~isempty(strfind(line, 'ctrl_device')))
                    % Get the associated control device
                    idx = strfind(line, '=');
                    tmp = line(idx+1:end);
                    tmp = strtrim(tmp);
                    config.ctrl_device = tmp;
                end
            end
            fclose(fp_cfg);
        end
        
    end
    
    methods (Access = protected)
        %% Common functions
        function setupImpl(obj)
            % Implement tasks that need to be performed only once.
            
            % Set the initialization status to fail
            obj.sys_obj_initialized = 0;
            
            % Read the object's configuration from the associated configuration file
            obj.iio_dev_cfg = getObjConfig(obj);
            if(isempty(obj.iio_dev_cfg))
                msgbox('Could not read device configuration!', 'Error','error');
                return;
            end
            
            % Initialize discrete-state properties.
            obj.num_cfg_in = zeros(1, length(obj.iio_dev_cfg.cfg_ch));
            obj.str_cfg_in = zeros(length(obj.iio_dev_cfg.cfg_ch), 64);
            
            % Initialize the libiio data input device
            if(obj.in_ch_no ~= 0)
                [ret, err_msg, msg_log] = init(obj.libiio_data_in_dev, obj.ip_address, ...
                    obj.iio_dev_cfg.data_in_device, 'OUT', ...
                    obj.in_ch_no, obj.in_ch_size);
                fprintf('%s', msg_log);
                if(ret < 0)
                    msgbox(err_msg, 'Error','error');
                    return;
                end
            end
            
            % Initialize the libiio data output device
            if(obj.out_ch_no ~= 0)
                [ret, err_msg, msg_log] = init(obj.libiio_data_out_dev, obj.ip_address, ...
                    obj.iio_dev_cfg.data_out_device, 'IN', ...
                    obj.out_ch_no, obj.out_ch_size);
                fprintf('%s', msg_log);
                if(ret < 0)
                    msgbox(err_msg, 'Error','error');
                    return;
                end
            end
            
            % Initialize the libiio control device
            if(~isempty(obj.iio_dev_cfg.ctrl_device))
                [ret, err_msg, msg_log] = init(obj.libiio_ctrl_dev, obj.ip_address, ...
                    obj.iio_dev_cfg.ctrl_device, '', ...
                    0, 0);
                fprintf('%s', msg_log);
                if(ret < 0)
                    msgbox(err_msg, 'Error','error');
                    return;
                end
            end
            
            % Assign the control device for each monitoring channel
            for i = 1 : length(obj.iio_dev_cfg.mon_ch)
                if(strcmp(obj.iio_dev_cfg.mon_ch(i).ctrl_dev_name, 'data_in_device'))
                    obj.iio_dev_cfg.mon_ch(i).ctrl_dev = obj.libiio_data_in_dev;
                elseif(strcmp(obj.iio_dev_cfg.mon_ch(i).ctrl_dev_name, 'data_out_device'))
                    obj.iio_dev_cfg.mon_ch(i).ctrl_dev = obj.libiio_data_out_dev;
                else
                    obj.iio_dev_cfg.mon_ch(i).ctrl_dev = obj.libiio_ctrl_dev;
                end
            end
            
            % Assign the control device for each configuration channel
            for i = 1 : length(obj.iio_dev_cfg.cfg_ch)
                if(strcmp(obj.iio_dev_cfg.cfg_ch(i).ctrl_dev_name, 'data_in_device'))
                    obj.iio_dev_cfg.cfg_ch(i).ctrl_dev = obj.libiio_data_in_dev;
                elseif(strcmp(obj.iio_dev_cfg.cfg_ch(i).ctrl_dev_name, 'data_out_device'))
                    obj.iio_dev_cfg.cfg_ch(i).ctrl_dev = obj.libiio_data_out_dev;
                else
                    obj.iio_dev_cfg.cfg_ch(i).ctrl_dev = obj.libiio_ctrl_dev;
                end
            end
            
            % Set the initialization status to success
            obj.sys_obj_initialized = 1;
        end
        
        function releaseImpl(obj)
            % Release any resources used by the system object.
            obj.iio_dev_cfg = {};
            delete(obj.libiio_data_in_dev);
            delete(obj.libiio_data_out_dev);
            delete(obj.libiio_ctrl_dev);
        end
        
        function varargout = stepImpl(obj, varargin)
            % Implement the system object's processing flow.
            varargout = cell(1, obj.out_ch_no + length(obj.iio_dev_cfg.mon_ch));
            if(obj.sys_obj_initialized == 0)
                return;
            end
            
            % Implement the device configuration flow
            for i = 1 : length(obj.iio_dev_cfg.cfg_ch)
                if(~isempty(varargin{i + obj.in_ch_no}))
                    if(length(varargin{i + obj.in_ch_no}) == 1)
                        new_data = (varargin{i + obj.in_ch_no} ~= obj.num_cfg_in(i));
                    else
                        new_data = ~strncmp(char(varargin{i + obj.in_ch_no}'), char(obj.str_cfg_in(i,:)), length(varargin{i + obj.in_ch_no}));
                    end
                    if(new_data == 1)
                        if(length(varargin{i + obj.in_ch_no}) == 1)
                            obj.num_cfg_in(i) = varargin{i + obj.in_ch_no};
                            str = num2str(obj.num_cfg_in(i));
                        else
                            for j = 1:length(varargin{i + obj.in_ch_no})
                                obj.str_cfg_in(i,j) = varargin{i + obj.in_ch_no}(j);
                            end
                            obj.str_cfg_in(i,j+1) = 0;
                            str = char(obj.str_cfg_in(i,:));
                        end
                        writeAttributeString(obj.iio_dev_cfg.cfg_ch(i).ctrl_dev, obj.iio_dev_cfg.cfg_ch(i).port_attr, str);
                    end
                end
            end
            
            % Implement the data transmit flow
            writeData(obj.libiio_data_in_dev, varargin);
            
            % Implement the data capture flow
            [~, data] = readData(obj.libiio_data_out_dev);
            for i = 1 : obj.out_ch_no
                varargout{i} = data{i};
            end
            
            % Implement the parameters monitoring flow
            for i = 1 : length(obj.iio_dev_cfg.mon_ch)
                [~, val] = readAttributeDouble(obj.iio_dev_cfg.mon_ch(i).ctrl_dev, obj.iio_dev_cfg.mon_ch(i).port_attr);
                varargout{obj.out_ch_no + i} = val;
            end
            
            
        end
        
        function resetImpl(obj)
            % Initialize discrete-state properties.
            obj.num_cfg_in = zeros(1, length(obj.iio_dev_cfg.cfg_ch));
            obj.str_cfg_in = zeros(length(obj.iio_dev_cfg.cfg_ch), 64);
        end
        
        function num = getNumInputsImpl(obj)
            % Get number of inputs.
            num = obj.in_ch_no;
            
            config = getObjConfig(obj);
            if(~isempty(config))
                num = num + length(config.cfg_ch);
            end
        end
        
        function varargout = getInputNamesImpl(obj)
            % Get input names
            
            % Get the number of input data channels
            data_ch_no = obj.in_ch_no;
            
            % Get number of control channels
            cfg_ch_no = 0;
            config = getObjConfig(obj);
            if(~isempty(config))
                cgf_ch_no = length(config.cfg_ch);
            end
            
            if(data_ch_no + cgf_ch_no ~= 0)
                varargout = cell(1, data_ch_no + cgf_ch_no);
                for i = 1 : data_ch_no
                    varargout{i} = sprintf('DATA_IN%d', i);
                end
                for i = data_ch_no + 1 : data_ch_no + cgf_ch_no
                    varargout{i} = config.cfg_ch(i - data_ch_no).port_name;
                end
            else
                varargout = {};
            end
        end
        
        function num = getNumOutputsImpl(obj)
            % Get number of outputs.
            num = obj.out_ch_no;
            
            config = getObjConfig(obj);
            if(~isempty(config))
                num = num + length(config.mon_ch);
            end
        end
        
        function varargout = getOutputNamesImpl(obj)
            % Get output names
            
            % Get the number of output data channels
            data_ch_no = obj.out_ch_no;
            
            % Get number of monitoring channels
            mon_ch_no = 0;
            config = getObjConfig(obj);
            if(~isempty(config))
                mon_ch_no = length(config.mon_ch);
            end
            
            if(data_ch_no + mon_ch_no ~= 0)
                varargout = cell(1, data_ch_no + mon_ch_no);
                for i = 1 : data_ch_no
                    varargout{i} = sprintf('DATA_OUT%d', i);
                end
                for i = data_ch_no + 1 : data_ch_no + mon_ch_no
                    varargout{i} = config.mon_ch(i - data_ch_no).port_name;
                end
            else
                varargout = {};
            end
        end
        
        function varargout = isOutputFixedSizeImpl(obj)
            % Get outputs fixed size.
            varargout = cell(1, getNumOutputs(obj));
            for i = 1 : getNumOutputs(obj)
                varargout{i} = true;
            end
        end
        
        function varargout = getOutputDataTypeImpl(obj)
            % Get outputs data types.
            varargout = cell(1, getNumOutputs(obj));
            for i = 1 : getNumOutputs(obj)
                varargout{i} = 'double';
            end
        end
        
        function varargout = isOutputComplexImpl(obj)
            % Get outputs data types.
            varargout = cell(1, getNumOutputs(obj));
            for i = 1 : getNumOutputs(obj)
                varargout{i} = false;
            end
        end
        
        function varargout = getOutputSizeImpl(obj)
            % Implement if input size does not match with output size.
            varargout = cell(1, getNumOutputs(obj));
            for i = 1:obj.out_ch_no
                varargout{i} = [obj.out_ch_size 1];
            end
            for i = obj.out_ch_no + 1 : length(varargout)
                varargout{i} = [1 1];
            end
        end
        
        function icon = getIconImpl(obj)
            % Define a string as the icon for the System block in Simulink.
            if(~isempty(obj.dev_name))
                icon = obj.dev_name;
            else
                icon = mfilename('class');
            end
        end
        
        %% Backup/restore functions
        function s = saveObjectImpl(obj)
            % Save private, protected, or state properties in a
            % structure s. This is necessary to support Simulink
            % features, such as SimState.
        end
        
        function loadObjectImpl(obj, s, wasLocked)
            % Read private, protected, or state properties from
            % the structure s and assign it to the object obj.
        end
        
        %% Simulink functions
        function z = getDiscreteStateImpl(obj)
            % Return structure of states with field names as
            % DiscreteState properties.
            z = struct([]);
        end
    end
    
    methods(Static, Access = protected)
        %% Simulink customization functions
        function header = getHeaderImpl(obj)
            % Define header for the System block dialog box.
            header = matlab.system.display.Header(mfilename('class'));
        end
        
        function group = getPropertyGroupsImpl(obj)
            % Define section for properties in System block dialog box.
            group = matlab.system.display.Section(mfilename('class'));
        end
    end
end
