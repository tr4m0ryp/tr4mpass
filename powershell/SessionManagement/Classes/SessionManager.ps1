class SessionPhaseResult {
    [string]$Name
    [bool]$Succeeded
    [datetime]$StartedAt
    [datetime]$CompletedAt
    [string]$ErrorMessage

    SessionPhaseResult([string]$name) {
        $this.Name = $name
        $this.Succeeded = $false
    }
}

class SessionExecutionResult {
    [string]$SessionId
    [bool]$Succeeded
    [datetime]$StartedAt
    [datetime]$CompletedAt
    [hashtable]$ActivationRecord
    [SessionPhaseResult[]]$Phases

    SessionExecutionResult() {
        $this.Phases = @()
        $this.Succeeded = $false
    }
}

class SessionManager {
    [string]$SessionId
    [hashtable]$Context
    [hashtable]$ActivationRecord
    hidden [System.Collections.ArrayList]$PhaseHistory
    hidden [scriptblock]$HandshakeAction
    hidden [scriptblock]$ActivationAction
    hidden [scriptblock]$CleanupAction
    hidden [scriptblock]$VerifyAction

    SessionManager(
        [string]$sessionId,
        [scriptblock]$handshakeAction,
        [scriptblock]$activationAction,
        [scriptblock]$cleanupAction,
        [scriptblock]$verifyAction
    ) {
        if ([string]::IsNullOrWhiteSpace($sessionId)) {
            throw 'SessionId is required.'
        }

        foreach ($pair in @(
            @{ Name = 'handshakeAction'; Value = $handshakeAction },
            @{ Name = 'activationAction'; Value = $activationAction },
            @{ Name = 'cleanupAction'; Value = $cleanupAction },
            @{ Name = 'verifyAction'; Value = $verifyAction }
        )) {
            if ($null -eq $pair.Value) {
                throw "$($pair.Name) cannot be null."
            }
        }

        $this.SessionId = $sessionId
        $this.Context = @{
            SessionId      = $sessionId
            HandshakeData  = @{}
            CleanupDetails = @{}
            Verification   = @{}
        }
        $this.ActivationRecord = @{}
        $this.PhaseHistory = [System.Collections.ArrayList]::new()
        $this.HandshakeAction = $handshakeAction
        $this.ActivationAction = $activationAction
        $this.CleanupAction = $cleanupAction
        $this.VerifyAction = $verifyAction
    }

    [SessionExecutionResult] Run() {
        $execution = [SessionExecutionResult]::new()
        $execution.SessionId = $this.SessionId
        $execution.StartedAt = [datetime]::UtcNow

        try {
            [void]$this.InvokePhase('SessionHandshake', $this.HandshakeAction)
            [void]$this.InvokePhase('ActivationRecord', $this.ActivationAction)
        }
        catch {
            $this.Context['Failure'] = @{
                Phase = $this.GetLastPhaseName()
                Error = $_.Exception.Message
            }
        }
        finally {
            try {
                [void]$this.InvokePhase('Cleanup', $this.CleanupAction)
            }
            catch {
                $this.Context['CleanupFailure'] = $_.Exception.Message
            }

            try {
                [void]$this.InvokePhase('Verify', $this.VerifyAction)
            }
            catch {
                $this.Context['VerificationFailure'] = $_.Exception.Message
            }
        }

        $phaseResults = @($this.PhaseHistory.ToArray())
        $execution.Phases = $phaseResults
        $execution.ActivationRecord = $this.CloneHashtable($this.ActivationRecord)
        $execution.CompletedAt = [datetime]::UtcNow
        $execution.Succeeded = $this.DetermineOverallSuccess($phaseResults)
        return $execution
    }

    hidden [SessionPhaseResult] InvokePhase([string]$name, [scriptblock]$action) {
        $phase = [SessionPhaseResult]::new($name)
        $phase.StartedAt = [datetime]::UtcNow
        try {
            $null = & $action $this.Context $this.ActivationRecord
            $phase.Succeeded = $true
            return $phase
        }
        catch {
            $phase.ErrorMessage = $_.Exception.Message
            throw
        }
        finally {
            $phase.CompletedAt = [datetime]::UtcNow
            [void]$this.PhaseHistory.Add($phase)
        }
    }

    hidden [string] GetLastPhaseName() {
        if ($this.PhaseHistory.Count -eq 0) {
            return ''
        }

        return $this.PhaseHistory[$this.PhaseHistory.Count - 1].Name
    }

    hidden [bool] DetermineOverallSuccess([SessionPhaseResult[]]$phaseResults) {
        if ($null -eq $phaseResults -or $phaseResults.Count -eq 0) {
            return $false
        }

        $required = @('SessionHandshake', 'ActivationRecord', 'Cleanup', 'Verify')
        foreach ($name in $required) {
            $matching = @($phaseResults | Where-Object { $_.Name -eq $name })
            if ($matching.Count -eq 0) {
                return $false
            }

            if (-not $matching[0].Succeeded) {
                return $false
            }
        }

        return $true
    }

    hidden [hashtable] CloneHashtable([hashtable]$source) {
        $copy = @{}
        if ($null -eq $source) {
            return $copy
        }

        foreach ($key in $source.Keys) {
            $copy[$key] = $source[$key]
        }

        return $copy
    }
}
