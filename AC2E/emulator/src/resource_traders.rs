//! Resource trader shelves from the installed StoreTemplate graphs. AC2Hero's
//! resource guide records the three pack sizes the retail client offered.
use super::{GOLD_DEFINITION, Stock};
use crate::{
    dat::Dat,
    inventory::{Item, Location, Roll},
    professions,
};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, io};

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Merchandise {
    pub(super) item: u32,
    pub(super) quantity: u16,
    pub(super) price: u32,
    pub(super) texture: u32,
}

impl Merchandise {
    fn definition(self) -> &'static professions::Definition {
        professions::catalog()
            .items
            .iter()
            .find(|d| d.id == self.item)
            .expect("validated resource trader merchandise")
    }

    pub(super) fn name(self) -> &'static str {
        &self.definition().name
    }

    pub(super) fn item(self, id: u64, slot: u16) -> Item {
        let d = self.definition();
        Item {
            id,
            definition: d.id,
            visual: d.visual,
            quantity: self.quantity,
            stack_limit: d.stack,
            equipment_mask: 0,
            location: Location::Bag(slot),
            roll: crate::harvesting::tool(d.id).map(|_| Roll {
                level: 1,
                damage: 0,
                armor: 0,
                tint: 0,
                crafted: true,
                wear: 0,
            }),
        }
    }
}

#[derive(Deserialize)]
struct Stores {
    stores: Vec<Store>,
}
#[derive(Deserialize)]
struct Store {
    store: u32,
    vendors: Vec<u32>,
    sha256: String,
    offers: Vec<Offer>,
}
#[derive(Deserialize)]
struct Offer {
    ordinal: usize,
    item: u32,
    currency: u32,
    unit_price: f64,
}

pub(super) type Shelves = BTreeMap<u32, (u32, Vec<Stock>)>;

pub(super) fn load(portal: &mut Dat) -> io::Result<Shelves> {
    let stores: Stores = serde_json::from_str(include_str!("../content/native-store-stock.json"))?;
    let mut shelves = BTreeMap::new();
    for store in stores
        .stores
        .into_iter()
        .filter(|s| (0x6f003570..=0x6f003575).contains(&s.store))
    {
        let vendor = 0x47002f8e + (store.store - 0x6f003570);
        let signature = format!("{:x}", Sha256::digest(portal.read(store.store)?));
        if signature != store.sha256 || store.vendors != [vendor] || store.offers.len() != 7 {
            return Err(io::Error::other(
                "unsupported resource trader store template",
            ));
        }
        let mut stock = Vec::new();
        for (ordinal, offer) in store.offers.iter().enumerate() {
            if offer.ordinal != ordinal || offer.currency != GOLD_DEFINITION {
                return Err(io::Error::other("unsupported resource trader offer"));
            }
            // Foreground texture DIDs are inherited from each item's native Icon property.
            let (texture, quantities): (u32, &[u16]) = match offer.item {
                0x47002ad4 => (0x410082f0, &[200, 1000, 5000]),
                0x470010fa => (0x410082f1, &[2, 10, 50]),
                0x470027a2 => (0x41004307, &[2, 10, 50]),
                0x470010ff => (0x41004311, &[2, 10, 50]),
                0x47002f71 | 0x47002f72 | 0x47002f74 | 0x47002f75 => (0x4100034b, &[1]),
                0x47002f73 => (0x410082d9, &[1]),
                0x47002f76 => (0x410082da, &[1]),
                _ => return Err(io::Error::other("unknown resource trader item")),
            };
            portal.read(texture)?;
            let d = professions::catalog()
                .items
                .iter()
                .find(|d| d.id == offer.item)
                .ok_or_else(|| io::Error::other("missing resource trader inventory definition"))?;
            for &quantity in quantities {
                let price = f64::from(quantity) * offer.unit_price;
                if !(1.0..=f64::from(u32::MAX)).contains(&price)
                    || price.fract() != 0.0
                    || quantity > d.stack
                {
                    return Err(io::Error::other("invalid resource bundle"));
                }
                stock.push(Stock::Native(Merchandise {
                    item: d.id,
                    quantity,
                    price: price as u32,
                    texture,
                }));
            }
        }
        shelves.insert(vendor, (store.store, stock));
    }
    if shelves.len() != 6 {
        return Err(io::Error::other("missing native resource trader"));
    }
    Ok(shelves)
}
