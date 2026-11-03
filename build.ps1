param(
    [ValidateSet('nro', 'test', 'clean')]
    [string]$Target = 'nro'
)

$ErrorActionPreference = 'Stop'
$bash = 'C:\msys64\usr\bin\bash.exe'

function Get-FreeDriveLetter {
    $used = (Get-PSDrive -PSProvider FileSystem).Name
    foreach ($letter in 'T', 'U', 'V', 'W', 'X', 'Y', 'Z') {
        if ($used -notcontains $letter) { return $letter }
    }
    throw 'No free drive letter between T and Z'
}

function Get-MakeInvocation([string]$target, [string]$driveLetter) {
    $dir = '/' + $driveLetter.ToLower()
    switch ($target) {
        'nro' { return @{ Msystem = 'MSYS'; Command = "source /etc/profile.d/devkit-env.sh; cd $dir && make" } }
        'clean' { return @{ Msystem = 'MSYS'; Command = "source /etc/profile.d/devkit-env.sh; cd $dir && make clean" } }
        'test' { return @{ Msystem = 'UCRT64'; Command = "cd $dir && make test" } }
    }
}

$letter = Get-FreeDriveLetter
$drive = "${letter}:"
subst $drive $PSScriptRoot
$exitCode = 1
try {
    $invocation = Get-MakeInvocation $Target $letter
    $env:MSYSTEM = $invocation.Msystem
    & $bash -lc $invocation.Command
    $exitCode = $LASTEXITCODE
}
finally {
    subst $drive /d
}
exit $exitCode
