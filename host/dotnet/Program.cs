using System.Buffers.Binary;
using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;

namespace Suf2;

internal static class Program
{
    internal const int MaxImageSize = 16 * 1024 * 1024;
    internal const ulong AddressLimit = 1UL << 32;

    private const string Help = """
        SUF2 signed-only packer (draft format version 1)
        Usage:
          suf2 pack --input firmware.{bin,hex,elf} --output firmware.suf2
            --key signing-private.pem --product-id <32 hex> --base <0x...>
            [--end <exclusive address>] [--fill <byte>]

        All addresses are decimal or 0x-prefixed hexadecimal. The base is required.
        Fill is a decimal or 0x-prefixed byte (0..255).
        Sparse images require BOTH --end and --fill; no loaded bytes may be discarded.
        Images are limited to 16 MiB. Only unencrypted PKCS#8 P-256 private PEM keys
        are accepted. Output is created exclusively; existing files are never replaced.
        No encryption options are supported. Use --help to show this text.
        """;

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 1 && args[0] is "--help" or "-h" ||
                args.Length == 2 && args[0] == "pack" && args[1] is "--help" or "-h")
            {
                Console.WriteLine(Help);
                return 0;
            }
            var options = Options.Parse(args);
            var image = ImageReader.Read(options);
            using var key = ReadKey(options.Key);
            var manifest = PackageWriter.Manifest(image, options, key);
            PackageWriter.Write(options.Output, image, manifest);
            Console.WriteLine($"Created signed-only package: {image.Length} image bytes.");
            return 0;
        }
        catch (Exception ex) when (ex is ArgumentException or FormatException or OverflowException
            or IOException or UnauthorizedAccessException or CryptographicException)
        {
            // Crypto provider exceptions can contain details of the imported key.
            var message = ex is CryptographicException
                ? "Invalid signing key or signing operation failed."
                : ex is IOException or UnauthorizedAccessException
                    ? "Cannot read input/key or create output (existing outputs are not overwritten)."
                    : ex.Message;
            Console.Error.WriteLine($"Error: {message}");
            return 1;
        }
    }

    private static ECDsa ReadKey(string path)
    {
        using var file = File.OpenRead(path);
        if (file.Length > 16 * 1024)
            throw new ArgumentException("Signing key PEM is too large.");
        using var reader = new StreamReader(file, Encoding.ASCII, false);
        var pem = reader.ReadToEnd().Trim();
        const string begin = "-----BEGIN PRIVATE KEY-----";
        const string end = "-----END PRIVATE KEY-----";
        if (!pem.StartsWith(begin, StringComparison.Ordinal) ||
            !pem.EndsWith(end, StringComparison.Ordinal))
            throw new ArgumentException("Key must be one unencrypted PKCS#8 PRIVATE KEY PEM.");
        byte[] der;
        try
        {
            der = Convert.FromBase64String(pem[begin.Length..^end.Length]);
        }
        catch (FormatException)
        {
            throw new ArgumentException("Invalid PKCS#8 private PEM.");
        }
        var key = ECDsa.Create();
        try
        {
            key.ImportPkcs8PrivateKey(der, out var bytesRead);
            if (bytesRead != der.Length ||
                key.ExportParameters(false).Curve.Oid.Value != "1.2.840.10045.3.1.7")
                throw new ArgumentException("Key must use the named P-256 curve.");
            return key;
        }
        catch
        {
            key.Dispose();
            throw;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(der);
        }
    }
}

