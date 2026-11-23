#[test]
fn coverage_uses_the_same_profile_as_the_runtime() {
    pyrealium::data_profile::set_unconverted_7230(true);
    let report = pyrealium::abilities::Runtime::load().unwrap().coverage();
    assert_eq!(report["data_profile"], "7230");
    let rows = report["skills"].as_array().unwrap();
    let expected: Vec<_> = include_str!("../content/7230/skills.tsv")
        .lines()
        .filter(|l| !l.starts_with('#') && l.contains('\t'))
        .collect();
    assert_eq!(rows.len(), expected.len());
    for (row, line) in rows.iter().zip(expected) {
        let f: Vec<_> = line.split('\t').collect();
        assert_eq!(row["skill"], f[0]);
        assert_eq!(row["record"], f[1]);
        assert_eq!(row["race"], f[4].parse::<u32>().unwrap());
    }
}
