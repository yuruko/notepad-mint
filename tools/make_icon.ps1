<#
.SYNOPSIS
    Builds notepad_mint.ico - the real Notepad app icon recoloured from Aero cyan to mint green.

.DESCRIPTION
    1. Extracts every genuine frame of the first RT_GROUP_ICON in notepad.exe
       (kernel32 resource APIs; nothing is upscaled or invented).
    2. Recolours each frame in HSV (alpha untouched, neutral pixels untouched).
    3. Writes a multi-size .ico: 256x256 stored as PNG, smaller sizes as 32bpp DIB.
    4. Re-opens the .ico to verify it, and renders a before/after contact sheet.

    Needs only Windows PowerShell 5.1 + System.Drawing (inline C# through Add-Type).
    Tune the four recolour numbers below (or pass them on the command line) and re-run.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1 -SatGain 1.05 -ValGain 1.08
#>
[CmdletBinding()]
param(
    # ---- recolour tunables -------------------------------------------------------
    # target accent #9df5bd = RGB(157,245,189) = HSV(141.8 deg, 0.359, 0.961)
    [ValidateRange(0.0, 360.0)] [double]$TargetHue        = 141.8,   # every tinted pixel is moved to this hue (degrees)
    [ValidateRange(0.0, 10.0)]  [double]$SatGain          = 1.15,    # S' = min(1, S * SatGain)
    [ValidateRange(0.0, 10.0)]  [double]$ValGain          = 1.12,    # V' = min(1, V * ValGain)
    [ValidateRange(0.0, 1.0)]   [double]$NeutralThreshold = 0.06,    # pixels with S below this are left completely unchanged

    # optional 5th knob: pixels with S in [NeutralThreshold, NeutralThreshold + NeutralFeather) are faded in
    # (smoothstep blend original -> recoloured) instead of switching on at full strength. A hard cutoff leaves a
    # stippled seam where S dithers around the threshold (page edge / page under the cover). 0 = hard cutoff.
    [ValidateRange(0.0, 1.0)]   [double]$NeutralFeather   = 0.06,

    # ---- paths -------------------------------------------------------------------
    [string]$SourceExe    = 'C:\Windows\System32\notepad.exe',
    [string]$OutIco       = (Join-Path $PSScriptRoot '..\assets\notepad_mint.ico'),
    [string]$OutPreview   = (Join-Path $PSScriptRoot '..\assets\icon_preview.png'),
    [string]$TargetAccent = '#9df5bd'     # reference colour: preview swatch + tone report only, not used for the recolour maths
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

trap {
    Write-Host ('ERROR: ' + $_.Exception.ToString()) -ForegroundColor Red
    exit 1
}

# =====================================================================================
#  inline C#  (compiled by Add-Type with the .NET Framework C# 5 compiler: no C# 6+ syntax)
# =====================================================================================
$csharp = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;

namespace NotepadMint
{
    // One image of an icon group: the GRPICONDIRENTRY fields plus the raw RT_ICON payload.
    public class IconEntry
    {
        public int ResId;          // RT_ICON resource id (GRPICONDIRENTRY.nId)
        public int Width;          // real pixel size, read from the payload header
        public int Height;
        public int GroupWidth;     // size as written in the group directory (0 resolved to 256)
        public int GroupHeight;
        public int BitCount;       // real bit depth, read from the payload header
        public int GroupBitCount;  // wBitCount as written in the group directory
        public int GroupBytes;     // dwBytesInRes as written in the group directory
        public int ActualBytes;    // SizeofResource() of the RT_ICON resource
        public bool IsPng;
        public bool Keep;          // highest bit depth available for its size
        public byte[] Data;        // raw RT_ICON payload
        public Bitmap Original;    // decoded 32bpp ARGB frame
        public Bitmap Mint;        // recoloured copy
    }

    public class IconGroup
    {
        public string Name;
        public List<string> AllNames = new List<string>();
        public List<IconEntry> Entries = new List<IconEntry>();
    }

    public class AlphaInfo
    {
        public int Width;
        public int Height;
        public int[] CornerAlpha = new int[4];   // alpha of top-left, top-right, bottom-left, bottom-right
        public int CornersTransparent;           // 0..4 corner pixels with alpha == 0
        public int CornersOpaque;                // 0..4 corner pixels with alpha == 255
        public int Transparent;                  // pixels with alpha == 0
        public int Partial;                      // pixels with 0 < alpha < 255
        public int Opaque;                       // pixels with alpha == 255
        public int CenterAlpha;
    }

    public static class IconTool
    {
        const int RT_ICON = 3;
        const int RT_GROUP_ICON = 14;
        const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
        const uint LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020;

        delegate bool EnumResNameProc(IntPtr hModule, IntPtr lpszType, IntPtr lpszName, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool EnumResourceNamesW(IntPtr hModule, IntPtr lpszType, EnumResNameProc lpEnumFunc, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr FindResourceW(IntPtr hModule, IntPtr lpName, IntPtr lpType);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);

        [DllImport("kernel32.dll")]
        static extern IntPtr LockResource(IntPtr hResData);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

        // ------------------------------------------------------------------------------
        //  step 1 : extract the real frames
        // ------------------------------------------------------------------------------

        // MAKEINTRESOURCE ids have the upper 48 bits clear; anything else is a pointer to a name.
        static bool IsIntId(IntPtr p)
        {
            return (((ulong)p.ToInt64()) >> 16) == 0UL;
        }

        static string NameText(object n)
        {
            if (n is int) return "#" + ((int)n).ToString();
            return "\"" + (string)n + "\"";
        }

        static List<object> EnumNames(IntPtr hMod, int resType)
        {
            List<object> names = new List<object>();
            EnumResNameProc cb = delegate(IntPtr m, IntPtr t, IntPtr n, IntPtr l)
            {
                try
                {
                    if (IsIntId(n)) names.Add((int)n.ToInt64());
                    else names.Add(Marshal.PtrToStringUni(n));
                    return true;
                }
                catch (Exception)
                {
                    return false;   // never let a managed exception unwind through native frames
                }
            };
            EnumResourceNamesW(hMod, (IntPtr)resType, cb, IntPtr.Zero);
            GC.KeepAlive(cb);
            return names;
        }

        // name is either a boxed int (resource id) or a string (resource name)
        static byte[] GetResource(IntPtr hMod, int resType, object name)
        {
            IntPtr namePtr;
            IntPtr toFree = IntPtr.Zero;
            if (name is int)
            {
                namePtr = (IntPtr)((int)name);
            }
            else
            {
                toFree = Marshal.StringToHGlobalUni((string)name);
                namePtr = toFree;
            }
            try
            {
                IntPtr hRes = FindResourceW(hMod, namePtr, (IntPtr)resType);
                if (hRes == IntPtr.Zero) return null;
                uint size = SizeofResource(hMod, hRes);
                IntPtr hGlob = LoadResource(hMod, hRes);
                if (hGlob == IntPtr.Zero) return null;
                IntPtr p = LockResource(hGlob);
                if (p == IntPtr.Zero) return null;
                byte[] buf = new byte[size];
                Marshal.Copy(p, buf, 0, (int)size);
                return buf;
            }
            finally
            {
                if (toFree != IntPtr.Zero) Marshal.FreeHGlobal(toFree);
            }
        }

        static bool IsPngData(byte[] d)
        {
            return d.Length >= 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47
                && d[4] == 0x0D && d[5] == 0x0A && d[6] == 0x1A && d[7] == 0x0A;
        }

        static int BigEndian32(byte[] d, int o)
        {
            return (d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3];
        }

        // size + bit depth as stored in the payload itself (more trustworthy than the group directory)
        static void ReadPayloadHeader(IconEntry e)
        {
            byte[] d = e.Data;
            e.IsPng = IsPngData(d);
            if (e.IsPng)
            {
                if (d.Length < 26) throw new InvalidDataException("RT_ICON #" + e.ResId + ": truncated PNG");
                e.Width = BigEndian32(d, 16);          // IHDR width
                e.Height = BigEndian32(d, 20);         // IHDR height
                int depth = d[24];
                int colorType = d[25];
                int channels = (colorType == 0) ? 1 : (colorType == 2) ? 3 : (colorType == 3) ? 1 : (colorType == 4) ? 2 : 4;
                e.BitCount = depth * channels;
            }
            else
            {
                if (d.Length < 40 || BitConverter.ToUInt32(d, 0) != 40)
                    throw new InvalidDataException("RT_ICON #" + e.ResId + ": not a PNG and not a BITMAPINFOHEADER DIB");
                e.Width = BitConverter.ToInt32(d, 4);
                e.Height = BitConverter.ToInt32(d, 8) / 2;   // DIB height is doubled (XOR + AND mask)
                e.BitCount = BitConverter.ToUInt16(d, 14);
            }
        }

        // Reads the FIRST RT_GROUP_ICON (enumeration order) of a module and every RT_ICON it references.
        public static IconGroup ReadFirstIconGroup(string path)
        {
            IntPtr hMod = LoadLibraryExW(path, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            if (hMod == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibraryExW failed for " + path);
            try
            {
                List<object> names = EnumNames(hMod, RT_GROUP_ICON);
                if (names.Count == 0)
                    throw new InvalidOperationException("no RT_GROUP_ICON resource in " + path);

                IconGroup grp = new IconGroup();
                foreach (object n in names) grp.AllNames.Add(NameText(n));
                grp.Name = NameText(names[0]);

                byte[] dir = GetResource(hMod, RT_GROUP_ICON, names[0]);
                if (dir == null || dir.Length < 6)
                    throw new InvalidOperationException("cannot read RT_GROUP_ICON " + grp.Name);

                // GRPICONDIR: idReserved u16, idType u16, idCount u16
                int idType = BitConverter.ToUInt16(dir, 2);
                int count = BitConverter.ToUInt16(dir, 4);
                if (idType != 1 || dir.Length < 6 + count * 14)
                    throw new InvalidDataException("malformed GRPICONDIR in " + grp.Name);

                // GRPICONDIRENTRY (14 bytes, packed): bWidth u8, bHeight u8, bColorCount u8, bReserved u8,
                //                                     wPlanes u16, wBitCount u16, dwBytesInRes u32, nId u16
                for (int i = 0; i < count; i++)
                {
                    int o = 6 + i * 14;
                    IconEntry e = new IconEntry();
                    e.GroupWidth = (dir[o] == 0) ? 256 : dir[o];
                    e.GroupHeight = (dir[o + 1] == 0) ? 256 : dir[o + 1];
                    e.GroupBitCount = BitConverter.ToUInt16(dir, o + 6);
                    e.GroupBytes = (int)BitConverter.ToUInt32(dir, o + 8);
                    e.ResId = BitConverter.ToUInt16(dir, o + 12);

                    e.Data = GetResource(hMod, RT_ICON, e.ResId);
                    if (e.Data == null)
                        throw new InvalidOperationException("RT_ICON #" + e.ResId + " not found");
                    e.ActualBytes = e.Data.Length;
                    ReadPayloadHeader(e);
                    grp.Entries.Add(e);
                }
                return grp;
            }
            finally
            {
                FreeLibrary(hMod);
            }
        }

        // Marks, per size, the entry with the highest bit depth (Keep = true) and returns those, ascending by size.
        public static List<IconEntry> SelectFrames(IconGroup g)
        {
            List<IconEntry> kept = new List<IconEntry>();
            foreach (IconEntry e in g.Entries)
            {
                IconEntry best = null;
                foreach (IconEntry o in g.Entries)
                {
                    if (o.Width == e.Width && o.Height == e.Height && (best == null || o.BitCount > best.BitCount))
                        best = o;
                }
                e.Keep = object.ReferenceEquals(best, e);
                if (e.Keep) kept.Add(e);
            }
            kept.Sort(delegate(IconEntry a, IconEntry b)
            {
                int c = a.Width.CompareTo(b.Width);
                return (c != 0) ? c : a.Height.CompareTo(b.Height);
            });
            return kept;
        }

        static void PutU16(byte[] b, int o, int v)
        {
            b[o] = (byte)(v & 0xFF);
            b[o + 1] = (byte)((v >> 8) & 0xFF);
        }

        static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)(v & 0xFF);
            b[o + 1] = (byte)((v >> 8) & 0xFF);
            b[o + 2] = (byte)((v >> 16) & 0xFF);
            b[o + 3] = (byte)((v >> 24) & 0xFF);
        }

        // DIB frames: wrap the raw RT_ICON payload into a one-image .ico and let System.Drawing decode it
        //             (correct per-pixel alpha for 32bpp frames).
        // PNG frames: the payload already IS a PNG file, so decode it directly.  (Icon.ToBitmap() cannot be used here:
        //             powershell.exe runs System.Drawing in .NET compat mode, where it ignores PNG frames and
        //             mis-reads them as DIBs.)
        public static Bitmap DecodeFrame(IconEntry e)
        {
            if (e.IsPng)
            {
                using (MemoryStream pms = new MemoryStream(e.Data))
                using (Bitmap png = new Bitmap(pms))
                {
                    if (png.Width != e.Width || png.Height != e.Height)
                        throw new InvalidOperationException("PNG frame is " + png.Width + "x" + png.Height
                            + " but expected " + e.Width + "x" + e.Height + " (RT_ICON #" + e.ResId + ")");
                    return CloneBitmap(png);   // copy before the stream goes away; forces Format32bppArgb, non-premultiplied
                }
            }

            byte[] ico = new byte[22 + e.Data.Length];
            PutU16(ico, 2, 1);                                    // ICONDIR.idType = 1 (icon)
            PutU16(ico, 4, 1);                                    // ICONDIR.idCount = 1
            ico[6] = (byte)((e.Width >= 256) ? 0 : e.Width);      // ICONDIRENTRY.bWidth
            ico[7] = (byte)((e.Height >= 256) ? 0 : e.Height);    // ICONDIRENTRY.bHeight
            PutU16(ico, 10, 1);                                   // wPlanes
            PutU16(ico, 12, e.BitCount);                          // wBitCount
            PutU32(ico, 14, (uint)e.Data.Length);                 // dwBytesInRes
            PutU32(ico, 18, 22);                                  // dwImageOffset (6 + 16)
            Buffer.BlockCopy(e.Data, 0, ico, 22, e.Data.Length);

            using (MemoryStream ms = new MemoryStream(ico))
            using (Icon icon = new Icon(ms, e.Width, e.Height))   // ask for the exact size so nothing is rescaled
            using (Bitmap raw = icon.ToBitmap())
            {
                if (raw.Width != e.Width || raw.Height != e.Height)
                    throw new InvalidOperationException("decoded " + raw.Width + "x" + raw.Height
                        + " but expected " + e.Width + "x" + e.Height + " (RT_ICON #" + e.ResId + ")");
                return CloneBitmap(raw);     // independent, non-premultiplied Format32bppArgb copy
            }
        }

        public static AlphaInfo MeasureAlpha(Bitmap bmp)
        {
            int w = bmp.Width, h = bmp.Height;
            byte[] px = GetBgra(bmp);
            AlphaInfo a = new AlphaInfo();
            a.Width = w;
            a.Height = h;
            for (int i = 3; i < px.Length; i += 4)
            {
                if (px[i] == 0) a.Transparent++;
                else if (px[i] == 255) a.Opaque++;
                else a.Partial++;
            }
            int[] cx = new int[] { 0, w - 1, 0, w - 1 };
            int[] cy = new int[] { 0, 0, h - 1, h - 1 };
            for (int k = 0; k < 4; k++)
            {
                int ca = px[(cy[k] * w + cx[k]) * 4 + 3];
                a.CornerAlpha[k] = ca;
                if (ca == 0) a.CornersTransparent++;
                if (ca == 255) a.CornersOpaque++;
            }
            a.CenterAlpha = px[((h / 2) * w + (w / 2)) * 4 + 3];
            return a;
        }

        // ------------------------------------------------------------------------------
        //  pixel access: always Format32bppArgb, non-premultiplied, rows top-down, BGRA byte order
        // ------------------------------------------------------------------------------

        public static byte[] GetBgra(Bitmap bmp)
        {
            int w = bmp.Width, h = bmp.Height;
            byte[] px = new byte[w * h * 4];
            BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                for (int y = 0; y < h; y++)
                    Marshal.Copy(new IntPtr(bd.Scan0.ToInt64() + (long)y * bd.Stride), px, y * w * 4, w * 4);
            }
            finally
            {
                bmp.UnlockBits(bd);
            }
            return px;
        }

        static void SetBgra(Bitmap bmp, byte[] px)
        {
            int w = bmp.Width, h = bmp.Height;
            BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                for (int y = 0; y < h; y++)
                    Marshal.Copy(px, y * w * 4, new IntPtr(bd.Scan0.ToInt64() + (long)y * bd.Stride), w * 4);
            }
            finally
            {
                bmp.UnlockBits(bd);
            }
        }

        static Bitmap FromBgra(byte[] px, int w, int h)
        {
            Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            SetBgra(bmp, px);
            return bmp;
        }

        public static Bitmap CloneBitmap(Bitmap src)
        {
            return FromBgra(GetBgra(src), src.Width, src.Height);
        }

        // [0] = pixels whose alpha differs, [1] = pixels (alpha > 0) whose RGB differs
        public static int[] Diff(Bitmap a, Bitmap b)
        {
            if (a.Width != b.Width || a.Height != b.Height) return new int[] { -1, -1 };
            return DiffBgra(GetBgra(a), GetBgra(b));
        }

        // same comparison on two raw BGRA buffers (top-down, straight alpha)
        public static int[] DiffBgra(byte[] pa, byte[] pb)
        {
            if (pa.Length != pb.Length) return new int[] { -1, -1 };
            int alphaDiff = 0, rgbDiff = 0;
            for (int i = 0; i < pa.Length; i += 4)
            {
                if (pa[i + 3] != pb[i + 3]) alphaDiff++;
                else if (pa[i + 3] != 0 && (pa[i] != pb[i] || pa[i + 1] != pb[i + 1] || pa[i + 2] != pb[i + 2])) rgbDiff++;
            }
            return new int[] { alphaDiff, rgbDiff };
        }

        // ------------------------------------------------------------------------------
        //  step 2 : recolour
        // ------------------------------------------------------------------------------

        static void RgbToHsv(int r, int g, int b, out double h, out double s, out double v)
        {
            double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
            double max = Math.Max(rf, Math.Max(gf, bf));
            double min = Math.Min(rf, Math.Min(gf, bf));
            double d = max - min;
            v = max;
            s = (max <= 0.0) ? 0.0 : d / max;
            if (d <= 0.0) h = 0.0;
            else if (max == rf) h = 60.0 * (((gf - bf) / d) % 6.0);
            else if (max == gf) h = 60.0 * (((bf - rf) / d) + 2.0);
            else h = 60.0 * (((rf - gf) / d) + 4.0);
            if (h < 0.0) h += 360.0;
        }

        static int ToByte(double v)
        {
            int i = (int)Math.Floor(v * 255.0 + 0.5);
            return (i < 0) ? 0 : (i > 255) ? 255 : i;
        }

        static void HsvToRgb(double h, double s, double v, out int r, out int g, out int b)
        {
            h = h % 360.0;
            if (h < 0.0) h += 360.0;
            double c = v * s;
            double hp = h / 60.0;
            double x = c * (1.0 - Math.Abs((hp % 2.0) - 1.0));
            double m = v - c;
            double rr, gg, bb;
            if (hp < 1.0) { rr = c; gg = x; bb = 0.0; }
            else if (hp < 2.0) { rr = x; gg = c; bb = 0.0; }
            else if (hp < 3.0) { rr = 0.0; gg = c; bb = x; }
            else if (hp < 4.0) { rr = 0.0; gg = x; bb = c; }
            else if (hp < 5.0) { rr = x; gg = 0.0; bb = c; }
            else { rr = c; gg = 0.0; bb = x; }
            r = ToByte(rr + m);
            g = ToByte(gg + m);
            b = ToByte(bb + m);
        }

        // In-place recolour of a Format32bppArgb bitmap (un-premultiplied RGB). Alpha is never touched.
        //   S <  neutral                 : pixel left completely unchanged
        //   S >= neutral + feather       : H' = targetHue, S' = min(1, S*satGain), V' = min(1, V*valGain)
        //   neutral <= S < neutral+feather: smoothstep blend between the original and that result (feather = 0: no blend)
        // Returns { pixels recoloured (incl. feathered ones), pixels that were only partly recoloured }.
        public static int[] Recolor(Bitmap bmp, double targetHue, double satGain, double valGain, double neutral, double feather)
        {
            byte[] px = GetBgra(bmp);
            int changed = 0, feathered = 0;
            for (int i = 0; i < px.Length; i += 4)
            {
                if (px[i + 3] == 0) continue;                 // fully transparent: nothing to colour
                double h, s, v;
                RgbToHsv(px[i + 2], px[i + 1], px[i], out h, out s, out v);
                if (s < neutral || s <= 0.0) continue;        // neutral (or hue-less grey) pixel: leave untouched

                int r, g, b;
                HsvToRgb(targetHue, Math.Min(1.0, s * satGain), Math.Min(1.0, v * valGain), out r, out g, out b);

                if (feather > 0.0 && s < neutral + feather)
                {
                    double t = (s - neutral) / feather;
                    double w = t * t * (3.0 - 2.0 * t);       // smoothstep: 0 at the threshold, 1 at threshold + feather
                    r = (int)Math.Floor(px[i + 2] + (r - px[i + 2]) * w + 0.5);
                    g = (int)Math.Floor(px[i + 1] + (g - px[i + 1]) * w + 0.5);
                    b = (int)Math.Floor(px[i] + (b - px[i]) * w + 0.5);
                    feathered++;
                }

                px[i] = (byte)b;
                px[i + 1] = (byte)g;
                px[i + 2] = (byte)r;
                changed++;
            }
            SetBgra(bmp, px);
            return new int[] { changed, feathered };
        }

        // ------------------------------------------------------------------------------
        //  step 3 : write the .ico
        // ------------------------------------------------------------------------------

        // 32bpp DIB frame: BITMAPINFOHEADER, BGRA pixels bottom-up (non-premultiplied), then an all-zero AND mask.
        static byte[] BuildDib(Bitmap bmp)
        {
            int w = bmp.Width, h = bmp.Height;
            byte[] px = GetBgra(bmp);
            int xorBytes = w * h * 4;
            int maskStride = ((w + 31) / 32) * 4;
            byte[] d = new byte[40 + xorBytes + maskStride * h];
            PutU32(d, 0, 40);                     // biSize
            PutU32(d, 4, (uint)w);                // biWidth
            PutU32(d, 8, (uint)(h * 2));          // biHeight (XOR + AND)
            PutU16(d, 12, 1);                     // biPlanes
            PutU16(d, 14, 32);                    // biBitCount
            PutU32(d, 16, 0);                     // biCompression = BI_RGB
            PutU32(d, 20, (uint)xorBytes);        // biSizeImage
            for (int y = 0; y < h; y++)           // bottom-up rows
                Buffer.BlockCopy(px, (h - 1 - y) * w * 4, d, 40 + y * w * 4, w * 4);
            return d;                             // AND mask (maskStride * h bytes) stays zero
        }

        // frames must already be sorted ascending by size
        public static void WriteIco(string path, List<Bitmap> frames)
        {
            int n = frames.Count;
            List<byte[]> payloads = new List<byte[]>();
            foreach (Bitmap bmp in frames)
            {
                if (bmp.Width >= 256 || bmp.Height >= 256)
                {
                    using (MemoryStream ms = new MemoryStream())
                    {
                        bmp.Save(ms, ImageFormat.Png);
                        payloads.Add(ms.ToArray());
                    }
                }
                else
                {
                    payloads.Add(BuildDib(bmp));
                }
            }

            using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (BinaryWriter bw = new BinaryWriter(fs))
            {
                bw.Write((ushort)0);              // ICONDIR.idReserved
                bw.Write((ushort)1);              // ICONDIR.idType = icon
                bw.Write((ushort)n);              // ICONDIR.idCount
                int offset = 6 + 16 * n;
                for (int i = 0; i < n; i++)
                {
                    int w = frames[i].Width, h = frames[i].Height;
                    bw.Write((byte)((w >= 256) ? 0 : w));   // bWidth  (0 means 256)
                    bw.Write((byte)((h >= 256) ? 0 : h));   // bHeight (0 means 256)
                    bw.Write((byte)0);                      // bColorCount
                    bw.Write((byte)0);                      // bReserved
                    bw.Write((ushort)1);                    // wPlanes
                    bw.Write((ushort)32);                   // wBitCount
                    bw.Write((uint)payloads[i].Length);     // dwBytesInRes
                    bw.Write((uint)offset);                 // dwImageOffset
                    offset += payloads[i].Length;
                }
                for (int i = 0; i < n; i++) bw.Write(payloads[i]);
            }
        }

        // ------------------------------------------------------------------------------
        //  step 4 : contact sheet
        // ------------------------------------------------------------------------------

        static int ScaleFor(IconEntry f)
        {
            return (f.Width >= 256) ? 1 : 4;      // 256 shown 1:1, everything smaller enlarged 4x
        }

        static Bitmap UpscaleNearest(Bitmap src, int k)
        {
            int w = src.Width, h = src.Height, dw = w * k;
            byte[] s = GetBgra(src);
            byte[] d = new byte[dw * h * k * 4];
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int si = (y * w + x) * 4;
                    for (int dy = 0; dy < k; dy++)
                    {
                        for (int dx = 0; dx < k; dx++)
                        {
                            int di = ((y * k + dy) * dw + (x * k + dx)) * 4;
                            d[di] = s[si];
                            d[di + 1] = s[si + 1];
                            d[di + 2] = s[si + 2];
                            d[di + 3] = s[si + 3];
                        }
                    }
                }
            }
            return FromBgra(d, dw, h * k);
        }

        // top row = Original frames, bottom row = Mint frames, on a #161418 background
        public static void BuildPreview(string outPath, List<IconEntry> frames, string topCaption, string bottomCaption,
                                        int swR, int swG, int swB, string swLabel)
        {
            const int Margin = 16, Gap = 18, CapH = 24, LabH = 18, RowH = 256, MinCol = 84;
            int n = frames.Count;
            int[] colW = new int[n];
            int totalW = Margin;
            for (int i = 0; i < n; i++)
            {
                colW[i] = Math.Max(MinCol, ScaleFor(frames[i]) * frames[i].Width);
                totalW += colW[i] + Gap;
            }
            totalW = totalW - Gap + Margin;
            int blockH = CapH + LabH + RowH;
            int totalH = Margin + blockH + Gap + blockH + Margin;

            using (Bitmap sheet = new Bitmap(totalW, totalH, PixelFormat.Format32bppArgb))
            {
                using (Graphics g = Graphics.FromImage(sheet))
                using (Font capFont = new Font("Segoe UI", 10f, FontStyle.Bold, GraphicsUnit.Point))
                using (Font labFont = new Font("Segoe UI", 8f, FontStyle.Regular, GraphicsUnit.Point))
                using (Brush capBrush = new SolidBrush(Color.FromArgb(235, 235, 240)))
                using (Brush labBrush = new SolidBrush(Color.FromArgb(150, 150, 160)))
                using (Brush swBrush = new SolidBrush(Color.FromArgb(swR, swG, swB)))
                {
                    g.Clear(Color.FromArgb(0x16, 0x14, 0x18));
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    g.InterpolationMode = InterpolationMode.NearestNeighbor;
                    g.PixelOffsetMode = PixelOffsetMode.Half;
                    g.SmoothingMode = SmoothingMode.None;

                    for (int row = 0; row < 2; row++)
                    {
                        int y0 = Margin + row * (blockH + Gap);
                        string cap = (row == 0) ? topCaption : bottomCaption;
                        g.DrawString(cap, capFont, capBrush, Margin, y0);
                        if (row == 1)
                        {
                            SizeF sz = g.MeasureString(cap, capFont);
                            int sx = Margin + (int)sz.Width + 8;
                            g.FillRectangle(swBrush, sx, y0 + 2, 36, 18);
                            g.DrawString(swLabel, labFont, labBrush, sx + 42, y0 + 5);
                        }

                        int x = Margin;
                        for (int i = 0; i < n; i++)
                        {
                            IconEntry f = frames[i];
                            int k = ScaleFor(f);
                            Bitmap src = (row == 0) ? f.Original : f.Mint;
                            Bitmap shown = (k == 1) ? src : UpscaleNearest(src, k);
                            int dw = f.Width * k, dh = f.Height * k;
                            string lab = f.Width + "x" + f.Height + ((k > 1) ? "  (x" + k + ")" : "");
                            g.DrawString(lab, labFont, labBrush, x, y0 + CapH);
                            Rectangle dest = new Rectangle(x + (colW[i] - dw) / 2, y0 + CapH + LabH + (RowH - dh) / 2, dw, dh);
                            g.DrawImage(shown, dest, 0, 0, dw, dh, GraphicsUnit.Pixel);
                            if (k != 1) shown.Dispose();
                            x += colW[i] + Gap;
                        }
                    }
                }
                sheet.Save(outPath, ImageFormat.Png);
            }
        }
    }
}
'@

