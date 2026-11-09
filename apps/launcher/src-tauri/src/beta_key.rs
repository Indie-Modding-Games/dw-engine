//! The closed beta's key: the one key the beta gateway (apps/beta-gateway) asks for before it shows
//! a release. It opens the releases only, never the repository.
//!
//! The release build carries it: release.yml sets DWE_LAUNCHER_BETA_KEY from the DWE_BETA_KEY
//! secret, so a tester never types it. It is a light barrier, not a secret: the launcher is handed
//! out, and anyone who has it can read the key out of the program.
//!
//! The key a run sends, first found wins:
//!   1. DWE_LAUNCHER_BETA_KEY in the environment (development and tests);
//!   2. the key built in;
//!   3. only in a hand build without one: the key an older launcher saved in the system keyring
//!      (service "dwe-launcher", account "beta-key").
//!
//! A launcher with a key built in removes that keyring entry once (remove_stored).
//!
//! The key is never logged, never shown and never sent to the page: no type here prints it.

use crate::failure::{code, Failure, Result};

/// The key for one run, and the one a build carries (read when it is compiled).
pub const KEY_ENV: &str = "DWE_LAUNCHER_BETA_KEY";

/// The key the release build carries; none in a hand build.
pub const BUILT_IN: Option<&str> = match option_env!("DWE_LAUNCHER_BETA_KEY") {
    Some(key) if !key.is_empty() => Some(key),
    _ => None,
};

const SERVICE: &str = "dwe-launcher";
const ACCOUNT: &str = "beta-key";

/// Where older launchers kept the key the tester pasted.
pub trait KeyStore: Send + Sync {
    fn get(&self) -> Result<Option<String>>;
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

    fn clear(&self) -> Result<()> {
        let entry = keyring::Entry::new(SERVICE, ACCOUNT).map_err(keyring_failure)?;
        match entry.delete_credential() {
            Ok(()) | Err(keyring::Error::NoEntry) => Ok(()),
            Err(error) => Err(keyring_failure(error)),
        }
    }
}

/// The key this run sends (see the module's comment for the order).
pub struct BetaKey {
    /// The environment's key, else the build's.
    fixed: Option<String>,
    /// Asked only when there is no fixed key.
    stored: Box<dyn KeyStore>,
}

impl BetaKey {
    pub fn new(from_env: Option<String>, built_in: Option<&str>, stored: Box<dyn KeyStore>) -> Self {
        let fixed = from_env.as_deref().and_then(clean).or_else(|| built_in.and_then(clean));
        Self { fixed, stored }
    }

    /// This build's key: the environment's, the built-in one, else the system keyring's.
    pub fn from_build() -> Self {
        Self::new(std::env::var(KEY_ENV).ok(), BUILT_IN, Box::new(Keyring))
    }

    pub fn get(&self) -> Option<String> {
        self.fixed.clone().or_else(|| self.stored.get().ok().flatten().as_deref().and_then(clean))
    }
}

/// Removes the key an older launcher saved; quiet, a failure only goes to the log.
pub fn remove_stored(stored: &dyn KeyStore) {
    if let Err(failure) = stored.clear() {
        crate::log_line!("the old beta key entry was not removed: {}", failure.detail);
    }
}

/// Hides the run's key, should it ever reach a message (log::redact).
pub fn hide(text: &str) -> String {
    let from_env = std::env::var(KEY_ENV).ok();
    hide_keys(text, &[BUILT_IN, from_env.as_deref()])
}

/// The text with each key (of 8 characters or more: a shorter one is a development placeholder
/// that could be any word) replaced.
fn hide_keys(text: &str, keys: &[Option<&str>]) -> String {
    let keys = keys.iter().flatten().map(|k| k.trim()).filter(|k| k.len() >= 8);
    keys.fold(text.to_string(), |text, key| text.replace(key, "[beta key]"))
}

