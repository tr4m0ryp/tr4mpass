$ErrorActionPreference = 'Stop'

$moduleRoot = Split-Path -Path $PSCommandPath -Parent

. (Join-Path -Path $moduleRoot -ChildPath 'Classes/SessionManager.ps1')
. (Join-Path -Path $moduleRoot -ChildPath 'Public/New-SessionManager.ps1')

Export-ModuleMember -Function 'New-SessionManager'
