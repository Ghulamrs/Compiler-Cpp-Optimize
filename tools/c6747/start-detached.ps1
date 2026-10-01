# start-detached.ps1 <cmd file>: runs it through WMI, outside the ssh session that asked - a lost
# connection does not take it down. wmic is not on this box; this is the same call.
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = "cmd.exe /c " + $args[0] }
exit $r.ReturnValue
