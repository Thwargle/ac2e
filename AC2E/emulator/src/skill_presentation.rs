//! Cosmetic skill hook data from the installed Legions DAT. This never grants
//! a skill or makes unsupported gameplay executable. See import_skill_presentation.py.
use std::{collections::BTreeMap, sync::OnceLock};

#[derive(Clone, Copy, Debug, Default, serde::Deserialize)]
pub struct Profile {
    pub cast_fx: u32,
    pub release_fx: u32,
    missile_entity: u32,
    missile_visual: u32,
}
impl Profile {
    pub fn projectile(self) -> Option<crate::projectiles::Profile> {
        (self.missile_entity >> 24 == 0x47 && self.missile_visual >> 24 == 0x1f)
            .then_some(crate::projectiles::Profile { entity: self.missile_entity, visual: self.missile_visual })
    }
}
#[derive(serde::Deserialize)]
struct Catalog {
    version: u32,
    skills: BTreeMap<String, Profile>,
    record_sha256: BTreeMap<String, String>,
}
fn catalog() -> &'static Catalog {
    static DATA: OnceLock<Catalog> = OnceLock::new();
    DATA.get_or_init(|| {
        let data: Catalog = serde_json::from_str(include_str!("../content/skill-presentation.json"))
            .expect("checked-in skill presentation catalog");
        assert_eq!(data.version, 1);
        data
    })
}
pub fn profile(skill: u32) -> Profile {
    // Do not apply Legions cosmetic identities to the unconverted 7230 release.
    if crate::data_profile::unconverted_7230() { return Profile::default(); }
    catalog().skills.get(&format!("{skill:x}")).copied().unwrap_or_default()
}
pub fn validate_assets(portal: &mut crate::dat::Dat) -> std::io::Result<()> {
    use sha2::{Digest, Sha256};
    if crate::data_profile::unconverted_7230() { return Ok(()); }
    for (record, expected) in &catalog().record_sha256 {
        let did = u32::from_str_radix(record, 16).map_err(std::io::Error::other)?;
        if &format!("{:x}", Sha256::digest(portal.read(did)?)) != expected {
            return Err(std::io::Error::other(format!("skill presentation record changed: {did:08x}; regenerate skill-presentation.json")));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_cast_release_and_projectile_are_distinct() {
        let flame = profile(0x59);
        assert_eq!(flame.cast_fx, 0x81000281);
        assert_eq!(flame.release_fx, 0x410004a9);
        assert_eq!(flame.projectile().unwrap().entity, 0x47000d04);
        assert_eq!(profile(0x55).cast_fx, 0x810001d0);
        assert!(profile(0x55).projectile().is_none());
        assert_eq!(profile(0x60).cast_fx, 0x810001cd);
        assert!(profile(0x60).projectile().is_none());
        assert_eq!(profile(u32::MAX).cast_fx, 0);
        assert!(profile(u32::MAX).projectile().is_none());
        for p in catalog().skills.values() {
            assert_eq!(p.missile_entity == 0, p.missile_visual == 0);
            for fx in [p.cast_fx, p.release_fx] {
                assert!(fx == 0 || matches!(fx >> 24, 0x40 | 0x41 | 0x81));
            }
        }
    }
}
