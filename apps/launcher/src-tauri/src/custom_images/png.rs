//! A PNG's size, read from its header without decoding it, and the engine's rule for how much
//! larger a replacement may be (engine/assets image_replacement.hpp, replacementScale).

use serde::Serialize;

const SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
const HEADER_CHUNK: &[u8; 4] = b"IHDR";
/// The bytes up to the end of the width and height.
pub const HEADER_BYTES: usize = 24;
/// The largest factor a replacement may be larger by, the same on both axes.
pub const MAX_SCALE: u32 = 8;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}

/// The size a PNG's header gives, or nothing when the bytes are not a PNG.
pub fn size(bytes: &[u8]) -> Option<Size> {
    if bytes.len() < HEADER_BYTES || bytes[..8] != SIGNATURE || &bytes[12..16] != HEADER_CHUNK {
        return None;
    }
    let number = |at: usize| u32::from_be_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]]);
    let size = Size { width: number(16), height: number(20) };
    (size.width > 0 && size.height > 0).then_some(size)
}

/// How many times larger `image` is than `original`, when it is a whole number and the same on both
/// axes (the same aspect ratio); MAX_SCALE is checked apart, so a too large image says so.
pub fn whole_scale(original: Size, image: Size) -> Option<u32> {
    if !image.width.is_multiple_of(original.width) || !image.height.is_multiple_of(original.height) {
        return None;
    }
    let scale = image.width / original.width;
    (scale >= 1 && scale == image.height / original.height).then_some(scale)
}

#[cfg(test)]
pub mod tests {
    use super::*;

    /// The first bytes of a PNG of this size: all the header the launcher reads.
    pub fn header(width: u32, height: u32) -> Vec<u8> {
        let mut bytes = SIGNATURE.to_vec();
        bytes.extend_from_slice(&13u32.to_be_bytes());
        bytes.extend_from_slice(HEADER_CHUNK);
        bytes.extend_from_slice(&width.to_be_bytes());
        bytes.extend_from_slice(&height.to_be_bytes());
        bytes.extend_from_slice(&[8, 6, 0, 0, 0]);
        bytes
    }

    #[test]
    fn the_size_comes_from_the_header() {
        assert_eq!(size(&header(768, 640)), Some(Size { width: 768, height: 640 }));
        assert_eq!(size(b"GIF89a not a png at all"), None);
        assert_eq!(size(&header(768, 640)[..20]), None);
    }

    #[test]
    fn a_replacement_is_a_whole_number_of_times_larger_on_both_axes() {
        let original = Size { width: 768, height: 640 };
        assert_eq!(whole_scale(original, original), Some(1));
        assert_eq!(whole_scale(original, Size { width: 1536, height: 1280 }), Some(2));
        assert_eq!(whole_scale(original, Size { width: 1536, height: 640 }), None);
        assert_eq!(whole_scale(original, Size { width: 1000, height: 833 }), None);
        assert_eq!(whole_scale(original, Size { width: 384, height: 320 }), None);
    }
}
