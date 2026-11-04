//! The player's GitHub token, for a private repository during the closed beta: kept in the
//! system keyring (Secret Service on Linux, the credential manager on Windows, the keychain on
//! macOS), never in a file or a log.

use crate::failure::{code, Failure, Result};

const SERVICE: &str = "dwe-launcher";
const ACCOUNT: &str = "github-token";

pub trait TokenStore: Send + Sync {
    fn get(&self) -> Result<Option<String>>;
    fn set(&self, token: &str) -> Result<()>;
    fn clear(&self) -> Result<()>;
}

pub struct Keyring;

fn keyring_failure(error: keyring::Error) -> Failure {
    Failure::new(code::KEYRING, error.to_string())
}

impl TokenStore for Keyring {
    fn get(&self) -> Result<Option<String>> {
        let entry = keyring::Entry::new(SERVICE, ACCOUNT).map_err(keyring_failure)?;
        match entry.get_password() {
            Ok(token) => Ok(Some(token)),
            Err(keyring::Error::NoEntry) => Ok(None),
            Err(error) => Err(keyring_failure(error)),
        }
    }

    fn set(&self, token: &str) -> Result<()> {
        keyring::Entry::new(SERVICE, ACCOUNT).and_then(|e| e.set_password(token)).map_err(keyring_failure)
    }

    fn clear(&self) -> Result<()> {
        let entry = keyring::Entry::new(SERVICE, ACCOUNT).map_err(keyring_failure)?;
        match entry.delete_credential() {
            Ok(()) | Err(keyring::Error::NoEntry) => Ok(()),
            Err(error) => Err(keyring_failure(error)),
        }
    }
}

/// A store for tests: in memory only.
#[derive(Default)]
pub struct Memory(std::sync::Mutex<Option<String>>);

impl TokenStore for Memory {
    fn get(&self) -> Result<Option<String>> {
        Ok(self.0.lock().unwrap().clone())
    }
    fn set(&self, token: &str) -> Result<()> {
        *self.0.lock().unwrap() = Some(token.to_string());
        Ok(())
    }
    fn clear(&self) -> Result<()> {
        *self.0.lock().unwrap() = None;
        Ok(())
    }
}

/// A token as the player pasted it: trimmed; nothing when empty.
pub fn clean(token: &str) -> Option<String> {
    let token = token.trim();
    (!token.is_empty()).then(|| token.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_memory_store_and_cleaning() {
        let store = Memory::default();
        assert_eq!(store.get().unwrap(), None);
        store.set("ghp_x").unwrap();
        assert_eq!(store.get().unwrap().as_deref(), Some("ghp_x"));
        store.clear().unwrap();
        assert_eq!(store.get().unwrap(), None);
        assert_eq!(clean("  ghp_y \n").as_deref(), Some("ghp_y"));
        assert_eq!(clean("   "), None);
    }
}
