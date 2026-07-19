$moduleManifest = Join-Path -Path $PSScriptRoot -ChildPath '..\SessionManagement.psd1'
Import-Module $moduleManifest -Force

Describe 'SessionManager lifecycle' {
    It 'runs all phases in order and succeeds on happy path' {
        $order = [System.Collections.Generic.List[string]]::new()

        $manager = New-SessionManager `
            -SessionId 'happy-001' `
            -HandshakeAction {
                param($context, $activationRecord)
                $order.Add('SessionHandshake')
                $context.HandshakeData.Ready = $true
            } `
            -ActivationAction {
                param($context, $activationRecord)
                $order.Add('ActivationRecord')
                $activationRecord.Status = 'Active'
            } `
            -CleanupAction {
                param($context, $activationRecord)
                $order.Add('Cleanup')
                $context.CleanupDetails.Done = $true
            } `
            -VerifyAction {
                param($context, $activationRecord)
                $order.Add('Verify')
                if (-not $context.HandshakeData.Ready) { throw 'Handshake not ready.' }
                if ($activationRecord.Status -ne 'Active') { throw 'Activation missing.' }
            }

        $result = $manager.Run()

        $result.Succeeded | Should Be $true
        $result.SessionId | Should Be 'happy-001'
        $result.Phases.Count | Should Be 4
        $result.Phases[0].Name | Should Be 'SessionHandshake'
        $result.Phases[1].Name | Should Be 'ActivationRecord'
        $result.Phases[2].Name | Should Be 'Cleanup'
        $result.Phases[3].Name | Should Be 'Verify'
        @($order.ToArray()) | Should Be @('SessionHandshake', 'ActivationRecord', 'Cleanup', 'Verify')
    }

    It 'still runs cleanup and verify when handshake fails' {
        $order = [System.Collections.Generic.List[string]]::new()

        $manager = New-SessionManager `
            -SessionId 'fail-handshake-001' `
            -HandshakeAction {
                param($context, $activationRecord)
                $order.Add('SessionHandshake')
                throw 'Handshake failed'
            } `
            -ActivationAction {
                param($context, $activationRecord)
                $order.Add('ActivationRecord')
                $activationRecord.Status = 'Active'
            } `
            -CleanupAction {
                param($context, $activationRecord)
                $order.Add('Cleanup')
                $context.CleanupDetails.Done = $true
            } `
            -VerifyAction {
                param($context, $activationRecord)
                $order.Add('Verify')
                $context.Verification.Checked = $true
            }

        $result = $manager.Run()

        $result.Succeeded | Should Be $false
        $result.Phases.Count | Should Be 3
        $result.Phases[0].Name | Should Be 'SessionHandshake'
        $result.Phases[0].Succeeded | Should Be $false
        $result.Phases[0].ErrorMessage | Should Match 'Handshake failed'
        $result.Phases[1].Name | Should Be 'Cleanup'
        $result.Phases[1].Succeeded | Should Be $true
        $result.Phases[2].Name | Should Be 'Verify'
        $result.Phases[2].Succeeded | Should Be $true
        @($order.ToArray()) | Should Be @('SessionHandshake', 'Cleanup', 'Verify')
    }

    It 'marks overall run as failed when verify phase throws' {
        $manager = New-SessionManager `
            -SessionId 'fail-verify-001' `
            -HandshakeAction {
                param($context, $activationRecord)
                $context.HandshakeData.Ready = $true
            } `
            -ActivationAction {
                param($context, $activationRecord)
                $activationRecord.Status = 'Active'
            } `
            -CleanupAction {
                param($context, $activationRecord)
                $context.CleanupDetails.Done = $true
            } `
            -VerifyAction {
                param($context, $activationRecord)
                throw 'Verification failed'
            }

        $result = $manager.Run()

        $result.Succeeded | Should Be $false
        $result.Phases.Count | Should Be 4
        $result.Phases[3].Name | Should Be 'Verify'
        $result.Phases[3].Succeeded | Should Be $false
        $result.Phases[3].ErrorMessage | Should Match 'Verification failed'
    }
}
