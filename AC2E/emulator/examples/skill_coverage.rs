//! Run after regenerating skill-effects.tsv to audit executable versus decoded records.
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args().skip(1);
    match args.next().as_deref() {
        None => {}
        Some("--7230") if args.next().is_none() => {
            pyrealium::data_profile::set_unconverted_7230(true)
        }
        _ => return Err("usage: skill_coverage [--7230]".into()),
    }
    let runtime = pyrealium::abilities::Runtime::load()?;
    println!("{}", serde_json::to_string_pretty(&runtime.coverage())?);
    Ok(())
}
