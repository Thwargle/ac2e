"""Name evidence outside the main creature/item template catalog."""
import re


# gmQualities 0x81xxxxxx, base-description singular StringInfo. Table lookups
# use the name variable (0xa8607cec) of the retail wrapper 0x2500035f.
QUALITY_NAMES = {
    "47000714": ("Lifestone", "2500010f"),
    "4700079b": ("Door", "25000105"),
    "4700079c": ("Door", "25000105"),
    "470007a3": ("Door", "2500126f"),
    "470007de": ("Lifestone", "2500010c"),
    "47000805": ("Lifestone", "2500010d"),
    "47000818": ("Door", "25000d64"),
    "470010a0": ("Lost Wish Glyph", "250011ef"),
    "470013c3": ("Virindi Death Trap", None),  # Inline English string.
    "47001582": ("Menhir Glyph", "2500164e"),
    "47001587": ("Imbued Digging Tool", "25001650"),
}

# Both unnamed base classes are used by many differently named entrances.
PORTAL_CLASSES = {"47001451": "1f00031e", "47001454": "1f000d09"}


def journal_label(package):
    stem = package.removeprefix("Quest").replace("_", " ").replace("ofthe", "OfThe")
    words = re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])|(?<=[A-Za-z])(?=\d)",
                   " ", stem).split()
    substitutions = {"Armorsmiths": "Armorsmith's", "Of": "of", "The": "the"}
    return " ".join(substitutions.get(w, w) for w in words) + " (journal entry)"


# Angle-bracketed guide labels describe families whose members vary by town,
# continent, profession or weapon. Match actual template names on that page.
LUDWARD = "quest/linvak_ludwardsdream.html"
GEAR_PART = r"Gear Knight (?:Armor Plate|Etheric Motivator|Gear|Sword)"
LUDWARD_RECIPES = re.compile(r"(?:Basic|Excellent) " + GEAR_PART +
                             r"|Gear Knight (?:Coil|Linkage|Spring|Timing Pendulum)")
ITEM_FAMILIES = {
    "quest/omishan_clensetheburialmound.html": {
        "<itemname> of unaro": (r"(?:Board|Celestrum|Drum|Scepter|Staff) of Unaro", "Unaro reward variant")},
    "quest/osteth_portalbeaconblowout.html": {
        "<townname> portal shard": (r"(?:Arwic|Cavendo|Kehan|Millstone|Molwirth|Rithwic|Shoushi) Portal Shard",
                                    "Osteth portal shard variant")},
    "quest/knorr_atlansweapons.html": {
        "atlan's <weapontype> emblem": (r"Atlan .+ Emblem", "Atlan emblem variant"),
        "elegant atlan <weapontype>": (r"Atlan (?!.* Emblem$).+", "Atlan weapon base for Elegant reward")},
    "quest/shapershoardkeys.html": {
        "shaper's <resourcetype> hoard key of <continent>": (
            r"(?:Osteth|Omishan|Linvak) (?:Wood|Stone|Iron|Silver|Crystal) Hoard Key", "Shaper's Hoard key variant")},
    LUDWARD: {
        "basic gearknight <item>": (r"Basic " + GEAR_PART, "Basic Gear Knight component"),
        "excellent gear knight <item>": (r"Excellent " + GEAR_PART, "Excellent Gear Knight component"),
        "unknown <item>": (None, "Ingredients vary by Gear Knight recipe and profession")},
}