internal sealed record Options(string Input, string Output, string Key, byte[] ProductId,
    uint Base, ulong? End, byte? Fill)
{
    internal static Options Parse(string[] args)
    {
        if (args.Length == 0 || args[0] != "pack")
            throw new ArgumentException("Expected 'pack'; use --help for usage.");
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        var allowed = new HashSet<string>
            { "--input", "--output", "--key", "--product-id", "--base", "--end", "--fill" };
        for (var i = 1; i < args.Length; i += 2)
        {
            if (!allowed.Contains(args[i]) || i + 1 == args.Length ||
                args[i + 1].StartsWith("--", StringComparison.Ordinal) ||
                !values.TryAdd(args[i], args[i + 1]))
                throw new ArgumentException("Unknown, duplicate or incomplete option; use --help.");
        }
        string Required(string name) => values.TryGetValue(name, out var value) && value.Length > 0
            ? value : throw new ArgumentException($"Missing {name}.");
        var input = Required("--input");
        var output = Required("--output");
        var key = Required("--key");
        var productText = Required("--product-id");
        if (productText.Length != 32 || !productText.All(Uri.IsHexDigit))
            throw new ArgumentException("Product ID must be exactly 32 hexadecimal characters.");
        var baseValue = Number(Required("--base"), uint.MaxValue, "base");
        ulong? end = values.TryGetValue("--end", out var endText)
            ? Number(endText, Program.AddressLimit, "end") : null;
        byte? fill = values.TryGetValue("--fill", out var fillText)
            ? (byte)Number(fillText, byte.MaxValue, "fill") : null;
        if (fill.HasValue && !end.HasValue)
            throw new ArgumentException("--fill requires --end.");
        if (end.HasValue && end.Value <= baseValue)
            throw new ArgumentException("--end must be greater than --base.");
        return new Options(input, output, key, Convert.FromHexString(productText),
            (uint)baseValue, end, fill);
    }

    private static ulong Number(string text, ulong max, string name)
    {
        var hex = text.StartsWith("0x", StringComparison.OrdinalIgnoreCase);
        var digits = hex ? text[2..] : text;
        if (digits.Length == 0 || !digits.All(c => hex ? Uri.IsHexDigit(c) : c is >= '0' and <= '9') ||
            !ulong.TryParse(digits, hex ? NumberStyles.AllowHexSpecifier : NumberStyles.None,
                CultureInfo.InvariantCulture, out var value) || value > max)
            throw new ArgumentException($"Invalid {name} value.");
        return value;
    }
}

internal sealed record Segment(ulong Address, byte[] Data);

internal static class ImageReader
{
    internal static byte[] Read(Options options)
    {
        List<Segment> segments = Path.GetExtension(options.Input).ToLowerInvariant() switch
        {
            ".bin" => ReadBin(options),
            ".hex" => ReadHex(options.Input),
            ".elf" => ReadElf(options.Input),
            _ => throw new ArgumentException("Input extension must be .bin, .hex or .elf.")
        };
        if (segments.Count == 0)
            throw new ArgumentException("Input has no loaded bytes.");
        segments.Sort((a, b) => a.Address.CompareTo(b.Address));
        if (segments[0].Address != options.Base)
            throw new ArgumentException("--base must equal the lowest loaded physical address.");
        ulong previousEnd = options.Base;
        var hasGap = false;
        foreach (var segment in segments)
        {
            if (segment.Address < previousEnd)
                throw new ArgumentException("Overlapping loaded segments are not permitted.");
            hasGap |= segment.Address > previousEnd;
            previousEnd = segment.Address + (ulong)segment.Data.Length;
        }
        var end = options.End ?? previousEnd;
        if (end < previousEnd)
            throw new ArgumentException("Selected range would discard loaded bytes.");
        hasGap |= end > previousEnd;
        if (hasGap && (!options.End.HasValue || !options.Fill.HasValue))
            throw new ArgumentException("Sparse ranges require both --end and --fill.");
        var length = end - options.Base;
        if (length is 0 or > Program.MaxImageSize)
            throw new ArgumentException("Image must contain 1 byte to 16 MiB.");
        var image = new byte[(int)length];
        if (options.Fill.HasValue)
            Array.Fill(image, options.Fill.Value);
        foreach (var segment in segments)
            segment.Data.CopyTo(image, (int)(segment.Address - options.Base));
        return image;
    }

    private static List<Segment> ReadBin(Options options)
    {
        using var file = File.OpenRead(options.Input);
        if (file.Length is 0 or > Program.MaxImageSize ||
            (ulong)file.Length + options.Base > Program.AddressLimit)
            throw new ArgumentException("BIN is empty, exceeds 16 MiB or overflows the address range.");
        var data = new byte[(int)file.Length];
        file.ReadExactly(data);
        if (file.ReadByte() != -1)
            throw new ArgumentException("BIN changed while reading.");
        return [new Segment(options.Base, data)];
    }

