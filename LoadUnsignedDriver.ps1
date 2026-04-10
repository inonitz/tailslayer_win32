param (
    [Parameter(Mandatory=$false, Position=0)]
    [ValidateSet("Load", "Unload", "Verify")]
    [string]$Action,

    [Parameter(Mandatory=$false)]
    [string]$DriverPath = "C:\Path\To\Your\UnsignedDriver.sys",

    [Parameter(Mandatory=$false)]
    [string]$DriverName = "MyTestDriver",

    [Parameter(Mandatory=$true, ParameterSetName="Help")]
    [Alias("h", "-Help")]
    [switch]$Help
)


# Utility Functions
function Confirm-Admin {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error "STRICT REQUIREMENT: This script must be run as an Administrator."
        exit
    }
}


function Verify-Driver {
    Write-Host "[*] Verifying status of: $DriverName" -ForegroundColor Cyan
    $status = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
    
    if ($null -eq $status) {
        Write-Host "[-] Driver service '$DriverName' is not registered." -ForegroundColor Yellow
    } else {
        Write-Host "[+] Service Status: $($status.Status)" -ForegroundColor Green
        $query = driverquery /v /fo csv | ConvertFrom-Csv | Where-Object { $_."DisplayName" -eq $DriverName -or $_."ModuleName" -eq $DriverName }
        if ($query) {
            Write-Host "[+] Kernel Module: Found and Active" -ForegroundColor Green
            $query | Select-Object "ModuleName", "IsSigned", "Status" | Out-Host
        } else {
            Write-Host "[-] Kernel Module: Not found in active memory." -ForegroundColor Red
        }
    }
}


function Load-MyDriver {
    Write-Host "[*] Action: Loading $DriverName" -ForegroundColor Cyan
    
    if (-not (Test-Path $DriverPath)) {
        Write-Error "Driver file not found at $DriverPath"
        return
    }

    $existingService = Get-Service -Name $DriverName -ErrorAction SilentlyContinue
    if ($null -eq $existingService) {
        sc.exe create $DriverName type= kernel binPath= $DriverPath
    }

    try {
        Start-Service -Name $DriverName
        Write-Host "[+] Driver started successfully." -ForegroundColor Green
        Verify-Driver
    } catch {
        Write-Host "[-] Load failed. Ensure 'testsigning' is ON." -ForegroundColor Red
        $_.Exception.Message
    }
}


function Unload-MyDriver {
    Write-Host "[*] Action: Unloading $DriverName" -ForegroundColor Cyan
    try {
        Stop-Service -Name $DriverName -ErrorAction Stop
        sc.exe delete $DriverName
        Write-Host "[+] Driver stopped and service entry removed." -ForegroundColor Green
    } catch {
        Write-Host "[-] Failed to unload. Driver may be busy or already stopped." -ForegroundColor Red
    }
}


function Show-Help {
    Write-Host @"
============================================================
DRIVER MANAGEMENT SCRIPT - USAGE GUIDE
============================================================
This script facilitates loading, unloading, and verifying 
unsigned .sys drivers in a Test-Signing environment.

COMMANDS:
  -Action Load    : Creates service, starts driver, and verifies.
  -Action Unload  : Stops driver and deletes service entry.
  -Action Verify  : Checks if the driver is active in the kernel.

PARAMETERS:
  -DriverPath     : Full path to the .sys file.
  -DriverName     : The name to register in the Service Manager.

EXAMPLES:
  .\ManageDriver.ps1 -Action Load -DriverPath "C:\test.sys" -DriverName "MyDev"
  .\ManageDriver.ps1 -Action Verify -DriverName "MyDev"
  .\ManageDriver.ps1 -Action Unload -DriverName "MyDev"
  .\ManageDriver.ps1 -Help

NOTE: Unsigned drivers require 'bcdedit /set testsigning on' 
and a system reboot to function.
============================================================
"@
}


# Check for help flag before anything else
if ($Help -or ($null -eq $Action -and $PSBoundParameters.Count -eq 0)) {
    Show-Help
    exit
}


# --- Main Logic ---
Confirm-Admin

switch ($Action) {
    "Load"   { Load-MyDriver }
    "Unload" { Unload-MyDriver }
    "Verify" { Verify-Driver }
}