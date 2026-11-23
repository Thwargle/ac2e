// SPDX-License-Identifier: AGPL-3.0-only
//! Login policies at the private IPC boundary. Never log these credentials.
use ac2_net::{Credential, Logon};
use ac2_net::protocol::{Build, ProtocolError, ServerEvent};
use serde_json::Value;

pub(crate) fn logon(v: &Value, loopback: bool) -> Result<Logon, String> {
    let direct = match v["auth"].as_str().unwrap_or("account_service") {
        "account_service" => false,
        "gls_direct" => true,
        _ => return Err("Unknown login method".into()),
    };
    let raw_account = v["account"].as_str().ok_or("Missing account")?;
    if raw_account.is_empty() || raw_account.len() > 32
        || !raw_account.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_') {
        return Err("Account must contain 1-32 letters, digits or underscores".into());
    }
    // AC2E accounts are normalized; direct servers own their account-name policy.
    let account = if direct { raw_account.to_owned() } else { raw_account.to_ascii_lowercase() };
    let ticket = v["ticket"].as_str().unwrap_or("");
    let password = v["password"].as_str().unwrap_or("");
    let credential = if direct {
        if !ticket.is_empty() { return Err("Direct login cannot also use an account-service ticket".into()); }
        if password.is_empty() || password.chars().count() > 128 || password.chars().any(char::is_control) {
            return Err("Direct login requires a password of 1-128 characters without control characters".into());
        }
        // Legions gmClient::InitAuth: --glsticketdirect packs the supplied
        // narrow string into NetAuthenticator type 0x40000002. It is not a
        // GUN ticket (0x41000001), and has no AC2E hex-token requirement.
        Credential::Password(password.to_owned())
    } else {
        if !password.is_empty() { return Err("A password requires explicit direct-server login mode".into()); }
        if ticket.is_empty() {
            if !loopback { return Err("Remote servers require account-service authentication or explicit direct login".into()); }
            Credential::Account
        } else {
            if ticket.len() != 64 || !ticket.bytes().all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c)) {
                return Err("Invalid login ticket".into());
            }
            Credential::Password(ticket.to_owned())
        }
    };
    let version = v["version"].as_str().unwrap_or(if direct { "7160" } else { "7148" });
    if !matches!(version, "7148" | "7160" | "7230") {
        return Err("Client version must be 7148, 7160 or 7230".into());
    }
    Ok(Logon { version: version.into(), account, credential,
        connection_sequence: ac2_net::real_time() as u32, crypto: None })
}

pub(crate) fn decode(bytes: &[u8], build: Build) -> Result<ServerEvent, ProtocolError> {
    let result = ServerEvent::decode(bytes, build);
    // Pyrealium sends the admin CharacterSet even for newer advertised builds.
    // Both recovered layouts must consume the entire message; no bytes are
    // discarded to make an unknown server reply appear valid.
    if result.is_err() && bytes.starts_with(&0x00010055u32.to_le_bytes()) {
        return ServerEvent::decode(bytes, match build {
            Build::Admin7148 => Build::Legions7160,
            Build::Legions7160 => Build::Admin7148,
        });
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn direct_login_preserves_case_and_packs_the_native_gls_string() {
        let request = json!({"auth":"gls_direct", "account":"Mixed_Account", "password":"Short!"});
        let login = logon(&request, false).unwrap();
        let (version, auth, account, extra) = Logon::decode_payload(&login.payload()).unwrap();
        assert_eq!(version, "7160"); assert_eq!(auth, 0x40000002);
        assert_eq!(account, "Mixed_Account");
        assert_eq!(ac2_net::unpack_string(&extra, &mut 0, 512).unwrap(), b"Short!");
    }

    #[test]
    fn direct_login_is_explicit_and_does_not_weaken_ticket_login() {
        for request in [json!({"account":"test"}), json!({"account":"test","password":"Short!"}),
            json!({"account":"test","ticket":"Short!"}),
            json!({"auth":"unknown","account":"test"}),
            json!({"auth":"gls_direct","account":"test"}),
            json!({"auth":"gls_direct","account":"test","password":"a\nb"}),
            json!({"auth":"gls_direct","account":"test","password":"x".repeat(129)}),
            json!({"auth":"gls_direct","account":"test","password":"Short!","ticket":"a".repeat(64)})] {
            assert!(logon(&request, false).is_err());
        }
        let ticket = json!({"account":"Mixed_Account","ticket":"a".repeat(64)});
        let login = logon(&ticket, false).unwrap();
        assert_eq!(login.account,"mixed_account"); assert_eq!(login.version,"7148");
        assert!(matches!(login.credential, Credential::Password(_)));
        assert!(matches!(logon(&json!({"account":"local"}), true).unwrap().credential, Credential::Account));
    }

    #[test]
    fn both_recovered_character_lists_decode_without_ignoring_extra_data() {
        for legions in [false,true] {
            let mut bytes = 0x10055u32.to_le_bytes().to_vec();
            bytes.extend([0u32,0,0,5].iter().flat_map(|v|v.to_le_bytes()));
            ac2_net::pack_string(&mut bytes,0,b"Mixed_Account");
            bytes.extend(0u32.to_le_bytes());
            if legions {bytes.extend([1u32,2].iter().flat_map(|v|v.to_le_bytes()));}
            for build in [Build::Admin7148,Build::Legions7160] {
                assert!(matches!(decode(&bytes,build),Ok(ServerEvent::CharacterSet(_))));
                let mut invalid=bytes.clone();invalid.push(1);
                assert!(decode(&invalid,build).is_err());
            }
        }
    }
}
