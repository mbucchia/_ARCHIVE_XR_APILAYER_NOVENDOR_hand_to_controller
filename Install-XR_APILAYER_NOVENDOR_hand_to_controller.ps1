$JsonPath = Join-Path "$PSScriptRoot" "XR_APILAYER_NOVENDOR_hand_to_controller.json"

# Search for Ultraleap.
$ultraleapPath = $null
$layers = Get-Item HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit 2> $null | Select-Object -ExpandProperty property
foreach ($entry in $layers)
{
	if ($entry -match ".*\\UltraleapHandTracking.json")
	{
		$ultraleapPath = $entry
		break
	}
}

# To guarantee the loading order of the API layers, we remove Ultraleap, add our layer, then re-add Ultraleap.
if ($ultraleapPath)
{
	Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList @"
		& {
			Remove-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$ultraleapPath' -Force | Out-Null
			New-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$jsonPath' -PropertyType DWord -Value 0 -Force | Out-Null
			New-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$ultraleapPath' -PropertyType DWord -Value 0 -Force | Out-Null
		}
"@
}
else
{
	Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList @"
		& {
			New-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$jsonPath' -PropertyType DWord -Value 0 -Force | Out-Null
		}
"@
}