    private static List<Segment> ReadHex(string path)
    {
        var segments = new List<Segment>();
        ulong upper = 0;
        var eof = false;
        var loaded = 0;
        using var file = File.OpenRead(path);
        CheckInputSize(file);
        using var reader = new StreamReader(file, Encoding.ASCII, false);
        // A HEX record has at most 521 characters. Bound each line before decoding.
        while (ReadHexLine(reader) is { } line)
        {
            line = line.Trim();
            if (line.Length == 0)
                continue;
            if (eof)
                throw new ArgumentException("Intel HEX contains records after EOF.");
            if (line[0] != ':' || line.Length < 11 || line.Length % 2 != 1 ||
                !line.AsSpan(1).ToString().All(Uri.IsHexDigit))
                throw new ArgumentException("Invalid Intel HEX record.");
            var record = Convert.FromHexString(line[1..]);
            if (record.Length != record[0] + 5 || record.Sum(b => (int)b) % 256 != 0)
                throw new ArgumentException("Intel HEX length or checksum is invalid.");
            var count = record[0];
            var address = BinaryPrimitives.ReadUInt16BigEndian(record.AsSpan(1, 2));
            var type = record[3];
            var data = record.AsSpan(4, count);
            if (type != 0 && address != 0)
                throw new ArgumentException("Intel HEX control record address must be zero.");
            switch (type)
            {
                case 0:
                    var physical = upper + address;
                    if (address + count > 0x10000 || physical + count > Program.AddressLimit)
                        throw new ArgumentException("Intel HEX data crosses a 64 KiB window or overflows 32 bits.");
                    loaded = checked(loaded + count);
                    if (loaded > Program.MaxImageSize)
                        throw new ArgumentException("Loaded image exceeds 16 MiB.");
                    if (count != 0)
                        segments.Add(new Segment(physical, data.ToArray()));
                    break;
                case 1 when count == 0:
                    eof = true;
                    break;
                case 2 when count == 2:
                    upper = (ulong)BinaryPrimitives.ReadUInt16BigEndian(data) << 4;
                    break;
                case 4 when count == 2:
                    upper = (ulong)BinaryPrimitives.ReadUInt16BigEndian(data) << 16;
                    break;
                case 3 or 5 when count == 4:
                    // Start-address records are entry points, not image mapping bases.
                    break;
                default:
                    throw new ArgumentException("Unsupported Intel HEX type or invalid record length.");
            }
        }
        if (!eof)
            throw new ArgumentException("Intel HEX EOF record is required.");
        return segments;
    }

    private static string? ReadHexLine(StreamReader reader)
    {
        var line = new StringBuilder();
        int value;
        while ((value = reader.Read()) != -1 && value != '\n')
        {
            if (line.Length >= 1024)
                throw new ArgumentException("Intel HEX line is too long.");
            line.Append((char)value);
        }
        return value == -1 && line.Length == 0 ? null : line.ToString();
    }

    private static List<Segment> ReadElf(string path)
    {
        using var file = File.OpenRead(path);
        CheckInputSize(file);
        var ident = ReadAt(file, 0, 16);
        if (!ident.AsSpan(0, 4).SequenceEqual(new byte[] { 0x7f, (byte)'E', (byte)'L', (byte)'F' }) ||
            ident[4] is not (1 or 2) || ident[5] != 1 || ident[6] != 1)
            throw new ArgumentException("Input must be little-endian ELF32 or ELF64.");
        var is64 = ident[4] == 2;
        var headerSize = is64 ? 64 : 52;
        var header = ReadAt(file, 0, headerSize);
        if (U16(header, 16) != 2 || U32(header, 20) != 1 ||
            U16(header, is64 ? 52 : 40) != headerSize)
            throw new ArgumentException("ELF must have a valid ET_EXEC header.");
        var phOffset = is64 ? U64(header, 32) : U32(header, 28);
        var phSize = U16(header, is64 ? 54 : 42);
        var phCount = U16(header, is64 ? 56 : 44);
        if (phCount is 0 or ushort.MaxValue || phSize != (is64 ? 56 : 32) ||
            phOffset < (ulong)headerSize)
            throw new ArgumentException("Invalid ELF program header table; extended counts are unsupported.");
        CheckBounds(file, phOffset, (ulong)phSize * phCount);
        var segments = new List<Segment>();
        ulong loaded = 0;
        for (var i = 0; i < phCount; i++)
        {
            var ph = ReadAt(file, phOffset + (ulong)i * phSize, phSize);
            var offset = is64 ? U64(ph, 8) : U32(ph, 4);
            var address = is64 ? U64(ph, 24) : U32(ph, 12);
            var filesz = is64 ? U64(ph, 32) : U32(ph, 16);
            var memsz = is64 ? U64(ph, 40) : U32(ph, 20);
            CheckBounds(file, offset, filesz);
            if (U32(ph, 0) != 1)
                continue;
            if (filesz > memsz)
                throw new ArgumentException("ELF PT_LOAD filesz exceeds memsz.");
            if (filesz == 0)
                continue;
            if (address >= Program.AddressLimit || filesz > Program.AddressLimit - address)
                throw new ArgumentException("ELF physical loaded range overflows 32 bits.");
            if (filesz > Program.MaxImageSize || loaded + filesz > Program.MaxImageSize)
                throw new ArgumentException("ELF loaded image exceeds 16 MiB.");
            loaded += filesz;
            segments.Add(new Segment(address, ReadAt(file, offset, (int)filesz)));
        }
        return segments;
    }

