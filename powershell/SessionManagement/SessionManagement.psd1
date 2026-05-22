@{
    RootModule        = 'SessionManagement.psm1'
    ModuleVersion     = '1.0.0'
    GUID              = 'dca2d036-f71d-410f-a907-e03d9ce26315'
    Author            = 'tr4mpass contributors'
    CompanyName       = 'Community'
    Copyright         = '(c) tr4mpass contributors'
    Description       = 'Session lifecycle module with handshake, activation, cleanup, and verification phases.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @('New-SessionManager')
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
