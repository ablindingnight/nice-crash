# Screenshot the Nice Crash game window after N seconds: shot.ps1 <seconds> <out.png>
param([int]$sec = 10, [string]$out = "build\shot.png")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; } }
"@
Start-Sleep -Seconds $sec
$h = [W]::FindWindow("NiceCrashGame", $null)
if ($h -eq [IntPtr]::Zero) { "no window"; exit 1 }
$r = New-Object W+RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
$p = New-Object W+POINT; [W]::ClientToScreen($h, [ref]$p) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($p.X, $p.Y, 0, 0, $bmp.Size)
$bmp.Save($out); "saved $out"