    private static void CheckBounds(FileStream file, ulong offset, ulong count)
    {
        var length = (ulong)file.Length;
        if (offset > length || count > length - offset)
            throw new ArgumentException("ELF offset or length exceeds the input file.");
    }

    private static void CheckInputSize(FileStream file)
    {
        if (file.Length > Program.MaxImageSize)
            throw new ArgumentException("Input file exceeds 16 MiB.");
    }

    private static byte[] ReadAt(FileStream file, ulong offset, int count)
    {
        CheckBounds(file, offset, (ulong)count);
        file.Position = (long)offset;
        var bytes = new byte[count];
        file.ReadExactly(bytes);
        return bytes;
    }

    private static ushort U16(byte[] data, int offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset, 2));
    private static uint U32(byte[] data, int offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));
    private static ulong U64(byte[] data, int offset) =>
        BinaryPrimitives.ReadUInt64LittleEndian(data.AsSpan(offset, 8));
}

internal static class PackageWriter
{
    private static readonly BigInteger Order = new(
        Convert.FromHexString("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551"),
        isUnsigned: true, isBigEndian: true);

    internal static byte[] Manifest(byte[] image, Options options, ECDsa key)
    {
        var manifest = new byte[256];
        Encoding.ASCII.GetBytes("SUF2MFST").CopyTo(manifest, 0);
        Put(manifest, 8, 1);
        options.ProductId.CopyTo(manifest, 16);
        Put(manifest, 32, options.Base);
        Put(manifest, 36, (uint)image.Length);
        var point = new byte[65];
        point[0] = 4;
        var publicKey = key.ExportParameters(false);
        if (publicKey.Q.X?.Length != 32 || publicKey.Q.Y?.Length != 32)
            throw new CryptographicException();
        publicKey.Q.X.CopyTo(point, 1);
        publicKey.Q.Y.CopyTo(point, 33);
        SHA256.HashData(point).AsSpan(0, 16).CopyTo(manifest.AsSpan(40, 16));
        var digest = SHA256.HashData(image);
        digest.CopyTo(manifest, 96);
        digest.CopyTo(manifest, 128);
        var signed = new byte[209];
        Encoding.ASCII.GetBytes("SUF2-MANIFEST-V1\0").CopyTo(signed, 0);
        manifest.AsSpan(0, 192).CopyTo(signed.AsSpan(17));
        var signature = key.SignData(signed, HashAlgorithmName.SHA256,
            DSASignatureFormat.IeeeP1363FixedFieldConcatenation);
        if (signature.Length != 64)
            throw new CryptographicException();
        var r = new BigInteger(signature.AsSpan(0, 32), isUnsigned: true, isBigEndian: true);
        var s = new BigInteger(signature.AsSpan(32, 32), isUnsigned: true, isBigEndian: true);
        if (r <= 0 || r >= Order || s <= 0 || s >= Order)
            throw new CryptographicException();
        if (s > Order / 2)
        {
            var lowS = (Order - s).ToByteArray(isUnsigned: true, isBigEndian: true);
            signature.AsSpan(32, 32).Clear();
            lowS.CopyTo(signature, 64 - lowS.Length);
        }
        signature.CopyTo(manifest, 192);
        return manifest;
    }

    internal static void Write(string path, byte[] image, byte[] manifest)
    {
        var packageId = SHA256.HashData(manifest).AsSpan(0, 16).ToArray();
        var count = (uint)((image.Length + 255) / 256 + 1);
        using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        try
        {
            var block = new byte[512];
            for (uint number = 0; number < count; number++)
            {
                Array.Clear(block);
                Put(block, 0, 0x0A324655);
                Put(block, 4, 0x9E5D5157);
                Put(block, 8, 0x00002001);
                Put(block, 16, 272);
                Put(block, 20, number);
                Put(block, 24, count);
                Put(block, 28, 0x1E0424ED);
                packageId.CopyTo(block, 32);
                if (number == 0)
                    manifest.CopyTo(block, 48);
                else
                {
                    var offset = (int)(number - 1) * 256;
                    image.AsSpan(offset, Math.Min(256, image.Length - offset)).CopyTo(block.AsSpan(48));
                }
                Put(block, 508, 0x0AB16F30);
                output.Write(block);
            }
            output.Flush(flushToDisk: true);
        }
        catch
        {
            output.Dispose();
            // Never leave a partially written package after a reported write failure.
            try { File.Delete(path); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            throw;
        }
    }

    private static void Put(byte[] bytes, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset, 4), value);
}
