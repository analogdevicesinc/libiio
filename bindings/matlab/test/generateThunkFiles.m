libName = 'libiio';
libiiowrapperh = 'iio-wrapper.h';
iioh = 'iio.h';
fp = fileparts(which(iioh));
if isempty(fp)
    error('Cannot find %s', iioh);
end
loadlibraryArgs = {libiiowrapperh,'includepath',fp,...
    'addheader',iioh};


if ~libisloaded(libName)
    msgID = 'MATLAB:loadlibrary:StructTypeExists';
    warnStruct = warning('off',msgID);
    [~, ~] = loadlibrary(libName, loadlibraryArgs{:});
    warning(warnStruct);
end

if isunix
    rext = '.so';
elseif ispc
    rext = '.dll';
else
    rext = '.dylib';
end

mkdir 'thunks'
searchdir = [tempdir,filesep,'tp*'];
files = dir(searchdir);
for fileIndx = 1:length(files)
    disp(files(fileIndx).name)
    if files(fileIndx).isdir
        folder = [files(fileIndx).folder,filesep,files(fileIndx).name];
        filesInFolder = dir(folder);
        for i = 1:length(filesInFolder)
            if ~filesInFolder(i).isdir
                [~,~,ext] = fileparts(filesInFolder(i).name);
                if strcmpi(ext,rext)
                    fullpath = fullfile(...
                        folder,...
                        filesInFolder(i).name);
                    fprintf('Found thunk: %s\n', fullpath);
                    copyfile(fullpath, 'thunks');
                end
            end
        end
    end
end

unloadlibrary(libName);