/// A key as it was given: trimmed; nothing when empty.
fn clean(key: &str) -> Option<String> {
    let key = key.trim();
    (!key.is_empty()).then(|| key.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    const BUILT: &str = "placeholder-built-in-key-0123456789";
    const OLD: &str = "placeholder-old-keyring-key-0123456789";

    /// A keyring in memory.
    struct Memory(std::sync::Mutex<Option<String>>);
    impl KeyStore for Memory {
        fn get(&self) -> Result<Option<String>> {
            Ok(self.0.lock().unwrap().clone())
        }
        fn clear(&self) -> Result<()> {
            *self.0.lock().unwrap() = None;
            Ok(())
        }
    }

    fn stored(key: Option<&str>) -> Box<Memory> {
        Box::new(Memory(std::sync::Mutex::new(key.map(str::to_string))))
    }

    /// A keyring that must not be asked.
    struct Untouched;
    impl KeyStore for Untouched {
        fn get(&self) -> Result<Option<String>> {
            panic!("the keyring was read although a key is built in")
        }
        fn clear(&self) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn the_environment_then_the_build_then_an_older_launchers_keyring() {
        let key = BetaKey::new(Some(" placeholder-env-key \n".into()), Some(BUILT), stored(Some(OLD)));
        assert_eq!(key.get().as_deref(), Some("placeholder-env-key"));
        let key = BetaKey::new(Some("  ".into()), Some(BUILT), Box::new(Untouched));
        assert_eq!(key.get().as_deref(), Some(BUILT));
        let key = BetaKey::new(None, Some(BUILT), Box::new(Untouched));
        assert_eq!(key.get().as_deref(), Some(BUILT));
        // A hand build: the keyring's key, else none.
        assert_eq!(BetaKey::new(None, None, stored(Some(OLD))).get().as_deref(), Some(OLD));
        assert_eq!(BetaKey::new(None, Some(""), stored(Some(OLD))).get().as_deref(), Some(OLD));
        assert_eq!(BetaKey::new(None, None, stored(Some(" "))).get(), None);
        assert_eq!(BetaKey::new(None, None, stored(None)).get(), None);
    }

    #[test]
    fn the_old_keyring_entry_is_removed() {
        let store = stored(Some(OLD));
        remove_stored(store.as_ref());
        assert_eq!(store.get().unwrap(), None);
        remove_stored(store.as_ref());
    }

    #[test]
    fn the_page_never_asks_for_the_key_and_tells_an_old_launcher_to_update() {
        let page = [
            include_str!("../../ui/index.html"),
            include_str!("../../ui/js/app.js"),
            include_str!("../../ui/js/api.js"),
            include_str!("../../ui/js/settings.js"),
        ]
        .concat();
        for prompt in ["save_token", "clear_token", "saveToken", "tokenInput", "\"password\"", "\"account\""] {
            assert!(!page.contains(prompt), "the page still has {prompt}");
        }
        // The gateway's 401 (failure "auth") says to update or reinstall the launcher, never to
        // type a key.
        let texts = include_str!("../../ui/js/i18n.js");
        let auth: Vec<&str> = texts.lines().filter(|l| l.trim_start().starts_with("auth: ")).collect();
        assert_eq!(auth.len(), 2, "{auth:?}");
        assert!(auth[0].contains("Actualiza el lanzador o vuelve a instalarlo desde la última versión"), "{}", auth[0]);
        assert!(auth[1].contains("Update the launcher or reinstall it from the latest release"), "{}", auth[1]);
        assert!(auth.iter().all(|l| !l.contains("clave") && !l.contains("key")));
    }

    #[test]
    fn the_key_is_hidden_in_messages() {
        let keys = [Some(BUILT), None, Some("short")];
        assert_eq!(hide_keys(&format!("sent {BUILT} to x"), &keys), "sent [beta key] to x");
        assert_eq!(hide_keys("a short word", &keys), "a short word");
        assert_eq!(hide("nothing to hide"), "nothing to hide");
    }
}
