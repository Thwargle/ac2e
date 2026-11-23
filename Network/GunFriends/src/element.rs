use crate::MAX_LIST;

const MAX_DEPTH: usize = 4;

/// One element of the wire: a name, attributes in order, and flat children.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Elem {
    pub name: String,
    pub attrs: Vec<(String, String)>,
    pub children: Vec<Elem>,
}

impl Elem {
    pub fn new(name: &str) -> Self {
        Self {
            name: name.to_owned(),
            ..Self::default()
        }
    }
    pub fn attr(mut self, key: &str, value: impl Into<String>) -> Self {
        self.attrs.push((key.to_owned(), value.into()));
        self
    }
    pub fn child(mut self, child: Elem) -> Self {
        self.children.push(child);
        self
    }
    pub fn get(&self, key: &str) -> Option<&str> {
        self.attrs
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.as_str())
    }
    pub fn encode(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }
    fn write(&self, out: &mut String) {
        out.push('<');
        out.push_str(&self.name);
        for (key, value) in &self.attrs {
            out.push(' ');
            out.push_str(key);
            out.push_str("=\"");
            for c in value.chars() {
                match c {
                    '&' => out.push_str("&amp;"),
                    '<' => out.push_str("&lt;"),
                    '>' => out.push_str("&gt;"),
                    '"' => out.push_str("&quot;"),
                    c if (c as u32) < 0x20 => out.push_str(&format!("&#{};", c as u32)),
                    c => out.push(c),
                }
            }
            out.push('"');
        }
        if self.children.is_empty() {
            out.push_str("/>");
        } else {
            out.push('>');
            for child in &self.children {
                child.write(out);
            }
            out.push_str("</");
            out.push_str(&self.name);
            out.push('>');
        }
    }
    /// Parses one element (no declaration, comments or text between elements).
    pub fn parse(text: &str) -> Result<Elem, &'static str> {
        let mut parser = Parser {
            bytes: text.as_bytes(),
            at: 0,
        };
        parser.space();
        let elem = parser.element(0)?;
        parser.space();
        if parser.at != parser.bytes.len() {
            return Err("trailing data");
        }
        Ok(elem)
    }
}

struct Parser<'a> {
    bytes: &'a [u8],
    at: usize,
}

impl Parser<'_> {
    fn space(&mut self) {
        while self.bytes.get(self.at).is_some_and(u8::is_ascii_whitespace) {
            self.at += 1;
        }
    }
    fn name(&mut self) -> Result<String, &'static str> {
        let start = self.at;
        while self
            .bytes
            .get(self.at)
            .is_some_and(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'.'))
        {
            self.at += 1;
        }
        if self.at == start || self.at - start > 64 {
            return Err("bad name");
        }
        Ok(String::from_utf8_lossy(&self.bytes[start..self.at]).into_owned())
    }
    fn expect(&mut self, byte: u8) -> Result<(), &'static str> {
        if self.bytes.get(self.at) == Some(&byte) {
            self.at += 1;
            Ok(())
        } else {
            Err("unexpected byte")
        }
    }
    fn element(&mut self, depth: usize) -> Result<Elem, &'static str> {
        if depth > MAX_DEPTH {
            return Err("too deep");
        }
        self.expect(b'<')?;
        let mut elem = Elem::new(&self.name()?);
        loop {
            self.space();
            match self.bytes.get(self.at) {
                Some(b'/') => {
                    self.at += 1;
                    self.expect(b'>')?;
                    return Ok(elem);
                }
                Some(b'>') => {
                    self.at += 1;
                    break;
                }
                Some(_) => {
                    if elem.attrs.len() >= 16 {
                        return Err("too many attributes");
                    }
                    let key = self.name()?;
                    if elem.get(&key).is_some() {
                        return Err("duplicate attribute");
                    }
                    self.space();
                    self.expect(b'=')?;
                    self.space();
                    let value = self.value()?;
                    elem.attrs.push((key, value));
                }
                None => return Err("truncated"),
            }
        }
        loop {
            self.space();
            if self.bytes.get(self.at) == Some(&b'<') && self.bytes.get(self.at + 1) == Some(&b'/')
            {
                self.at += 2;
                let close = self.name()?;
                self.space();
                self.expect(b'>')?;
                return if close == elem.name {
                    Ok(elem)
                } else {
                    Err("mismatched close")
                };
            }
            if elem.children.len() >= MAX_LIST {
                return Err("too many children");
            }
            elem.children.push(self.element(depth + 1)?);
        }
    }
    fn value(&mut self) -> Result<String, &'static str> {
        let quote = *self.bytes.get(self.at).ok_or("truncated")?;
        if quote != b'"' && quote != b'\'' {
            return Err("unquoted value");
        }
        self.at += 1;
        let mut out = Vec::new();
        loop {
            let byte = *self.bytes.get(self.at).ok_or("truncated")?;
            self.at += 1;
            if byte == quote {
                break;
            }
            if byte == b'<' {
                return Err("bad value");
            }
            if byte != b'&' {
                out.push(byte);
                continue;
            }
            let end = self.bytes[self.at..]
                .iter()
                .position(|&b| b == b';')
                .ok_or("bad entity")?;
            let entity = &self.bytes[self.at..self.at + end];
            self.at += end + 1;
            let ch = match entity {
                b"amp" => '&',
                b"lt" => '<',
                b"gt" => '>',
                b"quot" => '"',
                b"apos" => '\'',
                [b'#', b'x', rest @ ..] => char::from_u32(
                    u32::from_str_radix(std::str::from_utf8(rest).map_err(|_| "bad entity")?, 16)
                        .map_err(|_| "bad entity")?,
                )
                .ok_or("bad entity")?,
                [b'#', rest @ ..] => char::from_u32(
                    std::str::from_utf8(rest)
                        .map_err(|_| "bad entity")?
                        .parse::<u32>()
                        .map_err(|_| "bad entity")?,
                )
                .ok_or("bad entity")?,
                _ => return Err("bad entity"),
            };
            let mut buffer = [0u8; 4];
            out.extend_from_slice(ch.encode_utf8(&mut buffer).as_bytes());
        }
        String::from_utf8(out).map_err(|_| "not UTF-8")
    }
}
