function New-SessionManager {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidateNotNullOrEmpty()]
        [string]$SessionId,

        [Parameter(Mandatory = $true)]
        [ValidateNotNull()]
        [scriptblock]$HandshakeAction,

        [Parameter(Mandatory = $true)]
        [ValidateNotNull()]
        [scriptblock]$ActivationAction,

        [Parameter(Mandatory = $true)]
        [ValidateNotNull()]
        [scriptblock]$CleanupAction,

        [Parameter(Mandatory = $true)]
        [ValidateNotNull()]
        [scriptblock]$VerifyAction
    )

    return [SessionManager]::new(
        $SessionId,
        $HandshakeAction,
        $ActivationAction,
        $CleanupAction,
        $VerifyAction
    )
}
