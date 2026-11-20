param(
    [ValidateSet('nro', 'test', 'clean', 'pc-test', 'pc-publish', 'test-all')]
    [string]$Target = 'nro'
)

$ErrorActionPreference = 'Stop'
$bash = 'C:\msys64\usr\bin\bash.exe'
$dotnetFallback = 'C:\Program Files\dotnet\dotnet.exe'
$pcSolution = Join-Path $PSScriptRoot 'pc\TotkSync.slnx'
$pcAppProject = Join-Path $PSScriptRoot 'pc\TotkSync.App\TotkSync.App.csproj'

function Get-FreeDriveLetter {
    $used = (Get-PSDrive -PSProvider FileSystem).Name
    foreach ($letter in 'T', 'U', 'V', 'W', 'X', 'Y', 'Z') {
        if ($used -notcontains $letter) { return $letter }
    }
    throw 'No free drive letter between T and Z'
}

function Get-MakeInvocation([string]$target, [string]$driveLetter) {
    $switchDir = '/' + $driveLetter.ToLower() + '/switch'
    switch ($target) {
        'nro' { return @{ Msystem = 'MSYS'; Command = "source /etc/profile.d/devkit-env.sh; cd $switchDir && make" } }
        'clean' { return @{ Msystem = 'MSYS'; Command = "source /etc/profile.d/devkit-env.sh; cd $switchDir && make clean" } }
        'test' { return @{ Msystem = 'UCRT64'; Command = "cd $switchDir && make test" } }
    }
}

# The repo root is mounted (not switch/) so the Switch build can reach ../contracts.
function Invoke-SwitchMake([string]$target) {
    $letter = Get-FreeDriveLetter
    $drive = "${letter}:"
    subst $drive $PSScriptRoot | Out-Host
    try {
        $invocation = Get-MakeInvocation $target $letter
        $env:MSYSTEM = $invocation.Msystem
        & $bash -lc $invocation.Command | Out-Host
        return $LASTEXITCODE
    }
    finally {
        subst $drive /d | Out-Host
    }
}

function Get-DotnetPath {
    $onPath = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    if (Test-Path $dotnetFallback) { return $dotnetFallback }
    return $null
}

function Invoke-Dotnet([string]$requiredFile, [string[]]$arguments) {
    if (-not (Test-Path $requiredFile)) {
        $relativePath = $requiredFile.Substring($PSScriptRoot.Length + 1)
        [Console]::Error.WriteLine("PC project not found: $relativePath (pc/ is not created yet?)")
        return 1
    }
    $dotnet = Get-DotnetPath
    if (-not $dotnet) {
        [Console]::Error.WriteLine("dotnet not found on PATH or at $dotnetFallback")
        return 1
    }
    & $dotnet @arguments | Out-Host
    return $LASTEXITCODE
}

function Invoke-PcTest {
    return Invoke-Dotnet $pcSolution @('test', $pcSolution)
}

function Invoke-PcPublish {
    return Invoke-Dotnet $pcAppProject @('publish', $pcAppProject, '-p:PublishProfile=win-x64')
}

function Invoke-TestAll {
    $switchExit = Invoke-SwitchMake 'test'
    if ($switchExit -ne 0) { return $switchExit }
    return Invoke-PcTest
}

$exitCode = 1
switch ($Target) {
    'pc-test' { $exitCode = Invoke-PcTest }
    'pc-publish' { $exitCode = Invoke-PcPublish }
    'test-all' { $exitCode = Invoke-TestAll }
    default { $exitCode = Invoke-SwitchMake $Target }
}
exit $exitCode
