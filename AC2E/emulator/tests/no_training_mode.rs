//! Removed development switches must fail before opening a store or listener.
use std::process::Command;

#[test]
fn removed_training_mode_cannot_be_selected_or_advertised() {
    let server = env!("CARGO_BIN_EXE_ac2e");
    for args in [vec!["serve-training"], vec!["serve", "--training-kit", "true"],
                 vec!["serve", "--training-kit", "false"]] {
        let output = Command::new(server).args(&args).output().unwrap();
        assert!(!output.status.success(), "Accepted removed option: {args:?}");
        assert!(String::from_utf8_lossy(&output.stderr).contains("unknown"));
    }
    let help = Command::new(server).arg("--help").output().unwrap();
    assert!(help.status.success());
    let text = String::from_utf8(help.stdout).unwrap();
    assert!(!text.contains("serve-training") && !text.contains("--training-kit"));
    assert!(text.contains("serve --game-data"));
}
