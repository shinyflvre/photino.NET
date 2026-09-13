@echo off
setlocal
set PLATFORM=%1
if "%PLATFORM%"=="" set PLATFORM=x64
set MSBUILD=
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if "%MSBUILD%"=="" (
  echo MSBuild not found. Install Visual Studio with the C++ desktop workload.
  exit /b 1
)
cd /d "%~dp0Photino.Native"
"%MSBUILD%" Photino.Native.sln -t:Restore -p:RestorePackagesConfig=true -p:Configuration=Release -p:Platform=%PLATFORM% -nologo -v:m
if errorlevel 1 exit /b 1
"%MSBUILD%" Photino.Native\Photino.Native.vcxproj -t:Build -p:Configuration=Release -p:Platform=%PLATFORM% -nologo -v:m
if errorlevel 1 exit /b 1
echo.
echo Built Photino.Native\Photino.Native\%PLATFORM%\Release\Photino.Native.dll
endlocal
