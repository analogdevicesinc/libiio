              libiio Windows binary snapshot - README

   *********************************************************************
   *  The latest version of this snapshot can always be downloaded at: *
   *               https://github.com/analogdevicesinc/libiio          *
   *********************************************************************

In this archive, you should find the following directories:
o ./include : Common include files
o ./MinGW32 : 32-bit binaries compiled by the MinGW toolchain
o ./MinGW64 : 64-bit binaries compiled by the MinGW toolchain
o ./MS32    : 32-bit binaries compiled by the MicroSoft toolchain
o ./MS64    : 364bit binaries compiled by the MicroSoft toolchain

o Visual Studio:
  - Open existing or create a new project for your application
  - Copy iio.h, from the include\ directory, into your project and make sure that
    the location where the file reside appears in the 'Additional Include
    Directories' section (Configuration Properties -> C/C++ -> General).
  - Copy the relevant .lib file from MS32\ or MS64\ and add 'libiio.lib' to
    your 'Additional Dependencies' (Configuration Properties -> Linker -> Input)
    Also make sure that the directory where libiio.lib resides is added to
    'Additional Library Directories' (Configuration Properties -> Linker
    -> General)
  - If you use the static version of the libiio library, make sure that
    'Runtime Library' is set to 'Multi-threaded DLL (/MD)' (Configuration
    Properties -> C/C++ -> Code Generation).
  - Compile and run your application. If you use the DLL version of libiio,
    remember that you need to have a copy of the DLL either in the runtime
    directory or in system32

o WDK/DDK:
  - The following is an example of a sources files that you can use to compile
    a libiio 1.0 based console application. In this sample ..\libiio\ is the
    directory where you would have copied libiio.h as well as the relevant 
    libiio.lib

	TARGETNAME=your_app
	TARGETTYPE=PROGRAM
	USE_MSVCRT=1
	UMTYPE=console
	INCLUDES=..\libiio;$(DDK_INC_PATH)
	TARGETLIBS=..\libiio\libiio.lib
	SOURCES=your_app.c

o MinGW/cygwin
  - Copy libiio.h, from include/ to your default include directory,
    and copy the MinGW32/ or MinGW64/ .a files to your default library directory.
    Or, if you don't want to use the default locations, make sure that you feed
    the relevant -I and -L options to the compiler.
  - Add the '-liio' linker option when compiling.

o Additional information:
  - The libiio API documentation can be accessed at:
    http://analogdevicesinc.github.io/libiio/
  - For some libiio samples (including source), please have a look in examples/
    and tests/ directories
  - The MinGW and MS generated DLLs are fully interchangeable, provided that you
    use the import libs provided or generate one from the .def also provided.
  - If you find any issue, please visit 
    http://analogdevicesinc.github.io/libiio/
    and check the Issues section
