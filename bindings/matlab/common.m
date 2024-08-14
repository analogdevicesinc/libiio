classdef common < handle
    % Common class implementation for the libiio library
    % 
    % This class is a common parent class for all libiio classes. It
    % provides a common constructor and destructor for all classes.
    % 
    % See also: Context, Device, Channel, Attribute, Buffer, BufferData
    % 
    % ----------------------------------------------------------------------------

    properties (SetAccess = protected)
        % Pointer to the C++ object
        pImpl
    end

    methods (Access = protected)
        function obj = common()
            % Constructor
            % 
            % This function creates a new instance of the common class.
            % 
            % See also: delete
            % 
            % ----------------------------------------------------------------------------
            obj.pImpl = [];
        end

        function delete(obj)
            % Destructor
            % 
            % This function deletes the instance of the common class.
            % 
            % See also: common
            % 
            % ----------------------------------------------------------------------------
            if ~isempty(obj.pImpl)
                delete(obj.pImpl);
            end
        end

    end

    methods (Access = protected, Static)

        function libname = getLibraryName(~)
            % Get the library name
            % 
            % This function returns the name of the libiio library.
            % 
            % ----------------------------------------------------------------------------
            libname = 'libiio';
        end

        function checkLibraryLoaded(~)
            if ~libisloaded('libiio')
                context.loadLibrary()
%                 error('libiio library not loaded');
            end
        end

        function loadLibrary(~)
            % Load the libiio library
            % 
            % This function loads the libiio library.
            %
            libName = 'libiio';
            libiiowrapperh = 'iio-wrapper.h';
            iioh = 'iio.h';
            fp = fileparts(which(iioh));
            loadlibraryArgs = {libiiowrapperh,'includepath',fp,...
                'addheader',iioh};
            if ~libisloaded(libName)
                msgID = 'MATLAB:loadlibrary:StructTypeExists';
                warnStruct = warning('off',msgID);
                [~, ~] = loadlibrary(libName, loadlibraryArgs{:});
                warning(warnStruct);
            end
        end

 
    end

end