$moduleManifest = Join-Path -Path $PSScriptRoot -ChildPath '..\SessionManagement.psd1'
Import-Module $moduleManifest -Force

Describe 'SessionManager integration lifecycle' {
    It 'executes handshake, activation, cleanup, and verify with real artifacts' {
        $runId = [guid]::NewGuid().ToString('N')
        $workDir = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("session-manager-it-" + $runId)
        New-Item -Path $workDir -ItemType Directory -Force | Out-Null

        try {
            $staleFile = Join-Path -Path $workDir -ChildPath 'stale.tmp'
            New-Item -Path $staleFile -ItemType File -Force | Out-Null

            $handshakeFile = Join-Path -Path $workDir -ChildPath 'handshake.json'
            $activationFile = Join-Path -Path $workDir -ChildPath 'activation.json'
            $cleanupFile = Join-Path -Path $workDir -ChildPath 'cleanup.log'

            $manager = New-SessionManager `
                -SessionId 'integration-001' `
                -HandshakeAction {
                    param($context, $activationRecord)
                    $context.HandshakeData.File = $handshakeFile
                    $context.HandshakeData.Protocol = 'drmHandshake'
                    @{ protocol = 'drmHandshake'; status = 'ok' } | ConvertTo-Json | Set-Content -Path $handshakeFile -Encoding UTF8
                } `
                -ActivationAction {
                    param($context, $activationRecord)
                    $activationRecord.SessionId = $context.SessionId
                    $activationRecord.Status = 'Active'
                    $activationRecord.Path = $activationFile
                    $activationRecord | ConvertTo-Json | Set-Content -Path $activationFile -Encoding UTF8
                } `
                -CleanupAction {
                    param($context, $activationRecord)
                    if (Test-Path -Path $staleFile) {
                        Remove-Item -Path $staleFile -Force
                    }

                    "cleanup complete for $($context.SessionId)" | Set-Content -Path $cleanupFile -Encoding UTF8
                    $context.CleanupDetails.Path = $cleanupFile
                } `
                -VerifyAction {
                    param($context, $activationRecord)
                    if (-not (Test-Path -Path $context.HandshakeData.File)) { throw 'Handshake artifact missing.' }
                    if (-not (Test-Path -Path $activationRecord.Path)) { throw 'Activation artifact missing.' }
                    if (-not (Test-Path -Path $context.CleanupDetails.Path)) { throw 'Cleanup artifact missing.' }
                    if (Test-Path -Path $staleFile) { throw 'Stale file was not removed.' }

                    $activation = Get-Content -Path $activationRecord.Path -Raw | ConvertFrom-Json
                    if ($activation.Status -ne 'Active') { throw 'Activation status invalid.' }
                }

            $result = $manager.Run()

            $result.Succeeded | Should Be $true
            $result.Phases.Count | Should Be 4
            $result.Phases[0].Name | Should Be 'SessionHandshake'
            $result.Phases[1].Name | Should Be 'ActivationRecord'
            $result.Phases[2].Name | Should Be 'Cleanup'
            $result.Phases[3].Name | Should Be 'Verify'
            (Test-Path -Path $handshakeFile) | Should Be $true
            (Test-Path -Path $activationFile) | Should Be $true
            (Test-Path -Path $cleanupFile) | Should Be $true
            (Test-Path -Path $staleFile) | Should Be $false
        }
        finally {
            if (Test-Path -Path $workDir) {
                Remove-Item -Path $workDir -Recurse -Force
            }
        }
    }

    It 'runs cleanup and verify after activation failure in a real run' {
        $runId = [guid]::NewGuid().ToString('N')
        $workDir = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("session-manager-it-" + $runId)
        New-Item -Path $workDir -ItemType Directory -Force | Out-Null

        try {
            $handshakeFile = Join-Path -Path $workDir -ChildPath 'handshake.json'
            $cleanupFile = Join-Path -Path $workDir -ChildPath 'cleanup.log'

            $manager = New-SessionManager `
                -SessionId 'integration-activation-fail-001' `
                -HandshakeAction {
                    param($context, $activationRecord)
                    $context.HandshakeData.File = $handshakeFile
                    @{ protocol = 'drmHandshake'; status = 'ok' } | ConvertTo-Json | Set-Content -Path $handshakeFile -Encoding UTF8
                } `
                -ActivationAction {
                    param($context, $activationRecord)
                    throw 'Simulated activation outage'
                } `
                -CleanupAction {
                    param($context, $activationRecord)
                    "cleanup complete for $($context.SessionId)" | Set-Content -Path $cleanupFile -Encoding UTF8
                    $context.CleanupDetails.Path = $cleanupFile
                } `
                -VerifyAction {
                    param($context, $activationRecord)
                    if (-not (Test-Path -Path $context.HandshakeData.File)) { throw 'Handshake artifact missing.' }
                    if (-not (Test-Path -Path $context.CleanupDetails.Path)) { throw 'Cleanup artifact missing.' }
                    if (-not $context.Failure) { throw 'Expected failure context not found.' }
                    if ($context.Failure.Phase -ne 'ActivationRecord') { throw 'Failure phase was not captured as ActivationRecord.' }
                }

            $result = $manager.Run()

            $result.Succeeded | Should Be $false
            $result.Phases.Count | Should Be 4
            $result.Phases[1].Name | Should Be 'ActivationRecord'
            $result.Phases[1].Succeeded | Should Be $false
            $result.Phases[1].ErrorMessage | Should Match 'Simulated activation outage'
            $result.Phases[2].Name | Should Be 'Cleanup'
            $result.Phases[2].Succeeded | Should Be $true
            $result.Phases[3].Name | Should Be 'Verify'
            $result.Phases[3].Succeeded | Should Be $true
            (Test-Path -Path $cleanupFile) | Should Be $true
        }
        finally {
            if (Test-Path -Path $workDir) {
                Remove-Item -Path $workDir -Recurse -Force
            }
        }
    }
}
