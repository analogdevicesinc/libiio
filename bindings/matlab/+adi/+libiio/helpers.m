classdef helpers < handle
    %%helpers
    methods (Hidden, Static)
        function libName = getIIOLibName()
            libName = 'libiio1';
        end

        function loadlibraryArgs = getIIOHeaderName()
            headerFile = 'iio.h';
            headerWrapperFile = 'iio-wrapper.h';
            if ispc
                headerPath = [getenv('PROGRAMFILES(X86)'), '\Microsoft Visual Studio 12.0\VC\include\iio\'];
                headerWrapperPath = strcat("..", filesep);
            elseif isunix
                headerPath = '/usr/local/lib'; % FIXME
                headerWrapperPath = strcat("..", filesep); % FIXME
            end
            loadlibraryArgs = {headerWrapperPath+headerWrapperFile,'includepath',headerPath,'addheader',headerFile};
        end

        function [notfound, warnings] = loadLibIIO()
            notfound = [];
            warnings = [];
            libName = adi.libiio.helpers.getIIOLibName();

            if ~libisloaded(libName)
                loadlibraryArgs = adi.libiio.helpers.getIIOHeaderName();
                [notfound, warnings] = loadlibrary(libName,loadlibraryArgs{:});
                if ~isempty(notfound)
                    % error
                end
            end
        end

        function unloadLibIIO()
            libName = adi.libiio.helpers.getIIOLibName();

            if libisloaded(libName)
                unloadlibrary(libName);
            end
        end

        function varargout = calllibADI(fn, varargin)
            [notfound, warnings] = adi.libiio.helpers.loadLibIIO();
            varargout = cell(1, nargout);
            varargoutLocal = calllib(adi.libiio.helpers.getIIOLibName(), fn, varargin{:});
            [varargout{:}] = varargoutLocal;
        end

        function strout = ntstr(strin)
            % Appends a null character to terminate the string.
            % This is needed for code generation since MATLAB character 
            % arrays are not null terminated in code generation.
            strout = [uint8(strin) uint8(0)];
        end
    end
end