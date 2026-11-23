using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

internal sealed class Launcher : Form
{
    internal static readonly Version Installed = new Version(Assembly.GetExecutingAssembly().GetName().Version.ToString(3));
    private const string Preferences = @"HKEY_CURRENT_USER\Software\Thwargle\AC2Unreal";
    private readonly string home = AppDomain.CurrentDomain.BaseDirectory;
    private readonly string cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Thwargle", "AC2Unreal", "Updates");
    private readonly TextBox dat = new TextBox { Dock = DockStyle.Fill, ReadOnly = true };
    private readonly CheckBox automatic = new CheckBox { Text = "Automatically download and install updates", AutoSize = true };
    private readonly Label status = new Label { Dock = DockStyle.Fill, AutoEllipsis = true };
    private readonly Button check = new Button { Text = "Check for updates", AutoSize = true };
    private readonly Button cancel = new Button { Text = "Cancel update", AutoSize = true, Enabled = false };
    private readonly Button play = new Button { Text = "Play", Width = 90 };
    private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 15 * 60 * 1000 };
    private CancellationTokenSource job;
    private bool paused, handedOff;
    private Process game;

    internal Launcher()
    {
        Text = "AC2:Unreal"; ClientSize = new Size(540, 255);
        AutoScaleMode = AutoScaleMode.Dpi; StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false;
        Font = new Font("Segoe UI", 10);
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(18), ColumnCount = 2, RowCount = 6 };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var heading = new Label { Text = "AC2:Unreal   " + Installed, Font = new Font(Font.FontFamily, 16), AutoSize = true };
        grid.Controls.Add(heading, 0, 0); grid.SetColumnSpan(heading, 2);
        grid.Controls.Add(new Label { Text = "Asheron’s Call 2 DAT folder", AutoSize = true }, 0, 1);
        grid.Controls.Add(dat, 0, 2);
        var browse = new Button { Text = "Browse…", AutoSize = true };
        browse.Click += delegate { ChooseData(); };
        grid.Controls.Add(browse, 1, 2); grid.Controls.Add(automatic, 0, 3); grid.SetColumnSpan(automatic, 2);
        grid.Controls.Add(status, 0, 4); grid.SetColumnSpan(status, 2);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = false };
        actions.Controls.Add(play); actions.Controls.Add(check); actions.Controls.Add(cancel);
        grid.Controls.Add(actions, 0, 5); grid.SetColumnSpan(actions, 2); Controls.Add(grid);
        dat.Text = Convert.ToString(Registry.GetValue(Preferences, "DatFolder", ""));
        automatic.Checked = Convert.ToInt32(Registry.GetValue(Preferences, "AutomaticUpdates", 0)) == 1;
        automatic.CheckedChanged += delegate {
            Registry.SetValue(Preferences, "AutomaticUpdates", automatic.Checked ? 1 : 0);
            if (!automatic.Checked) StopUpdate(); else { paused = false; AutoCheck(); }
        };
        status.Text = "Choose your original game data to play.";
        play.Click += delegate { Play(); };
        check.Click += async delegate { paused = false; await Update(false); };
        cancel.Click += delegate { StopUpdate(); };
        timer.Tick += delegate { AutoCheck(); };
        Shown += delegate { ClearOldInstallers(); timer.Start(); AutoCheck(); };
        FormClosing += delegate { timer.Stop(); paused = true; if (job != null) job.Cancel(); };
    }
    private void ClearOldInstallers()
    {
        if (!Directory.Exists(cache)) return;
        foreach (string f in Directory.GetFiles(cache, "AC2-update-*.exe"))
            if (File.GetLastWriteTimeUtc(f) < DateTime.UtcNow.AddDays(-1)) Discard(f);
    }
    private static void Discard(string path)
    {
        try { if (path != null) File.Delete(path); } catch (IOException) {} catch (UnauthorizedAccessException) {}
    }
    private bool ChooseData()
    {
        using (var dialog = new FolderBrowserDialog { Description = "Choose your Asheron’s Call 2 installation", SelectedPath = dat.Text, ShowNewFolderButton = false })
        {
            if (dialog.ShowDialog(this) != DialogResult.OK) return false;
            if (!ValidDat(dialog.SelectedPath)) { status.Text = "The folder needs portal.dat, cell_1.dat, and local_English.dat."; return false; }
            dat.Text = dialog.SelectedPath; Registry.SetValue(Preferences, "DatFolder", dat.Text); return true;
        }
    }
    private static bool ValidDat(string dir)
    {
        foreach (string f in new[] { "portal.dat", "cell_1.dat", "local_English.dat" })
            if (!File.Exists(Path.Combine(dir, f))) return false;
        return true;
    }
    private void Play()
    {
        if (handedOff || game != null || (!ValidDat(dat.Text) && !ChooseData())) return;
        StopUpdate();
        string dir = dat.Text.TrimEnd(Path.DirectorySeparatorChar);
        if (dir.EndsWith(":")) dir += @"\.";
        try
        {
            var info = new ProcessStartInfo(Path.Combine(home, "AC2Unreal.exe"), "-SaveToUserDir -AC2DatDir=\"" + dir + "\"") { WorkingDirectory = home, UseShellExecute = false };
            // Launch the game binary directly so the tracked process owns the full play session.
            string shipping = Path.Combine(home, "AC2Unreal", "Binaries", "Win64", "AC2Unreal-Win64-Shipping.exe");
            info.FileName = shipping;
            game = new Process { StartInfo = info, EnableRaisingEvents = true };
            game.Exited += delegate { if (IsHandleCreated) BeginInvoke((Action)delegate { if (game != null) game.Dispose(); game = null; Show(); Activate(); status.Text = "Ready to play."; }); };
            game.Start(); Hide();
        }
        catch (Exception e) { if (game != null) game.Dispose(); game = null; status.Text = "Could not start the game: " + e.Message; }
    }
    private void StopUpdate() { paused = true; if (job != null) job.Cancel(); }
    private async void AutoCheck() { if (automatic.Checked && !paused && Visible) await Update(true); }
    private bool Running()
    {
        if (game != null) return true;
        foreach (var p in Process.GetProcessesByName("AC2Unreal-Win64-Shipping"))
            using (p) { try { if (p.MainModule.FileName.StartsWith(home, StringComparison.OrdinalIgnoreCase)) return true; } catch (System.ComponentModel.Win32Exception) { return true; } }
        return false;
    }
    private async Task Update(bool auto)
    {
        if (job != null || handedOff || Running()) return;
        var source = new CancellationTokenSource(); job = source;
        check.Enabled = false; cancel.Enabled = true;
        string file = null;
        try
        {
            status.Text = "Checking for updates…";
            var release = await UpdateFeed.Check(source.Token);
            source.Token.ThrowIfCancellationRequested();
            if (release.Version <= Installed) { status.Text = "You have the latest version."; return; }
            if (!auto && MessageBox.Show(this, "Download and install AC2:Unreal " + release.Version + "?", Text, MessageBoxButtons.YesNo) != DialogResult.Yes) { paused = true; status.Text = "Update available."; return; }
            Directory.CreateDirectory(cache);
            file = Path.Combine(cache, "AC2-update-" + Guid.NewGuid().ToString("N") + ".exe");
            await UpdateFeed.Download(release, file, p => { status.Text = "Downloading " + release.Version + " · " + p + "%"; }, source.Token);
            for (int seconds = 8; seconds > 0; --seconds)
            {
                status.Text = "Verified. The launcher will close and reopen for installation in " + seconds + "s.";
                await Task.Delay(1000, source.Token);
            }
            source.Token.ThrowIfCancellationRequested();
            if (Running()) { status.Text = "Update deferred while the game is running."; return; }
            Process.Start(new ProcessStartInfo(file, "/SILENT /NORESTART /AC2RESTART /DIR=\"" + home.TrimEnd('\\') + "\"") { UseShellExecute = false });
            handedOff = true; file = null; Close();
        }
        catch (OperationCanceledException) { if (!IsDisposed) status.Text = "Update cancelled."; }
        catch (Exception e) { paused = true; if (!IsDisposed) status.Text = "Update unavailable: " + e.Message; }
        finally
        {
            Discard(file); source.Dispose(); job = null;
            if (!IsDisposed) { check.Enabled = true; cancel.Enabled = false; }
        }
    }
    [STAThread] static void Main()
    {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        bool first;
        using (var instance = new Mutex(true, @"Local\Thwargle.AC2Unreal.Launcher", out first))
        {
            if (!first) return;
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Launcher());
        }
    }
}
