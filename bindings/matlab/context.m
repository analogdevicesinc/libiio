classdef context < common
    % Context class implementation for the libiio library
    

    methods (Static)

        function ctx = iio_create_context(params, uri)
            % Create a new context
            % 
            % This function creates a new context.
            context.checkLibraryLoaded();
            if isempty(params)
                params = libpointer;
            end
            ctx = calllib(context.getLibraryName(), 'iio_create_context', params, uri);
        end

        function name = iio_context_get_name(ctx)
            context.checkLibraryLoaded();
            name = calllib(context.getLibraryName(), 'iio_context_get_name', ctx);
        end

    end

end