Param ($VersionFile)

Try {
    $VersionString = git describe --tags --dirty --match v[0-9]*
}
Catch [System.Management.Automation.CommandNotFoundException] {
    Write-Host 'warning: Git was not found in the system PATH. Version info will be missing from the DLL.'
}

If ( ($VersionString -is [String]) -and ($VersionString -match '^v[0-9]+\.[0-9]+\.[0-9]+(-.*)?$') ) {
    $VersionNumber = $VersionString.Split('-', 2)[0].Substring(1).Replace('.', ',') + ",0"
}
Else {
    $VersionString = 'unknown'
    $VersionNumber = '0,0,0,0'
}

$FileContent = @"
#define VERSION_NUMBER $VersionNumber
#define VERSION_STRING "$VersionString"
"@

Try {
    $PrevFileContent = [System.IO.File]::ReadAllText($VersionFile).Trim()
}
Catch [System.IO.FileNotFoundException] {
}

If ( $FileContent -eq $PrevFileContent ) {
    Write-Host "Version: $VersionString (same as previous build)"
}
Else {
    Write-Host "Version: $VersionString (differs from previous build)"
    $FileContent | Out-File -FilePath $VersionFile
}
