using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
internal static class UpdateTests
{
    private static void MustFail(Action test) { try { test(); } catch (InvalidDataException) { return; } throw new Exception("Expected rejection"); }
    private static void Main()
    {
        const string hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        const string json = "{\"product\":\"ac2-unreal-windows-x64\",\"version\":\"0.11.0\",\"bytes\":3,\"sha256\":\"" + hash + "\",\"installer\":\"https://thwargle.com/downloads/ac2/v0.11.0/AC2-Unreal-Setup-0.11.0.exe\"}";
        var r = ClientRelease.Parse(json);
        MustFail(() => ClientRelease.Parse(json.Replace("thwargle.com", "example.com")));
        MustFail(() => ClientRelease.Parse(json.Replace("thwargle.com/downloads/ac2", "github.com/Thwargle/ac2e/releases/download")));
        MustFail(() => ClientRelease.Parse(json.Replace("/v0.11.0/", "/v0.10.0/")));
        MustFail(() => ClientRelease.Parse(json.Replace(hash, "bad")));
        MustFail(() => ClientRelease.Parse(json.Replace(":3,", ":0,")));
        var data = new byte[] {97, 98, 99};
        Action<byte[]> verify = b => {
            using (var output = new MemoryStream())
            {
                UpdateFeed.VerifyAndCopy(new MemoryStream(b), output, r, p => {}, CancellationToken.None).GetAwaiter().GetResult();
                if (!output.ToArray().SequenceEqual(data)) throw new Exception("Copy mismatch");
            }
        };
        verify(data);
        MustFail(() => verify(new byte[]{97,98,100})); MustFail(() => verify(new byte[]{97,98})); MustFail(() => verify(new byte[]{97,98,99,100}));
        try {
            UpdateFeed.VerifyAndCopy(new MemoryStream(data), new MemoryStream(), r, p=>{}, new CancellationToken(true)).GetAwaiter().GetResult();
            throw new Exception("Cancellation ignored");
        } catch (OperationCanceledException) {}
        Console.WriteLine("Passed update feed, host, size, SHA-256, copy and cancellation checks.");
    }
}
