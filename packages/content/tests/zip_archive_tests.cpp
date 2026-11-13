#include <dwe/content/zip_archive.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/zip_writer.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using dwe::content::ZipArchive;
using dwe::content::ZipLimits;
using dwe::test_support::TempFolder;
using dwe::test_support::ZipFileSpec;

// lodepng allocates its output with malloc.
struct FreeBuffer {
  void operator()(unsigned char* buffer) const {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): lodepng's buffer needs free.
    std::free(buffer);
  }
};

// Bytes as real deflate, as zip tools compress them.
std::string deflated(std::string_view data) {
  unsigned char* out = nullptr;
  std::size_t size = 0;
  LodePNGCompressSettings settings;
  lodepng_compress_settings_init(&settings);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): lodepng reads unsigned chars.
  const auto* in = reinterpret_cast<const unsigned char*>(data.data());
  REQUIRE(lodepng_deflate(&out, &size, in, data.size(), &settings) == 0);
  const std::unique_ptr<unsigned char, FreeBuffer> owner{out};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): back to chars.
  return {reinterpret_cast<const char*>(out), size};
}

std::string text(const std::vector<std::byte>& bytes) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the test files are text.
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Opens an archive of the files, written in a new folder.
dwe::core::Result<ZipArchive> openZip(const TempFolder& folder,
                                      const std::vector<ZipFileSpec>& files,
                                      const ZipLimits& limits = {}) {
  const std::filesystem::path path = folder.path() / "test.zip";
  dwe::test_support::writeZip(path, files);
  return ZipArchive::open(path, limits);
}

// Why an archive of the files is refused; fails the test if it is not.
std::string refusal(const std::vector<ZipFileSpec>& files, const ZipLimits& limits = {}) {
  const TempFolder folder;
  auto zip = openZip(folder, files, limits);
  REQUIRE_FALSE(zip.has_value());
  return zip.error().message;
}

// A file of the bytes, really deflated.
ZipFileSpec compressed(std::string name, const std::string& data) {
  ZipFileSpec spec = dwe::test_support::zipFile(std::move(name), data);
  spec.stored = deflated(data);
  return spec;
}

ZipFileSpec stored(std::string name, std::string data) {
  ZipFileSpec spec = dwe::test_support::zipFile(std::move(name), std::move(data));
  spec.method = 0;
  return spec;
}

ZipFileSpec file(std::string name, std::string data = "x") {
  return dwe::test_support::zipFile(std::move(name), std::move(data));
}

} // namespace

TEST_CASE("a .zip's stored and deflate files are read in place, its folders dropped") {
  const TempFolder folder;
  // Text that compresses as text does, a few times and no more.
  std::string big;
  std::uint32_t seed = 1;
  while (big.size() < 200'000) {
    seed = seed * 1'103'515'245U + 12'345U;
    big += static_cast<char>('a' + ((seed >> 16U) % 26U));
  }
  std::vector<ZipFileSpec> files{
      dwe::test_support::zipFolder("clock/"),
      stored("clock/addon.yaml", "id: clock\n"),
      file("clock/stored_blocks.txt", "in deflate stored blocks"),
      compressed("clock/big.txt", big),
      file("clock/empty.txt", ""),
      // As MS-DOS tools write it: no Unix mode.
      file("clock/dos.txt", "dos"),
  };
  files.back().madeBy = 0x0014;
  files.back().attributes = 0x20;
  auto zip = openZip(folder, files);
  REQUIRE_MESSAGE(zip.has_value(), zip.error().message);
  REQUIRE(zip->entries().size() == 5);
  CHECK(zip->find("clock/") == nullptr);
  for (const ZipFileSpec& spec : files) {
    if (spec.name.ends_with('/')) {
      continue;
    }
    CAPTURE(spec.name);
    const auto* entry = zip->find(spec.name);
    REQUIRE(entry != nullptr);
    CHECK(entry->size == spec.data.size());
    auto read = zip->read(*entry);
    REQUIRE_MESSAGE(read.has_value(), read.error().message);
    CHECK(text(*read) == spec.data);
  }
  // Really compressed.
  CHECK(zip->find("clock/big.txt")->storedSize < big.size());
}

TEST_CASE("a .zip whose paths could leave its folder is refused whole") {
  for (const std::string& name : std::vector<std::string>{
           "/etc/passwd", "../evil.lua", "clock/../../evil.lua", "clock/./x.txt", "clock//x.txt",
           "clock\\..\\evil.lua", "C:/evil.lua", std::string{"clock/a\x01.txt"}, std::string{}}) {
    CAPTURE(name);
    CHECK(refusal({file("clock/addon.yaml"), file(name)}).contains("not a safe path"));
  }
  CHECK(refusal({file("a/a/a/a/a/a/a/a/a/a/a/a/a.txt")}).contains("more than 12 folders deep"));
  const TempFolder folder;
  CHECK(openZip(folder, {file("a/a/a/a/a/a/a/a/a/a/a/a.txt")}).has_value());
}

