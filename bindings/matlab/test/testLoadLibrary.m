if isunix
    hfile = '/usr/share/libiio/matlab/iio-wrapper.h';
    loadlibraryArgs = {hfile,'includepath','/usr/local/include','addheader','iio.h'};
    [a2, b2] = loadlibrary('libiio', loadlibraryArgs{:});
    libfunctions('libiio')
elseif ispc
    % Code to run on Windows platform
end