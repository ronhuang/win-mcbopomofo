# Windows dev container for Win-McBopomofo.
# Requires Docker Desktop for Windows in "Windows containers" mode.

FROM mcr.microsoft.com/windows/servercore:ltsc2022

# NOTE: Pre-commands must not be included in this array. Docker on Windows wraps
# the SHELL+RUN body in outer double-quotes for CreateProcess, stripping all inner
# double-quotes. Each RUN therefore starts with the $ErrorActionPreference preamble
# explicitly, and uses only single-quoted strings + concatenation throughout.
SHELL ["powershell", "-NoLogo", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command"]

LABEL org.opencontainers.image.title="win-mcbopomofo-dev" \
      org.opencontainers.image.description="Windows dev container with all prerequisites for Win-McBopomofo" \
      org.opencontainers.image.source="https://github.com/openvanilla/win-mcbopomofo" \
      org.opencontainers.image.licenses="MIT"

# Version pins – override at build time with --build-arg if needed.
ARG CMAKE_VERSION=4.3.3
ARG LLVM_VERSION=22.1.0
ARG GIT_VERSION=2.54.0
ARG PYTHON_VERSION=3.14.5
ARG DOTNET_SDK_VERSION=10.0.300
ARG WIX_VERSION=7.0.0
ARG WIX_UI_EXT_VERSION=7.0.0
ARG WIX_UTIL_EXT_VERSION=7.0.0

ENV CMAKE_VERSION=${CMAKE_VERSION} \
    LLVM_VERSION=${LLVM_VERSION} \
    GIT_VERSION=${GIT_VERSION} \
    PYTHON_VERSION=${PYTHON_VERSION} \
    DOTNET_SDK_VERSION=${DOTNET_SDK_VERSION} \
    WIX_VERSION=${WIX_VERSION} \
    WIX_UI_EXT_VERSION=${WIX_UI_EXT_VERSION} \
    WIX_UTIL_EXT_VERSION=${WIX_UTIL_EXT_VERSION}

# 1. Git
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://github.com/git-for-windows/git/releases/download/v' + $env:GIT_VERSION + '.windows.1/Git-' + $env:GIT_VERSION + '-64-bit.exe'; \
    $installer = 'C:\TEMP\git-installer.exe'; \
    New-Item -ItemType Directory -Force C:\TEMP | Out-Null; \
    Write-Host ('Downloading Git ' + $env:GIT_VERSION + ' ...'); \
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing; \
    Write-Host 'Installing Git ...'; \
    Start-Process $installer -Wait -ArgumentList @( \
        '/VERYSILENT', \
        '/NORESTART', \
        '/NOCANCEL', \
        '/SP-', \
        '/COMPONENTS=icons,ext\reg\shellhere,assoc,assoc_sh', \
        '/o:PathOption=Cmd' \
    ); \
    Remove-Item $installer -Force; \
    Write-Host 'Git installation complete.'

RUN git --version

# 2. Visual Studio 2022 Build Tools with MSVC (x64/x86/ARM64) and Windows 11 SDK.
# SDK 22621 (Win 11 22H2) is used instead of the latest 26100 because rc.exe from
# SDK 26100 crashes with STATUS_ACCESS_VIOLATION on the LTSC 2022 container OS.
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://aka.ms/vs/17/release/vs_buildtools.exe'; \
    $installer = 'C:\TEMP\vs_buildtools.exe'; \
    Write-Host 'Downloading Visual Studio 2022 Build Tools ...'; \
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing; \
    Write-Host 'Installing VS 2022 Build Tools (this may take 10-20 minutes) ...'; \
    $exitCode = (Start-Process $installer -Wait -PassThru -ArgumentList @( \
        '--quiet', \
        '--norestart', \
        '--nocache', \
        '--installPath', 'C:\BuildTools', \
        '--add', 'Microsoft.VisualStudio.Workload.VCTools', \
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', \
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.ARM64', \
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.ARM64EC', \
        '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621' \
    )).ExitCode; \
    Remove-Item $installer -Force; \
    if ($exitCode -ne 0 -and $exitCode -ne 3010) { \
        throw ('VS Build Tools installer exited with code ' + $exitCode); \
    }; \
    Write-Host ('VS Build Tools installation complete (exit code: ' + $exitCode + ').')

# Run vcvarsall.bat and persist every variable it sets at Machine scope so that
# INCLUDE, LIB, WindowsSdkBinPath, etc. are available to cl.exe and link.exe.
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $vsWhere     = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'; \
    $installPath = & $vsWhere -products * -latest -property installationPath; \
    $vcvarsall   = Join-Path $installPath 'VC\Auxiliary\Build\vcvarsall.bat'; \
    Write-Host ('Capturing environment from ' + $vcvarsall + ' x64 ...'); \
    $envLines = cmd /c ('"' + $vcvarsall + '" x64 && set') 2>&1; \
    $envLines | Where-Object { $_ -match '^[A-Za-z_][A-Za-z0-9_()]*=' } | ForEach-Object { \
        $parts = $_ -split '=', 2; \
        [Environment]::SetEnvironmentVariable($parts[0], $parts[1], 'Machine'); \
    }; \
    Write-Host ('WindowsSdkBinPath = ' + [Environment]::GetEnvironmentVariable('WindowsSdkBinPath', 'Machine')); \
    Write-Host 'VS developer environment persisted.'

# 3. CMake
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://github.com/Kitware/CMake/releases/download/v' + $env:CMAKE_VERSION + '/cmake-' + $env:CMAKE_VERSION + '-windows-x86_64.msi'; \
    $installer = 'C:\TEMP\cmake.msi'; \
    Write-Host ('Downloading CMake ' + $env:CMAKE_VERSION + ' ...'); \
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing; \
    Write-Host 'Installing CMake ...'; \
    Start-Process msiexec.exe -Wait -ArgumentList @( \
        '/i', $installer, \
        '/quiet', \
        '/norestart', \
        'ADD_CMAKE_TO_PATH=System' \
    ); \
    Remove-Item $installer -Force; \
    Write-Host 'CMake installation complete.'

RUN cmake --version

# 4. LLVM (provides clang-format)
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-' + $env:LLVM_VERSION + '/LLVM-' + $env:LLVM_VERSION + '-win64.exe'; \
    $installer = 'C:\TEMP\llvm.exe'; \
    Write-Host ('Downloading LLVM ' + $env:LLVM_VERSION + ' ...'); \
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing; \
    Write-Host 'Installing LLVM ...'; \
    Start-Process $installer -Wait -ArgumentList '/S'; \
    Remove-Item $installer -Force; \
    $llvmBin = 'C:\Program Files\LLVM\bin'; \
    $current = [Environment]::GetEnvironmentVariable('Path', 'Machine'); \
    [Environment]::SetEnvironmentVariable('Path', $llvmBin + ';' + $current, 'Machine'); \
    Write-Host 'LLVM installation complete.'

RUN clang-format --version

# 5. Python (required by OpenCC's dictionary build step)
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://www.python.org/ftp/python/' + $env:PYTHON_VERSION + '/python-' + $env:PYTHON_VERSION + '-amd64.exe'; \
    $installer = 'C:\TEMP\python.exe'; \
    Write-Host ('Downloading Python ' + $env:PYTHON_VERSION + ' ...'); \
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing; \
    Write-Host 'Installing Python ...'; \
    Start-Process $installer -Wait -ArgumentList @( \
        '/quiet', \
        'InstallAllUsers=1', \
        'PrependPath=1', \
        'Include_test=0', \
        'Include_doc=0' \
    ); \
    Remove-Item $installer -Force; \
    Write-Host 'Python installation complete.'

RUN python --version

# 6. .NET SDK (required by WiX v7, which is a dotnet global tool)
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $url = 'https://dot.net/v1/dotnet-install.ps1'; \
    $script = 'C:\TEMP\dotnet-install.ps1'; \
    Write-Host 'Downloading .NET install script ...'; \
    Invoke-WebRequest -Uri $url -OutFile $script -UseBasicParsing; \
    Write-Host ('Installing .NET SDK ' + $env:DOTNET_SDK_VERSION + ' ...'); \
    & $script -Version $env:DOTNET_SDK_VERSION -InstallDir 'C:\dotnet' -NoPath; \
    Remove-Item $script -Force; \
    $current = [Environment]::GetEnvironmentVariable('Path', 'Machine'); \
    [Environment]::SetEnvironmentVariable('Path', 'C:\dotnet;' + $current, 'Machine'); \
    [Environment]::SetEnvironmentVariable('DOTNET_ROOT', 'C:\dotnet', 'Machine'); \
    Write-Host '.NET SDK installation complete.'

RUN dotnet --version

# 7. WiX Toolset v7
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    $toolsDir = 'C:\dotnet-tools'; \
    New-Item -ItemType Directory -Force $toolsDir | Out-Null; \
    [Environment]::SetEnvironmentVariable('DOTNET_TOOLS', $toolsDir, 'Machine'); \
    $current = [Environment]::GetEnvironmentVariable('Path', 'Machine'); \
    [Environment]::SetEnvironmentVariable('Path', $toolsDir + ';' + $current, 'Machine'); \
    dotnet tool install --tool-path $toolsDir wix --version $env:WIX_VERSION; \
    Write-Host 'WiX toolset installed.'; \
    & ($toolsDir + '\wix.exe') eula accept wix7; \
    Write-Host 'WiX EULA accepted.'; \
    & ($toolsDir + '\wix.exe') extension add -g ('WixToolset.UI.wixext/' + $env:WIX_UI_EXT_VERSION); \
    & ($toolsDir + '\wix.exe') extension add -g ('WixToolset.Util.wixext/' + $env:WIX_UTIL_EXT_VERSION); \
    Write-Host 'WiX extensions installed.'

RUN wix --version

# Verify all tools are reachable before finalising the image.
RUN $ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; \
    Write-Host '--- Tool versions ---'; \
    git --version; \
    cmake --version | Select-Object -First 1; \
    python --version; \
    clang-format --version; \
    dotnet --version; \
    wix --version; \
    Write-Host '--- All prerequisites verified ---'

# Mark C:\src as safe for Git to avoid "dubious ownership" errors
# with Docker volume mounts (host user != ContainerAdministrator)
RUN git config --global --add safe.directory C:\src

WORKDIR C:\\src

CMD ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
