# loaded by ui_test.ps1: verify actual preview size and pixels, including every supported point size
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public static class FP {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct LF {
        public int height, width, escapement, orientation, weight;
        public byte italic, underline, strikeout, charset, outprecision, clipprecision, quality, pitch;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string face;
    }
    [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] static extern int GetObjectW(IntPtr h, int n, out LF lf);
    public static LF Font(long h) {
        LF lf;
        IntPtr f = SendMessageW(new IntPtr(h), 0x31, IntPtr.Zero, IntPtr.Zero);
        if (f == IntPtr.Zero || GetObjectW(f, Marshal.SizeOf(typeof(LF)), out lf) == 0) throw new Exception("preview font unavailable");
        return lf;
    }
    public static int Dpi(long h) { return (int)GetDpiForWindow(new IntPtr(h)); }
    public static int[] Ink(Bitmap b) {
        int bg = b.GetPixel(0, 0).ToArgb(), count = 0, top = b.Height, bottom = -1;
        for (int y=0; y<b.Height; y++) for (int x=0; x<b.Width; x++) {
            if (b.GetPixel(x,y).ToArgb() == bg) continue;
            count++; if (y<top) top=y; if (y>bottom) bottom=y;
        }
        return new int[] { count, top, bottom };
    }
}
'@
function Test-T39 {
    $idx = 0
    foreach ($theme in @('dark', 'light')) {
        $app = Start-App
        if ($theme -eq 'light') { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_THEME_LIGHT 0) }
        $dlg = Open-Font $app
        $pv = Ctl $dlg $IDT.ID_PREVIEW
        $idx++; CkEq ('T39.' + $idx + ' ' + $theme + ': preview text is the requested sentence and digits') 'sphinx of black quartz, judge my vow. 0123456789' ([U]::Text($pv))
        $dpi = [FP]::Dpi($pv)
        foreach ($pt in $IDM.FONT_MIN..$IDM.FONT_MAX) {
            Set-Field $dlg $IDT.ID_SIZE ([string]$pt)
            $f = [FP]::Font($pv)
            $px = [int][Math]::Round($pt * $dpi / 72.0, [MidpointRounding]::AwayFromZero)
            $idx++; CkEq ('T39.' + $idx + ' ' + $theme + ': ' + $pt + ' pt is rendered at its actual selected size') (-$px) $f.height
            $bmp = [U]::Grab($pv)
            try {
                $ink = [FP]::Ink($bmp)
                $idx++; Ck ('T39.' + $idx + ' ' + $theme + ': ' + $pt + ' pt has a visible single line') ($ink[0] -gt 0 -and $ink[2] - $ink[1] -lt 2 * $px) ('ink ' + $ink[0] + ', text height ' + ($ink[2] - $ink[1] + 1))
                if ($pt -in @(9, 24, 70)) { $bmp.Save((Join-Path $Root ('build\audit106-preview-' + $theme + '-' + $pt + '.png')), [System.Drawing.Imaging.ImageFormat]::Png) }
            } finally { $bmp.Dispose() }
        }
        Press $dlg $IDT.ID_BOLD
        Press $dlg $IDT.ID_ITALIC
        [void](WaitFor { try { $q = [FP]::Font($pv); $q.weight -eq 700 -and $q.italic -eq 1 } catch { $false } } 2000)
        $f = [FP]::Font($pv)
        $idx++; Ck ('T39.' + $idx + ' ' + $theme + ': bold and italic update the preview font') ($f.weight -eq 700 -and $f.italic -eq 1) ('weight ' + $f.weight + ', italic ' + $f.italic)
        Press $dlg $IDCANCEL
        [void](Gone $dlg)
    }
}
