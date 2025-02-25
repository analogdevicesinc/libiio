if isunix
    hfile = 'iio-wrapper.h';
    helpersFilePath = fileparts(mfilename('fullpath'));
    headerWrapperPath = strcat(helpersFilePath, filesep, "..", filesep);
    loadlibraryArgs = {headerWrapperPath,'includepath','/usr/include/iio','addheader','iio.h'};
    [a2, b2] = loadlibrary('libiio', loadlibraryArgs{:});
    libfunctions('libiio')
elseif ispc
    % Code to run on Windows platform
end