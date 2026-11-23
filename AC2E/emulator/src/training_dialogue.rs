// SPDX-License-Identifier: AGPL-3.0-only
//! Authored training conversations, separate from recovered native wire layouts.
use crate::{appearance, quests::Book};

pub const MENTOR_ENTITY: u32 = 0x47001699;
pub const TUMEROK_RETURN: u64 = 0x8140000000030030;
pub const LUGIAN_RETURN: u64 = 0x8140000000030031;

pub fn race_name(race: u32) -> &'static str {
    match race { appearance::LUGIAN => "Lugian", appearance::TUMEROK => "Tumerok", _ => "Human" }
}
pub fn greeting(race: u32, book: &Book) -> String {
    format!("Welcome, {} adventurer. {}", race_name(race), book.guidance())
}
pub fn portal_name(id: u64) -> Option<&'static str> {
    match id {
        TUMEROK_RETURN => Some("Return to Tumerok Training"),
        LUGIAN_RETURN => Some("Return to Lugian Training"),
        0x8140000000030005 => Some("Return to Human Training"),
        0x8140000000030022 | 0x8140000000030025 => Some("Shared Training Grounds"),
        _ => None,
    }
}
