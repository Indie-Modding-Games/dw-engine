//! The closed beta's key: the one key the team hands every tester, which the beta gateway
//! (services/beta-gateway) asks for before it shows a release. Kept in the system keyring (Secret
//! Service on Linux, the credential manager on Windows, the keychain on macOS), never in a file or
//! a log. It opens the releases only, never the repository.

use crate::failure::{code, Failure, Result};

const SERVICE: &str = "dwe-launcher";
const ACCOUNT: &str = "beta-key";

pub trait KeyStore: Send + Sync {
    fn get(&self) -> Result<Option<String>>;
    fn set(&self, key: &str) -> Result<()>;
    fn clear(&self) -> Result<()>;
}

pub struct Keyring;

fn keyring_failure(error: keyring::Error) -> Failure {
    Failure::new(code::KEYRING, error.to_string())
}

impl KeyStore for Keyring {
    fn get(&self) -> Result<Option<String>> {
        let entry = keyring::Entry::new(SERVICE, ACCOUNT).map_err(keyring_failure)?;
        match entry.get_password() {
            Ok(key) => Ok(Some(key)),
            Err(keyring::Error::NoEntry) => Ok(None),
            Err(error) => Err(keyring_failure(error)),
        }
    }

    fn set(&self, key: &str) -> Result<()> {
        keyring::Entry::new(SERVICE, ACCOUNT).and_then(|e| e.set_password(key)).map_err(keyring_failure)
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

impl KeyStore for Memory {
    fn get(&self) -> Result<Option<String>> {
        Ok(self.0.lock().unwrap().clone())
    }
    fn set(&self, key: &str) -> Result<()> {
        *self.0.lock().unwrap() = Some(key.to_string());
        Ok(())
    }
    fn clear(&self) -> Result<()> {
        *self.0.lock().unwrap() = None;
        Ok(())
    }
}

/// A key as the tester pasted it: trimmed; nothing when empty.
pub fn clean(key: &str) -> Option<String> {
    let key = key.trim();
    (!key.is_empty()).then(|| key.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_memory_store_and_cleaning() {
        let store = Memory::default();
        assert_eq!(store.get().unwrap(), None);
        store.set("beta-x").unwrap();
        assert_eq!(store.get().unwrap().as_deref(), Some("beta-x"));
        store.clear().unwrap();
        assert_eq!(store.get().unwrap(), None);
        assert_eq!(clean("  beta-y \n").as_deref(), Some("beta-y"));
        assert_eq!(clean("   "), None);
    }
}