Add-Type -AssemblyName System.Drawing
if (-not ('NotepadMint.IconTool' -as [type])) {
    Add-Type -TypeDefinition $csharp -ReferencedAssemblies System.Drawing
}

# =====================================================================================
#  helpers
# =====================================================================================
function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host $Text -ForegroundColor Cyan
}

$script:failures = 0
function Test-Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) {
        Write-Host ('  pass  {0}  {1}' -f $Name, $Detail)
    } else {
        Write-Host ('  FAIL  {0}  {1}' -f $Name, $Detail) -ForegroundColor Red
        $script:failures++
    }
}

foreach ($p in @($OutIco, $OutPreview)) {
    $dir = Split-Path -Parent $p
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
}

Write-Host ('notepad mint icon builder: target hue {0}, saturation x{1}, value x{2}, neutral threshold S < {3} (feather {4})' -f `
    $TargetHue, $SatGain, $ValGain, $NeutralThreshold, $NeutralFeather)

# =====================================================================================
#  step 1 : extract the real frames
# =====================================================================================
Write-Step "[1/4] extracting frames from $SourceExe"

$grp = [NotepadMint.IconTool]::ReadFirstIconGroup($SourceExe)
Write-Host ('RT_GROUP_ICON resources in enumeration order: {0}' -f ($grp.AllNames -join ', '))
Write-Host ('using the first one: {0} ({1} entries)' -f $grp.Name, $grp.Entries.Count)

$frames = [NotepadMint.IconTool]::SelectFrames($grp)

$rows = foreach ($e in $grp.Entries) {
    [pscustomobject]@{
        ResId  = $e.ResId
        Size   = ('{0}x{1}' -f $e.Width, $e.Height)
        Bits   = $e.BitCount
        Bytes  = $e.ActualBytes
        Format = $(if ($e.IsPng) { 'PNG' } else { 'DIB' })
        Status = $(if ($e.Keep) { 'kept (highest bit depth for this size)' } else { 'skipped (lower bit depth)' })
    }
}
($rows | Format-Table -AutoSize | Out-String -Width 200).TrimEnd() | Write-Host

foreach ($e in $grp.Entries) {
    if ($e.Width -ne $e.GroupWidth -or $e.Height -ne $e.GroupHeight) {
        Write-Warning ('RT_ICON #{0}: group directory says {1}x{2}, payload is {3}x{4}' -f $e.ResId, $e.GroupWidth, $e.GroupHeight, $e.Width, $e.Height)
    }
    if ($e.BitCount -ne $e.GroupBitCount) {
        Write-Warning ('RT_ICON #{0}: group directory says {1} bpp, payload is {2} bpp' -f $e.ResId, $e.GroupBitCount, $e.BitCount)
    }
    if ($e.ActualBytes -ne $e.GroupBytes) {
        Write-Warning ('RT_ICON #{0}: group directory says {1} bytes, resource is {2} bytes' -f $e.ResId, $e.GroupBytes, $e.ActualBytes)
    }
}

Write-Host ''
Write-Host 'decoding kept frames (PNG: direct; DIB: one-image .ico in memory -> System.Drawing.Icon -> Bitmap):'
foreach ($e in $frames) {
    $e.Original = [NotepadMint.IconTool]::DecodeFrame($e)
    $a = [NotepadMint.IconTool]::MeasureAlpha($e.Original)
    # real per-pixel alpha = soft (partial) alpha present, transparent background, no opaque corner.
    # (a corner may legitimately hold a faint pixel of artwork, e.g. the 16x16 gold edge, so require >= 3 clear corners)
    $ok = ($e.Original.PixelFormat -eq [System.Drawing.Imaging.PixelFormat]::Format32bppArgb) -and
          ($a.CornersTransparent -ge 3) -and ($a.CornersOpaque -eq 0) -and
          ($a.Transparent -gt 0) -and ($a.Partial -gt 0) -and ($a.Opaque -gt 0)
    Test-Check ('{0}x{1} per-pixel alpha' -f $e.Width, $e.Height) $ok `
        ('corner alpha {0}; alpha 0 / partial / 255 pixels: {1} / {2} / {3}' -f ($a.CornerAlpha -join ','), $a.Transparent, $a.Partial, $a.Opaque)
}

