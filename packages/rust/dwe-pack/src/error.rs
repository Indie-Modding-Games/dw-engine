use std::path::PathBuf;

/// What can go wrong reading packs. The messages follow the engine's, so a pack that the game
/// refuses is refused here with the same reason.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{path}: {source}")]
    Io {
        path: PathBuf,
        #[source]
        source: std::io::Error,
    },
    #[error("{path}: {message}")]
    Yaml { path: PathBuf, message: String },
    #[error("{0}")]
    Invalid(String),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub(crate) fn io(path: impl Into<PathBuf>, source: std::io::Error) -> Self {
        Self::Io {
            path: path.into(),
            source,
        }
    }

    pub(crate) fn yaml(path: impl Into<PathBuf>, error: &serde_yaml_ng::Error) -> Self {
        Self::Yaml {
            path: path.into(),
            message: error.to_string(),
        }
    }

    pub(crate) fn invalid(message: impl Into<String>) -> Self {
        Self::Invalid(message.into())
    }
}
