Import-Module "$PSScriptRoot/../SessionManagement.psd1" -Force

$manager = New-SessionManager `
    -SessionId 'session-001' `
    -HandshakeAction {
        param($context, $activationRecord)
        $context.HandshakeData.Protocol = 'drmHandshake'
        $context.HandshakeData.Version = '1.0'
    } `
    -ActivationAction {
        param($context, $activationRecord)
        $activationRecord.SessionId = $context.SessionId
        $activationRecord.Status = 'Active'
        $activationRecord.StartedAt = [datetime]::UtcNow
    } `
    -CleanupAction {
        param($context, $activationRecord)
        $context.CleanupDetails.DisposedTempFiles = 0
        $context.CleanupDetails.StaleHandlesClosed = 0
    } `
    -VerifyAction {
        param($context, $activationRecord)
        if (-not $context.HandshakeData.Protocol) {
            throw 'Handshake data missing.'
        }

        if (-not $activationRecord.Status -or $activationRecord.Status -ne 'Active') {
            throw 'Activation record not active.'
        }

        $context.Verification.Result = 'Passed'
    }

$result = $manager.Run()
$result