# =====================================================================================
#  step 2 : recolour
# =====================================================================================
Write-Step ('[2/4] recolouring: H -> {0}, S x {1}, V x {2}, pixels with S < {3} left unchanged, feather {4}' -f $TargetHue, $SatGain, $ValGain, $NeutralThreshold, $NeutralFeather)

foreach ($e in $frames) {
    $e.Mint = [NotepadMint.IconTool]::CloneBitmap($e.Original)
    $res = [NotepadMint.IconTool]::Recolor($e.Mint, $TargetHue, $SatGain, $ValGain, $NeutralThreshold, $NeutralFeather)
    $d = [NotepadMint.IconTool]::Diff($e.Original, $e.Mint)
    $a = [NotepadMint.IconTool]::MeasureAlpha($e.Original)
    $visible = $a.Partial + $a.Opaque
    Test-Check ('{0}x{1} alpha preserved' -f $e.Width, $e.Height) ($d[0] -eq 0) `
        ('{0} of {1} visible pixels recoloured ({2} of them feathered), {3} left neutral' -f $res[0], $visible, $res[1], ($visible - $res[0]))
}

# =====================================================================================
#  step 3 : write + verify the .ico
# =====================================================================================
Write-Step "[3/4] writing $OutIco"

$mintList = New-Object 'System.Collections.Generic.List[System.Drawing.Bitmap]'
foreach ($e in $frames) { $mintList.Add($e.Mint) }
[NotepadMint.IconTool]::WriteIco($OutIco, $mintList)

$bytes = [System.IO.File]::ReadAllBytes($OutIco)
$count = [BitConverter]::ToUInt16($bytes, 4)
Test-Check 'ico header 00 00 01 00' (($bytes[0] -eq 0) -and ($bytes[1] -eq 0) -and ($bytes[2] -eq 1) -and ($bytes[3] -eq 0)) `
    ('first bytes: {0:X2} {1:X2} {2:X2} {3:X2}' -f $bytes[0], $bytes[1], $bytes[2], $bytes[3])
Test-Check 'ico entry count' ($count -eq $frames.Count) ('{0} entries, expected {1}' -f $count, $frames.Count)

$pngSig = @(0x89, 0x50, 0x4E, 0x47)
for ($i = 0; $i -lt $frames.Count; $i++) {
    $e = $frames[$i]
    $o = 6 + 16 * $i
    $bw = $bytes[$o]; $bh = $bytes[$o + 1]
    $len = [BitConverter]::ToUInt32($bytes, $o + 8)
    $off = [BitConverter]::ToUInt32($bytes, $o + 12)
    $wantB = $(if ($e.Width -ge 256) { 0 } else { $e.Width })
    $isPng = ($bytes[$off] -eq $pngSig[0]) -and ($bytes[$off + 1] -eq $pngSig[1]) -and ($bytes[$off + 2] -eq $pngSig[2]) -and ($bytes[$off + 3] -eq $pngSig[3])
    $isDib = ($bytes[$off] -eq 40) -and ($bytes[$off + 1] -eq 0) -and ($bytes[$off + 2] -eq 0) -and ($bytes[$off + 3] -eq 0)
    $fmtOk = $(if ($e.Width -ge 256) { $isPng } else { $isDib })
    $inRange = (($off + $len) -le $bytes.Length)
    Test-Check ('entry {0}: {1}x{2} directory' -f $i, $e.Width, $e.Height) `
        (($bw -eq $wantB) -and ($bh -eq $wantB) -and $fmtOk -and $inRange) `
        ('bWidth/bHeight byte {0}, {1} bytes @ {2}, stored as {3}' -f $bw, $len, $off, $(if ($isPng) { 'PNG' } else { 'DIB' }))
}

Write-Host ''
Write-Host 're-opening with System.Drawing.Icon(path, size) and comparing every pixel:'
foreach ($e in $frames) {
    if ($e.Width -ge 256) {
        # System.Drawing.Icon matches the requested size against the raw bWidth byte, which is 0 for 256, so it
        # always prefers the 48x48 frame; the 256x256 frame is verified through WIC below instead.
        Write-Host ('  note  {0}x{1}: System.Drawing.Icon cannot select the 256 frame (bWidth 0 is read as width 0, 48x48 wins) - checked via WIC below' -f $e.Width, $e.Height)
        continue
    }
    $ic = New-Object System.Drawing.Icon($OutIco, $e.Width, $e.Height)
    try {
        $bm = $ic.ToBitmap()
        try {
            $cx = [int]($bm.Width / 2); $cy = [int]($bm.Height / 2)
            $centerA = $bm.GetPixel($cx, $cy).A
            $d = [NotepadMint.IconTool]::Diff($e.Mint, $bm)
            Test-Check ('{0}x{1} reload' -f $e.Width, $e.Height) `
                (($ic.Width -eq $e.Width) -and ($ic.Height -eq $e.Height) -and ($centerA -eq 255) -and ($d[0] -eq 0) -and ($d[1] -eq 0)) `
                ('loaded {0}x{1}, centre ({2},{3}) alpha {4}, pixel mismatches alpha/rgb: {5}/{6}' -f $ic.Width, $ic.Height, $cx, $cy, $centerA, $d[0], $d[1])
        } finally { $bm.Dispose() }
    } finally { $ic.Dispose() }
}

Write-Host ''
Write-Host 'cross-checking every frame, 256x256 included, with WIC (the ICO decoder Windows itself uses):'
Add-Type -AssemblyName PresentationCore, WindowsBase
$dec = New-Object System.Windows.Media.Imaging.IconBitmapDecoder(
    (New-Object System.Uri($OutIco)),
    [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
    [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
Test-Check 'WIC frame count' ($dec.Frames.Count -eq $frames.Count) ('{0} frames, expected {1}' -f $dec.Frames.Count, $frames.Count)
foreach ($e in $frames) {
    $wf = $dec.Frames | Where-Object { $_.PixelWidth -eq $e.Width -and $_.PixelHeight -eq $e.Height } | Select-Object -First 1
    if ($null -eq $wf) {
        Test-Check ('{0}x{1} WIC' -f $e.Width, $e.Height) $false 'frame not found'
        continue
    }
    $src = $wf
    if ($src.Format -ne [System.Windows.Media.PixelFormats]::Bgra32) {
        $src = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap($src, [System.Windows.Media.PixelFormats]::Bgra32, $null, 0.0)
    }
    $stride = $src.PixelWidth * 4
    $buf = New-Object byte[] ($stride * $src.PixelHeight)
    $src.CopyPixels($buf, $stride, 0)
    $d = [NotepadMint.IconTool]::DiffBgra([NotepadMint.IconTool]::GetBgra($e.Mint), $buf)
    $cx = [int]($e.Width / 2); $cy = [int]($e.Height / 2)
    $centerA = $buf[($cy * $e.Width + $cx) * 4 + 3]
    Test-Check ('{0}x{1} WIC' -f $e.Width, $e.Height) (($centerA -eq 255) -and ($d[0] -eq 0) -and ($d[1] -eq 0)) `
        ('native format {0}, centre alpha {1}, pixel mismatches alpha/rgb: {2}/{3}' -f $wf.Format, $centerA, $d[0], $d[1])
}

# =====================================================================================
#  step 4 : contact sheet
# =====================================================================================
Write-Step "[4/4] writing $OutPreview"

$accent = [System.Drawing.ColorTranslator]::FromHtml($TargetAccent)
$top = 'ORIGINAL  -  notepad.exe, RT_GROUP_ICON {0}' -f $grp.Name
$bottom = 'RECOLORED  -  hue {0}, sat x{1}, val x{2}, neutral S < {3} (feather {4})' -f $TargetHue, $SatGain, $ValGain, $NeutralThreshold, $NeutralFeather
[NotepadMint.IconTool]::BuildPreview($OutPreview, $frames, $top, $bottom, $accent.R, $accent.G, $accent.B, ('target ' + $TargetAccent))

# =====================================================================================
#  summary
# =====================================================================================
Write-Step 'summary'
Write-Host ('  parameters : TargetHue={0}  SatGain={1}  ValGain={2}  NeutralThreshold={3}  NeutralFeather={4}' -f $TargetHue, $SatGain, $ValGain, $NeutralThreshold, $NeutralFeather)
Write-Host ('  frames     : {0}' -f (($frames | ForEach-Object { '{0}x{1}' -f $_.Width, $_.Height }) -join ', '))
Write-Host ('  {0}  {1} bytes' -f $OutIco, (Get-Item -LiteralPath $OutIco).Length)
Write-Host ('  {0}  {1} bytes' -f $OutPreview, (Get-Item -LiteralPath $OutPreview).Length)

if ($script:failures -gt 0) {
    Write-Host ('{0} check(s) FAILED' -f $script:failures) -ForegroundColor Red
    exit 1
}
Write-Host 'all checks passed' -ForegroundColor Green
exit 0
