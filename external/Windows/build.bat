@echo off
rem Runs CMake to configure Nupic for Visual Studio 2015 or 2017.
rem
rem Run this from the "Developer Command Prompt for VS" provided by Visual Studio
rem   so that vsvars32.bat gets executed to set the tool chain.



:CheckCMake
rem  make sure CMake is installed.
cmake -version > NUL 2> NUL 
if %errorlevel% neq 0 (
  @echo build.bat;  CMake was not found. 
  @echo Make sure its path is in the system PATH environment variable.
  exit /B 1
)


:Build
set BUILDDIR=build

pushd ..\..
rem remove build folder
if exist ".\%BUILDDIR%\" (
    rd /s /q "%BUILDDIR%\"
)
mkdir %BUILDDIR%
pushd %BUILDDIR%


:CheckVS
rem  make sure Visual studio is installed
if defined VS150COMNTOOLS (

  rem Run CMake using the Visual Studio generator for VS 2017
  cmake -G "Visual Studio 15 2017 Win64"  ..
  if exist "nupic.base.sln" (
  	@echo You can now start Visual Studio using solution file %BUILDDIR%/nupic.base.sln
	exit /B 0
  )    
  popd
  popd
) else (
  if defined VS140COMNTOOLS (

    rem Run CMake using the Visual Studio generator for VS 2015
    cmake -G "Visual Studio 14 2015 Win64"  ..
    if exist "nupic.base.sln" (
  	@echo You can now start Visual Studio using solution file %BUILDDIR%/nupic.base.sln
	exit /B 0
    )    
    popd
    popd
  ) else (
    @echo build.bat
    @echo  Visual Studio 2017 or 2015 not found
    @echo  "%%VS150COMNTOOLS%%" or "%%VS140COMNTOOLS%%" environment variable not defined
    @echo  You must execute this command using "Developer Command Prompt for VS2015" to set the tool chain.
    exit /B 1
  )
)