TEST_CASE("a .zip with a link, a special file or an encrypted file is refused whole") {
  ZipFileSpec link = file("clock/passwd", "/etc/passwd");
  link.attributes = 0120777U << 16U;
  CHECK(refusal({file("clock/addon.yaml"), link}).contains("link or another special file"));
  ZipFileSpec fifo = file("clock/fifo", "");
  fifo.attributes = 0010644U << 16U;
  CHECK(refusal({fifo}).contains("special file"));
  // A Windows reparse point (a link or junction), as an NTFS tool marks it.
  ZipFileSpec junction = file("clock/junction");
  junction.madeBy = 0x0a14;
  junction.attributes = 0x0400;
  CHECK(refusal({junction}).contains("special file"));

  ZipFileSpec encrypted = file("clock/addon.yaml");
  encrypted.flags = 0x0001;
  CHECK(refusal({encrypted}).contains("is encrypted"));
  ZipFileSpec aes = file("clock/addon.yaml");
  aes.method = 99;
  CHECK(refusal({aes}).contains("is encrypted"));
  ZipFileSpec strong = file("clock/addon.yaml");
  strong.flags = 0x0040;
  CHECK(refusal({strong}).contains("is encrypted"));
  ZipFileSpec bzip2 = file("clock/addon.yaml");
  bzip2.method = 12;
  CHECK(refusal({bzip2}).contains("does not read (12)"));
}

TEST_CASE("a .zip with too many files, too many bytes or a zip bomb is refused whole") {
  ZipLimits limits;
  limits.maxEntries = 3;
  CHECK(refusal({file("a"), file("b"), file("c"), file("d")}, limits).contains("more than 3"));
  {
    const TempFolder folder;
    CHECK(openZip(folder, {file("a"), file("b"), file("c")}, limits).has_value());
  }

  limits = {};
  limits.maxArchiveBytes = 100;
  CHECK(refusal({file("a", std::string(200, 'x'))}, limits).contains("bigger than 100 bytes"));

  // What the archive declares counts before anything is read.
  limits = {};
  limits.maxExpandedBytes = 1000;
  CHECK(refusal({file("a", std::string(600, 'x')), file("b", std::string(600, 'y'))}, limits)
            .contains("expands to more than 1000"));

  // Ten megabytes of zeros deflate to a few kilobytes: far past any real file's ratio.
  const std::string zeros(10 * 1024 * 1024, '\0');
  CHECK(refusal({compressed("bomb.bin", zeros)}).contains("expands"));
  // A small file of one repeated byte is fine.
  const std::string spaces(4000, ' ');
  const TempFolder folder;
  CHECK(openZip(folder, {compressed("a.txt", spaces)}).has_value());
}

TEST_CASE("a .zip with one file twice, or that is not a .zip, is refused") {
  CHECK(refusal({file("clock/addon.yaml"), file("clock/addon.yaml")}).contains("twice"));
  const TempFolder folder;
  folder.write("not.zip", "this is text, not an archive");
  auto opened = ZipArchive::open(folder.path() / "not.zip");
  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().message.contains("not a .zip"));
  CHECK_FALSE(ZipArchive::open(folder.path() / "missing.zip").has_value());
#ifndef _WIN32
  // A link to an archive is not followed.
  dwe::test_support::writeZip(folder.path() / "real.zip", std::vector{file("a")});
  std::error_code error;
  std::filesystem::create_symlink(folder.path() / "real.zip", folder.path() / "link.zip", error);
  REQUIRE_FALSE(error);
  auto linked = ZipArchive::open(folder.path() / "link.zip");
  REQUIRE_FALSE(linked.has_value());
  CHECK(linked.error().message.contains("not a plain file"));
#endif
}

TEST_CASE("a .zip file that lies about its size or CRC fails to read, without a crash") {
  const TempFolder folder;
  const std::string zeros(1024 * 1024, '\0');
  // It declares 100 bytes and its deflate stream expands to a megabyte: the read stops past 100.
  ZipFileSpec liar = compressed("liar.bin", zeros);
  liar.size = 100;
  ZipFileSpec broken = file("broken.bin", "abc");
  broken.stored = std::string{"\xff\xff\xff"};
  auto zip = openZip(folder, {liar, broken});
  REQUIRE_MESSAGE(zip.has_value(), zip.error().message);
  auto lied = zip->read(*zip->find("liar.bin"));
  REQUIRE_FALSE(lied.has_value());
  CHECK(lied.error().message.contains("liar.bin is broken"));
  CHECK_FALSE(zip->read(*zip->find("broken.bin")).has_value());

  // A stored file with one byte changed after the archive was made.
  const std::filesystem::path path = folder.path() / "crc.zip";
  std::string bytes = dwe::test_support::zipArchive(std::vector{stored("crc.txt", "hello")});
  bytes[30 + std::string_view{"crc.txt"}.size()] = 'j';
  folder.write("crc.zip", bytes);
  auto changed = ZipArchive::open(path);
  REQUIRE(changed.has_value());
  auto read = changed->read(*changed->find("crc.txt"));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error().message.contains("CRC-32"));
}
