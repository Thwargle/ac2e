//! Recovered Shelter tutorial introduction. Text and NPC identities are native;
//! the three introduction transitions are a reconstructed, versioned server rule.
//! The tool/glyph/Menhir stages are not replaced by the development training quest.
use crate::{quests::{Book, Command}, script, world::WorldError};

pub const GUARD: u32 = 0x4700157d;
pub const LIEUTENANT: u32 = 0x4700157e;
pub const SENTRY: u32 = 0x47001581;
pub const JOURNAL: u32 = 0x4000ff05; // AC2E identity; original quest ID not recovered.
pub fn supported(entity: u32) -> bool {
    matches!(entity, GUARD | LIEUTENANT | SENTRY | 0x47001573..=0x4700157c)
}
pub fn guidance(phase: u8) -> &'static str {
    match phase {
        1 => "Rescue the Shelter Hold: read the training stones, then speak to the Shelter Lieutenant.",
        2 => "Rescue the Shelter Hold: continue through the training stones to the Shelter Sentry.",
        3 => "Rescue the Shelter Hold: the Sentry asks for a Drudge Digger's tool. The tool, Scout, glyph and Menhir stages are not yet playable in AC2E. The separate Training Grounds circuit remains available.",
        _ => "Speak to the Shelter Guard to begin Rescue the Shelter Hold.",
    }
}
/// Return the exact StringInfo table/key and optional once-only state change.
pub fn introduction(book: &Book, entity: u32) -> Option<(u32, u32, Option<Command>)> {
    let phase = book.shelter_phase();
    Some(match entity {
        GUARD => (0x25001648, if phase == 0 {0x2824fd4c} else {0x63691d8f},
            (phase == 0).then_some(Command::ShelterGuard)),
        LIEUTENANT if phase == 0 => (0x25001649, 0x8b6b186d, None),
        LIEUTENANT => (0x25001649, if phase == 1 {0x8b6b186f} else {0x63691d8f},
            (phase == 1).then_some(Command::ShelterLieutenant)),
        SENTRY => (0x2500164c, match phase {0=>0x8b6b186d,1=>0x8b6b186f,2=>0x8b6b1869,_=>0x8b6b186b},
            (phase == 2).then_some(Command::ShelterSentry)),
        0x47001573..=0x4700157c => (0x2500163e + entity - 0x47001573, 0x6c459364, None),
        _ => return None,
    })
}
pub fn text(table: u32, key: u32) -> Result<Vec<u8>, WorldError> {
    let mut args = script::localized(table, key)?;
    args.extend(script::integer(1));
    script::private_call(script::CHAT_STRING, &args)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn introductions_are_ordered_repeatable_and_keep_native_strings() {
        let mut book = Book::default();
        assert_eq!(introduction(&book,LIEUTENANT),Some((0x25001649,0x8b6b186d,None)));
        assert!(book.apply(&Command::ShelterSentry).is_err());
        for (npc,command,phase) in [(GUARD,Command::ShelterGuard,1),(LIEUTENANT,Command::ShelterLieutenant,2),(SENTRY,Command::ShelterSentry,3)] {
            assert_eq!(introduction(&book,npc).unwrap().2,Some(command));
            book=book.apply(&command).unwrap();
            assert_eq!(book.shelter_phase(),phase);
            assert_eq!(introduction(&book,npc).unwrap().2,None);
            assert!(book.apply(&command).is_err());
            assert_eq!(Command::decode(&command.encode()).unwrap(),command);
        }
        assert!(introduction(&book,0x47001587).is_none());
        assert_eq!(introduction(&book,0x4700157c),Some((0x25001647,0x6c459364,None)));
        assert!(!crate::quests::rewards(&Command::ShelterSentry).iter().next().is_some());
        assert!(book.list_graph().unwrap().windows(4).any(|v|v==JOURNAL.to_le_bytes()));
    }
}
