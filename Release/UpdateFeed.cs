using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

internal sealed class ClientRelease
{
    public Version Version;
    public Uri Installer;
    public long Bytes;
    public string Sha256;
    internal static ClientRelease Parse(string json)
    {
        var values = new JavaScriptSerializer { MaxJsonLength = 8192 }.Deserialize<Dictionary<string, object>>(json);
        string number = Convert.ToString(values["version"], CultureInfo.InvariantCulture);
        if (!Regex.IsMatch(number, @"^\d{1,4}\.\d{1,4}\.\d{1,4}$")) throw new InvalidDataException("Invalid version in update feed.");
        string url = "https://github.com/Thwargle/ac2e/releases/download/v" + number + "/AC2-Unreal-Setup-" + number + ".exe";
        long bytes = Convert.ToInt64(values["bytes"], CultureInfo.InvariantCulture);
        string digest = Convert.ToString(values["sha256"]);
        if (Convert.ToString(values["product"]) != "ac2-unreal-windows-x64" || Convert.ToString(values["installer"]) != url ||
            bytes <= 0 || bytes > 8L * 1024 * 1024 * 1024 || !Regex.IsMatch(digest, "^[a-fA-F0-9]{64}$"))
            throw new InvalidDataException("Invalid Windows installer metadata.");
        return new ClientRelease { Version = new Version(number), Installer = new Uri(url), Bytes = bytes, Sha256 = digest };
    }
}

internal static class UpdateFeed
{
    private static readonly Uri Feed = new Uri("https://thwargle.github.io/ac2e/release.json");
    private static readonly HttpClient Http = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false });
    internal static async Task<ClientRelease> Check(CancellationToken cancel)
    {
        using (var request = new HttpRequestMessage(HttpMethod.Get, Feed))
        {
            request.Headers.CacheControl = new System.Net.Http.Headers.CacheControlHeaderValue { NoCache = true };
            using (var response = await Http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancel))
            {
                response.EnsureSuccessStatusCode();
                using (var stream = await response.Content.ReadAsStreamAsync())
                using (var buffer = new MemoryStream())
                {
                    byte[] part = new byte[2048]; int n;
                    while ((n = await stream.ReadAsync(part, 0, part.Length, cancel)) != 0)
                    {
                        if (buffer.Length + n > 8192) throw new InvalidDataException("Update feed too large.");
                        buffer.Write(part, 0, n);
                    }
                    return ClientRelease.Parse(System.Text.Encoding.UTF8.GetString(buffer.ToArray()));
                }
            }
        }
    }
    private static async Task<HttpResponseMessage> OpenAsset(Uri url, CancellationToken cancel)
    {
        for (int i = 0; i < 5; i++)
        {
            if (url.Scheme != "https" || !url.IsDefaultPort || url.UserInfo.Length != 0 ||
                !(url.Host == "github.com" || url.Host == "release-assets.githubusercontent.com" || url.Host == "objects.githubusercontent.com"))
                throw new InvalidDataException("Unexpected installer host.");
            var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, cancel);
            if (response.StatusCode == HttpStatusCode.OK) return response;
            using (response)
            {
                int code = (int)response.StatusCode;
                if (code < 300 || code > 399 || response.Headers.Location == null)
                    throw new IOException("Installer download returned HTTP " + code);
                url = new Uri(url, response.Headers.Location);
            }
        }
        throw new IOException("Too many installer redirects.");
    }
    internal static async Task VerifyAndCopy(Stream input, Stream output, ClientRelease release, Action<int> progress, CancellationToken cancel)
    {
        using (var sha = SHA256.Create())
        {
            byte[] part = new byte[128 * 1024]; long length = 0; int n; int last = -1;
            while ((n = await input.ReadAsync(part, 0, part.Length, cancel)) > 0)
            {
                length += n;
                if (length > release.Bytes) throw new InvalidDataException("Installer exceeds the published size.");
                sha.TransformBlock(part, 0, n, part, 0);
                await output.WriteAsync(part, 0, n, cancel);
                int pct = (int)(100 * length / release.Bytes);
                if (pct != last) { progress(pct); last = pct; }
            }
            sha.TransformFinalBlock(new byte[0], 0, 0);
            if (length != release.Bytes || !String.Equals(BitConverter.ToString(sha.Hash).Replace("-", ""), release.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The installer did not match the published size and SHA-256. It was discarded.");
        }
    }
    internal static async Task Download(ClientRelease release, string path, Action<int> progress, CancellationToken cancel)
    {
        using (var response = await OpenAsset(release.Installer, cancel))
        using (var input = await response.Content.ReadAsStreamAsync())
        using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 131072, true))
            await VerifyAndCopy(input, output, release, progress, cancel);
    }
}
