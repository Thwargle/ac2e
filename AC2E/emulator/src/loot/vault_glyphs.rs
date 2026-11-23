//! Ringway glyph sources documented by the three Osteth vault guides. Regional
//! bounds are the guide anchor hulls in regional-content-v1.json, in map cells.
use crate::{arwic_mines, esper_camp, inventory::Item, movement::Placement, windmill};

const ARWIC: &[[f64; 2]] = &[[943.5,1483.08], [1001.64,1377.0], [1106.7,1448.4], [1020.0,1498.38]];
const ESPER: &[[f64; 2]] = &[[755.82,1440.24], [767.04,1323.96], [871.08,1480.02]];
const LOST_WISH: &[[f64; 2]] = &[[1120.98,1561.62], [1135.26,1470.84], [1238.28,1502.46], [1212.78,1565.70]];
const CRAWLSPACE: &[u32] = &[0x47000f7d,0x47000f7f,0x47001073,0x47001083,0x470012cc];
const DANK: &[u32] = &[0x47000f80,0x47001071,0x470012cd];
const TANGLEFOOT: &[u32] = &[0x47000f81,0x47000f82,0x47001081,0x47001082,0x470012cf,0x470012d0,0x470012d1];

pub(super) fn is_vault_glyph(id: u32) -> bool {
    matches!(id, arwic_mines::GLYPH | esper_camp::GLYPH | windmill::GLYPH)
}

fn inside(point: [f64; 2], hull: &[[f64; 2]]) -> bool {
    hull.iter().zip(hull.iter().cycle().skip(1)).all(|(a, b)| {
        (b[0]-a[0])*(point[1]-a[1]) >= (b[1]-a[1])*(point[0]-a[0])
    })
}

pub(super) fn drop_for(entity: u32, spawn: Placement, random: &mut u64) -> Option<Item> {
    if !(1..=64).contains(&(spawn.cell & 0xffff)) { return None; }
    let point = [f64::from(spawn.cell >> 24)*8.0 + f64::from(spawn.coordinates[0])/20.0,
                 f64::from((spawn.cell >> 16) & 255)*8.0 + f64::from(spawn.coordinates[1])/20.0];
    let glyph = if (CRAWLSPACE.contains(&entity) || DANK.contains(&entity)) && inside(point, ARWIC) {
        arwic_mines::GLYPH
    } else if (DANK.contains(&entity) || CRAWLSPACE.contains(&entity)) && inside(point, LOST_WISH) {
        windmill::GLYPH
    } else if TANGLEFOOT.contains(&entity) && inside(point, ESPER) {
        esper_camp::GLYPH
    } else { return None; };
    // Published guides identify the source and zone but do not give a drop rate.
    if crate::creature_ai::random(random) >= 0.10 { return None; }
    match glyph {
        arwic_mines::GLYPH => arwic_mines::item(glyph,1,0,1).ok(),
        windmill::GLYPH => windmill::item(glyph,1,0).ok(),
        esper_camp::GLYPH => esper_camp::item(glyph,1,0,1).ok(),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn glyphs_require_the_documented_species_and_ringway() {
        let mut seed=81735;
        for (cell, species, glyph) in [(0x7eb50001,DANK[0],arwic_mines::GLYPH),(0x7eb50001,CRAWLSPACE[0],arwic_mines::GLYPH),
                                        (0x92bd0001,DANK[0],windmill::GLYPH),(0x92bd0001,CRAWLSPACE[0],windmill::GLYPH),
                                        (0x61b20001,TANGLEFOOT[0],esper_camp::GLYPH)] {
            let spawn=Placement {cell,coordinates:[10.,10.,0.],heading:0};
            let items: Vec<_>=(0..1000).filter_map(|_|drop_for(species,spawn,&mut seed)).collect();
            assert!((40..180).contains(&items.len()), "cell {cell:x} drops {}", items.len());
            assert!(items.iter().all(|i|i.definition==glyph));
            let mut unchanged=seed;
            assert!(drop_for(0x47000001,spawn,&mut unchanged).is_none());
            assert!(drop_for(species,Placement{cell:cell|0x100,..spawn},&mut unchanged).is_none());
            assert_eq!(unchanged,seed);
        }
    }
}
