//! Retail GUN friend requests and events. AC2E carries the native application
//! message inside an authenticated world-session event. The two outer IDs are
//! specific to AC2E, and are never sent to a stock GUN or Icefire endpoint.

pub mod element;
pub mod envelope;

use element::Elem;

pub const REQUEST_ID: u32 = 0x7eac_2001;
pub const EVENT_ID: u32 = 0x7eac_2002;
pub const MAX_MESSAGE: usize = 65_536;
// A private script call adds three u32s (message type, function ID and size)
// before the native GUN payload. The world transport fragments that whole call.
pub const MAX_WORLD_PAYLOAD: usize = MAX_MESSAGE - 12;
pub const MAX_LIST: usize = 256;
pub const MAX_NAME: usize = 63;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Friend {
    pub name: String,
    pub mode: u32,
    pub location: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Request {
    List {
        cx: u32,
    },
    Add {
        cx: u32,
        name: String,
    },
    Delete {
        cx: u32,
        name: String,
    },
    Flags {
        cx: u32,
        op: u32,
        word: u32,
        flags: u32,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    List {
        cx: u32,
        hr: u32,
        friends: Vec<Friend>,
    },
    Start {
        friends: Vec<Friend>,
    },
    Added {
        cx: u32,
        hr: u32,
    },
    Deleted {
        cx: u32,
        hr: u32,
    },
    Flags {
        cx: u32,
        hr: u32,
        word: u32,
        flags: u32,
    },
    Status(Friend),
}

fn hex(element: &Elem, key: &str) -> Result<u32, &'static str> {
    let value = element
        .get(key)
        .and_then(|v| v.strip_prefix("0x"))
        .ok_or("missing hex attribute")?;
    if value.is_empty() || value.len() > 8 || !value.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err("invalid hex attribute");
    }
    u32::from_str_radix(value, 16).map_err(|_| "invalid hex attribute")
}

fn decimal(element: &Elem, key: &str) -> Result<u32, &'static str> {
    let value = element.get(key).ok_or("missing numeric attribute")?;
    if !value.bytes().all(|b| b.is_ascii_digit()) {
        return Err("invalid decimal attribute");
    }
    value.parse().map_err(|_| "invalid decimal attribute")
}

fn name(value: &str) -> Result<&str, &'static str> {
    if value.is_empty() || value.chars().count() > MAX_NAME || value.chars().any(char::is_control) {
        return Err("invalid friend name");
    }
    Ok(value)
}

fn location(value: &str) -> Result<&str, &'static str> {
    if value.chars().count() > MAX_NAME || value.chars().any(char::is_control) {
        return Err("invalid friend location");
    }
    Ok(value)
}

fn leaf(element: &Elem) -> Result<(), &'static str> {
    if !element.children.is_empty() {
        return Err("unexpected child element");
    }
    Ok(())
}

fn user(element: &Elem) -> Result<String, &'static str> {
    Ok(name(element.get("User").ok_or("missing friend name")?)?.to_owned())
}

fn context(tag: &str, cx: u32) -> Elem {
    Elem::new(tag).attr("Cx", format!("0x{cx:x}"))
}

fn result(tag: &str, cx: u32, hr: u32) -> Elem {
    context(tag, cx).attr("HR", format!("0x{hr:x}"))
}

fn encode_message(element: &Elem) -> Result<Vec<u8>, &'static str> {
    let payload = envelope::encode(element).map_err(|_| "invalid GUN message")?;
    if payload.len() > MAX_WORLD_PAYLOAD {
        return Err("GUN message exceeds world transport limit");
    }
    Ok(payload)
}

fn decode_message(payload: &[u8]) -> Result<Elem, &'static str> {
    Ok(envelope::decode(payload)
        .map_err(|_| "invalid GUN message")?
        .element)
}

impl Request {
    pub fn encode(&self) -> Result<Vec<u8>, &'static str> {
        let element = match self {
            Self::List { cx } => context("GetFriendsList", *cx),
            Self::Add { cx, name: value } => context("FriendAdd", *cx).attr("User", name(value)?),
            Self::Delete { cx, name: value } => {
                context("FriendDelete", *cx).attr("User", name(value)?)
            }
            Self::Flags {
                cx,
                op,
                word,
                flags,
            } => context("Flags", *cx)
                .attr("Op", op.to_string())
                .attr("Word", word.to_string())
                .attr("Flags", flags.to_string()),
        };
        encode_message(&element)
    }

    pub fn decode(payload: &[u8]) -> Result<Self, &'static str> {
        let element = decode_message(payload)?;
        leaf(&element)?;
        let cx = hex(&element, "Cx")?;
        match element.name.as_str() {
            "GetFriendsList" => Ok(Self::List { cx }),
            "FriendAdd" => Ok(Self::Add {
                cx,
                name: user(&element)?,
            }),
            "FriendDelete" => Ok(Self::Delete {
                cx,
                name: user(&element)?,
            }),
            "Flags" => Ok(Self::Flags {
                cx,
                op: decimal(&element, "Op")?,
                word: decimal(&element, "Word")?,
                flags: decimal(&element, "Flags")?,
            }),
            _ => Err("unknown GUN friends request"),
        }
    }
}

