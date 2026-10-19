[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    Log-Group "Building installer for ${ProductName}..."
    $Iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
    if ( ! $Iscc ) {
        $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        if ( ! ( Test-Path $Iscc ) ) {
            Invoke-External choco install innosetup --yes --no-progress
        }
    }
    $InstallerDir = "${ProjectRoot}/installer/windows"
    $IsccArgs = @(
        "/DAppVersion=${ProductVersion}"
        "/DSourceDir=${ProjectRoot}elease\${Configuration}\${ProductName}"
        "/DOutputDir=${ProjectRoot}elease"
    )
    # Chinese wizard texts are Inno Setup's community translations; build in English if they are unavailable.
    $LanguageBase = 'https://raw.githubusercontent.com/jrsoftware/issrc/is-6_4_3/Files/Languages/Unofficial'
    try {
        Invoke-WebRequest -Uri "${LanguageBase}/ChineseSimplified.isl" -OutFile "${InstallerDir}/ChineseSimplified.isl"
        Invoke-WebRequest -Uri "${LanguageBase}/ChineseTraditional.isl" -OutFile "${InstallerDir}/ChineseTraditional.isl"
        $IsccArgs += '/DHaveChinese'
    } catch {
        Write-Warning "Chinese installer translations unavailable: $_"
    }
    Invoke-External $Iscc @IsccArgs "${InstallerDir}/kaibo.iss"
    Log-Group
}

Package
