//! Checks a downloaded file before it is used: its SHA-256 against the manifest and its minisign
//! signature against the team's public key. The key and the signatures use the format of the
//! Tauri updater (base64 of the minisign .pub and .sig files), so one key signs both the launcher
//! and the game packages.

use base64::Engine;
use minisign_verify::{PublicKey, Signature};
use sha2::{Digest, Sha256};
use std::fs::File;
use std::io::Read;
use std::path::Path;

use crate::failure::{code, Failure, Result};

pub fn sha256_file(path: &Path) -> Result<String> {
    let mut file = File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0u8; 1 << 20];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(hex(&hasher.finalize()))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn check_sha256(path: &Path, expected: &str) -> Result<()> {
    let actual = sha256_file(path)?;
    if actual.eq_ignore_ascii_case(expected) {
        Ok(())
    } else {
        Err(Failure::new(code::BAD_CHECKSUM, format!("{}: {actual}", path.display())))
    }
}

fn decode_text(base64_text: &str) -> Option<String> {
    let bytes = base64::engine::general_purpose::STANDARD.decode(base64_text.trim()).ok()?;
    String::from_utf8(bytes).ok()
}

/// The public key of a build, or nothing when the build has none (a development build: updates
/// are then refused instead of trusted).
pub fn public_key(base64_key: &str) -> Option<PublicKey> {
    let text = decode_text(base64_key)?;
    PublicKey::decode(&text).ok()
}

pub fn check_signature(path: &Path, signature_base64: &str, key: &PublicKey) -> Result<()> {
    let text = decode_text(signature_base64)
        .ok_or_else(|| Failure::new(code::BAD_SIGNATURE, "the signature is not base64"))?;
    let signature =
        Signature::decode(&text).map_err(|e| Failure::new(code::BAD_SIGNATURE, e.to_string()))?;
    let mut verifier = key
        .verify_stream(&signature)
        .map_err(|e| Failure::new(code::BAD_SIGNATURE, e.to_string()))?;
    let mut file = File::open(path)?;
    let mut buffer = vec![0u8; 1 << 20];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        verifier.update(&buffer[..read]);
    }
    verifier
        .finalize()
        .map_err(|e| Failure::new(code::BAD_SIGNATURE, format!("{}: {e}", path.display())))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use std::io::Cursor;

    /// A throwaway key pair made for the test run: no real signing secret is ever in the
    /// repository. Returns the public key and the signature of data, both in the updater's form.
    pub(crate) fn sign(data: &[u8]) -> (String, String) {
        let pair = minisign::KeyPair::generate_unencrypted_keypair().unwrap();
        let public = pair.pk.to_box().unwrap().to_string();
        let signature = minisign::sign(Some(&pair.pk), &pair.sk, Cursor::new(data), None, None)
            .unwrap()
            .to_string();
        let b64 = base64::engine::general_purpose::STANDARD;
        (b64.encode(public), b64.encode(signature))
    }

    #[test]
    fn sha256_matches_and_mismatches() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("f");
        std::fs::write(&file, b"test").unwrap();
        let digest = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
        assert_eq!(sha256_file(&file).unwrap(), digest);
        check_sha256(&file, &digest.to_uppercase()).unwrap();
        assert_eq!(check_sha256(&file, &"0".repeat(64)).unwrap_err().code, code::BAD_CHECKSUM);
    }

    #[test]
    fn signatures_are_checked() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("package");
        std::fs::write(&file, b"the package").unwrap();
        let (key, signature) = sign(b"the package");
        let key = public_key(&key).unwrap();
        check_signature(&file, &signature, &key).unwrap();

        std::fs::write(&file, b"the package, changed").unwrap();
        assert_eq!(check_signature(&file, &signature, &key).unwrap_err().code, code::BAD_SIGNATURE);

        let (other_key, _) = sign(b"x");
        std::fs::write(&file, b"the package").unwrap();
        let other_key = public_key(&other_key).unwrap();
        assert!(check_signature(&file, &signature, &other_key).is_err());
        assert!(check_signature(&file, "not base64!", &key).is_err());
    }

    #[test]
    fn a_placeholder_key_is_no_key() {
        assert!(public_key("SET_BY_THE_RELEASE_BUILD").is_none());
    }
}