impl Friend {
    fn to_element(&self, tag: &str) -> Result<Elem, &'static str> {
        let element = Elem::new(tag)
            .attr("User", name(&self.name)?)
            .attr("Mode", self.mode.to_string());
        // Captured incremental FriendStat events omit an empty Location. Roster
        // entries use FriendStatus and include Location even when empty.
        if tag == "FriendStat" && self.location.is_empty() {
            Ok(element)
        } else {
            Ok(element.attr("Location", location(&self.location)?))
        }
    }

    fn from_element(element: &Elem) -> Result<Self, &'static str> {
        leaf(element)?;
        let loc = match element.get("Location") {
            Some(value) => location(value)?,
            None if element.name == "FriendStat" => "",
            None => return Err("missing roster location"),
        };
        Ok(Self {
            name: user(element)?,
            mode: decimal(element, "Mode")?,
            location: loc.to_owned(),
        })
    }
}

fn roster(element: Elem, friends: &[Friend]) -> Result<Elem, &'static str> {
    if friends.len() > MAX_LIST {
        return Err("too many friends");
    }
    let mut element = element.attr("Cnt", friends.len().to_string());
    element.children = friends
        .iter()
        .map(|f| f.to_element("FriendStatus"))
        .collect::<Result<_, _>>()?;
    Ok(element)
}

fn decode_roster(element: &Elem) -> Result<Vec<Friend>, &'static str> {
    if decimal(element, "Cnt")? as usize != element.children.len() {
        return Err("invalid roster count");
    }
    element
        .children
        .iter()
        .map(|entry| {
            if entry.name != "FriendStatus" {
                return Err("invalid roster entry");
            }
            Friend::from_element(entry)
        })
        .collect()
}

impl Event {
    pub fn encode(&self) -> Result<Vec<u8>, &'static str> {
        let element = match self {
            Self::Added { cx, hr } => result("FriendAddRes", *cx, *hr),
            Self::Deleted { cx, hr } => result("FriendDeleteRes", *cx, *hr),
            Self::Flags {
                cx,
                hr,
                word,
                flags,
            } => result("FlagsRes", *cx, *hr)
                .attr("WhichFlag", word.to_string())
                .attr("Flags", flags.to_string()),
            Self::List { cx, hr, friends } => {
                roster(result("GetFriendsListRes", *cx, *hr), friends)?
            }
            Self::Start { friends } => roster(Elem::new("FStart"), friends)?,
            Self::Status(friend) => friend.to_element("FriendStat")?,
        };
        encode_message(&element)
    }

    pub fn decode(payload: &[u8]) -> Result<Self, &'static str> {
        let element = decode_message(payload)?;
        match element.name.as_str() {
            "FriendAddRes" | "FriendDeleteRes" => {
                leaf(&element)?;
                let cx = hex(&element, "Cx")?;
                let hr = hex(&element, "HR")?;
                if element.name == "FriendAddRes" {
                    Ok(Self::Added { cx, hr })
                } else {
                    Ok(Self::Deleted { cx, hr })
                }
            }
            "GetFriendsListRes" => Ok(Self::List {
                cx: hex(&element, "Cx")?,
                hr: hex(&element, "HR")?,
                friends: decode_roster(&element)?,
            }),
            "FStart" => Ok(Self::Start {
                friends: decode_roster(&element)?,
            }),
            "FriendStat" => Ok(Self::Status(Friend::from_element(&element)?)),
            "FlagsRes" => {
                leaf(&element)?;
                Ok(Self::Flags {
                    cx: hex(&element, "Cx")?,
                    hr: hex(&element, "HR")?,
                    word: decimal(&element, "WhichFlag")?,
                    flags: decimal(&element, "Flags")?,
                })
            }
            _ => Err("unknown GUN friends event"),
        }
    }
}

#[cfg(test)]
mod tests;
