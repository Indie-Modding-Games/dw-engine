#include "effect_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/effect/efe_analysis.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>
#include <dwe/formats/effect/efe_yaml.hpp>
#include <dwe/formats/image/tim.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"
#include "image_extractors.hpp"
#include "model_extractors.hpp"
#include "model_textures.hpp"

namespace dwe::dwx {

namespace {

namespace fx = formats::effect;
using core::YamlNode;

constexpr std::string_view EffectPath = "ETCHI/EFEDAT.EFE";

YamlNode quotedNode(std::string text) {
    return YamlNode::scalar(std::move(text), YamlNode::ScalarStyle::Quoted);
}

YamlNode numberNode(std::int64_t value) {
    return YamlNode::scalar(std::to_string(value));
}

core::Result<OutputFile> yamlFile(const core::Result<assets::AssetId>& id, const YamlNode& node) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    const auto text = core::emitYaml(node);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const auto bytes = std::as_bytes(std::span{*text});
    return OutputFile{.id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

// Ranges of the bytes that are not the fill byte, from the given offset of the file on.
std::vector<std::string> nonFillRanges(std::span<const std::byte> bytes, std::size_t fileOffset) {
    std::vector<std::string> ranges;
    for (std::size_t at = 0; at < bytes.size();) {
        if (bytes[at] == fx::EfeFill) {
            ++at;
            continue;
        }
        std::size_t end = at;
        while (end < bytes.size() && bytes[end] != fx::EfeFill) {
            ++end;
        }
        ranges.push_back(std::format("{:#x}-{:#x}", fileOffset + at, fileOffset + end));
        at = end;
    }
    return ranges;
}

std::string joined(const std::vector<std::string>& parts) {
    std::string text;
    for (const auto& part : parts) {
        text += (text.empty() ? "" : ", ") + part;
    }
    return text;
}

// One slot of the file: its assets, and what the report should say about it.
class SlotExtractor {
public:
    SlotExtractor(Extraction& extraction, const GameTables* tables, std::string_view source, std::size_t slot,
                  std::size_t slotOffset)
        : extraction_(&extraction), tables_(tables), source_(source), slot_(slot), slotOffset_(slotOffset) {}

    void extract(std::span<const std::byte> bytes) {
        const auto header = fx::readEfeHeader(bytes);
        if (!header) {
            unknown(std::format("{:#x}-{:#x} not read: {}", slotOffset_, slotOffset_ + bytes.size(),
                                header.error().message));
            return;
        }
        const std::span<const std::byte> payload = fx::efePayload(bytes, *header);
        writeEffect(*header);
        writeScripts(payload, *header);
        writeTexture(payload, *header);
        if (header->tmdEnd > header->tmdStart) {
            writeModel(payload, *header);
        }
        checkPadding(bytes.subspan(fx::EfeHeaderSize + static_cast<std::size_t>(header->dataEnd)), *header);
    }

    [[nodiscard]] std::size_t instructions() const { return instructions_; }
    [[nodiscard]] std::size_t models() const { return models_; }

private:
    [[nodiscard]] std::size_t fileOffset(std::int32_t payloadOffset) const {
        return slotOffset_ + fx::EfeHeaderSize + static_cast<std::size_t>(payloadOffset);
    }

    void unknown(std::string text) {
        extraction_->unknowns.push_back(std::format("slot {}: {}", slot_, std::move(text)));
    }

    void note(std::string text) { extraction_->notes.push_back(std::format("slot {}: {}", slot_, std::move(text))); }

    void add(core::Result<OutputFile> output) {
        if (!output) {
            unknown(std::format("not written: {}", output.error().message));
            return;
        }
        extraction_->outputs.push_back(std::move(*output));
    }

    [[nodiscard]] std::uint32_t expectedId() const { return static_cast<std::uint32_t>(fx::EfeEffectIdBase + slot_); }

    void writeEffect(const fx::EfeHeader& header) {
        YamlNode root = YamlNode::mapping();
        root.set("slot", numberNode(static_cast<std::int64_t>(slot_)));
        const bool named = tables_ != nullptr && slot_ < tables_->ids.moves.size();
        root.set("move", named ? quotedNode(tables_->ids.moves[slot_]) : YamlNode::scalar("null"));
        root.set("header", fx::efeHeaderToYaml(header));
        root.set("uv_anim_count",
                 numberNode((header.uvAnimsEnd - header.uvAnims) / static_cast<std::int32_t>(fx::EfeUvAnimSize)));
        const bool matches = std::cmp_equal(header.effectId, expectedId());
        root.set("loads", YamlNode::scalar(matches ? "true" : "false"));
        if (!matches) {
            // BTL_setupLoadedEFEFile compares the stored id with the one it asked for and gives up on the slot.
            note(std::format("its effect id is {:#x}, not {:#x}: the original refuses to load it "
                             "(BTL_setupLoadedEFEFile), so the technique shows no effect; extracted as stored",
                             header.effectId, expectedId()));
        }
        add(yamlFile(names::effectAsset(tables_, slot_, "effect"), root));
    }

    void writeScripts(std::span<const std::byte> payload, const fx::EfeHeader& header) {
        const auto scripts = fx::decodeEfeScripts(payload, header);
        if (!scripts) {
            unknown(std::format("scripts {:#x}-{:#x} not decoded: {}", fileOffset(header.startScript),
                                fileOffset(header.uvAnims), scripts.error().message));
            return;
        }
        const auto analysis = fx::analyzeEfeScripts(payload, header, *scripts);
        if (!analysis) {
            unknown(std::format("data area {:#x}-{:#x} not followed: {}", fileOffset(0), fileOffset(header.startScript),
                                analysis.error().message));
            return;
        }
        // The analysis counts from the slot's base, as script.yaml does.
        for (const auto& issue : analysis->issues) {
            unknown(std::format("base {:#x}: {}", fileOffset(0), issue));
        }
        fx::EfeYamlIds ids;
        if (tables_ != nullptr) {
            ids.digimon = tables_->ids.digimon;
        }
        add(yamlFile(names::effectAsset(tables_, slot_, "script"),
                     fx::efeScriptToYaml(payload, header, *scripts, *analysis, ids)));
        instructions_ += scripts->instructions.size();
    }

    void writeTexture(std::span<const std::byte> payload, const fx::EfeHeader& header) {
        const std::span<const std::byte> bytes = payload.subspan(
            static_cast<std::size_t>(header.timStart), static_cast<std::size_t>(header.timEnd - header.timStart));
        const auto tim = formats::image::readTim(bytes);
        if (!tim) {
            unknown(std::format("TIM {:#x}-{:#x} not read: {}", fileOffset(header.timStart), fileOffset(header.timEnd),
                                tim.error().message));
            return;
        }
        if (tim->byteSize != bytes.size()) {
            unknown(std::format("{:#x}-{:#x} after the TIM", fileOffset(header.timStart) + tim->byteSize,
                                fileOffset(header.timEnd)));
        }
        tim_ = *tim;
        const auto id = names::effectAsset(tables_, slot_, "texture");
        if (!id) {
            unknown(std::format("texture not written: {}", id.error().message));
            return;
        }
        const auto added =
            addTimImage(*extraction_, *id, *tim,
                        TimPlace{.source = source_, .entry = slot_, .offset = fileOffset(header.timStart)});
        if (!added) {
            unknown(std::format("texture not written: {}", added.error().message));
        }
    }

    void writeModel(std::span<const std::byte> payload, const fx::EfeHeader& header) {
        const std::span<const std::byte> stored = payload.subspan(
            static_cast<std::size_t>(header.tmdStart), static_cast<std::size_t>(header.tmdEnd - header.tmdStart));
        const auto gaps = fx::efeModelGaps(stored);
        if (!gaps) {
            unknown(std::format("TMD {:#x}-{:#x} not read: {}", fileOffset(header.tmdStart), fileOffset(header.tmdEnd),
                                gaps.error().message));
            return;
        }
        for (const auto& gap : *gaps) {
            unknown(std::format("TMD bytes {:#x}-{:#x} that no part of it covers",
                                fileOffset(header.tmdStart) + gap.offset,
                                fileOffset(header.tmdStart) + gap.offset + gap.size));
        }
        // The model as the original draws it from cell 0: texture page and CLUT rewritten, the TIM moved there.
        std::vector<std::byte> placed(stored.begin(), stored.end());
        const fx::EfeVramCell cell = *fx::efeVramCell(0);
        if (const auto moved = fx::placeEfeModel(placed, cell); !moved) {
            unknown(std::format("TMD {:#x}-{:#x} not placed: {}", fileOffset(header.tmdStart),
                                fileOffset(header.tmdEnd), moved.error().message));
            return;
        }
        std::vector<NamedScene> scenes;
        if (tim_) {
            formats::image::Tim texture = *tim_;
            texture.imageRect.x = cell.pixelX;
            texture.imageRect.y = cell.pixelY;
            texture.clutRect.x = cell.clutX;
            texture.clutRect.y = cell.clutY;
            NamedScene scene{.name = "effect cell 0", .scene = {}};
            scene.scene.addTim(texture);
            scenes.push_back(std::move(scene));
        }
        auto model = buildStaticModel(placed, scenes, "the slot's TIM could not be read");
        if (!model) {
            unknown(std::format("model not built: {}", model.error().message));
            return;
        }
        for (auto& text : model->unknowns) {
            unknown(std::move(text));
        }
        for (auto& text : model->notes) {
            // Every model is textured from cell 0; the file's notes say so once.
            if (!text.starts_with("textured with")) {
                note(std::move(text));
            }
        }
        const auto id = names::effectAsset(tables_, slot_, "model");
        if (!id) {
            unknown(std::format("model not written: {}", id.error().message));
            return;
        }
        extraction_->outputs.push_back(OutputFile{.id = *id, .extension = "glb", .bytes = std::move(model->glb)});
        ++models_;
    }

    void checkPadding(std::span<const std::byte> padding, const fx::EfeHeader& header) {
        const auto stale = nonFillRanges(padding, fileOffset(header.dataEnd));
        if (!stale.empty()) {
            // The header bounds what the original reads; past dataEnd nothing points, so these are leftovers.
            note(std::format("bytes past the slot's data that are not fill and that no offset reaches: {}",
                             joined(stale)));
        }
    }

    Extraction* extraction_;
    const GameTables* tables_;
    std::string_view source_;
    std::size_t slot_;
    std::size_t slotOffset_;
    std::optional<formats::image::Tim> tim_;
    std::size_t instructions_ = 0;
    std::size_t models_ = 0;
};

} // namespace

bool handlesEffectFile(std::string_view path) {
    return path == EffectPath;
}

core::Result<Extraction> extractEffects(std::span<const std::byte> bytes, const GameTables* tables,
                                        std::string_view source) {
    const auto slotCount = fx::efeSlotCount(bytes.size());
    if (!slotCount) {
        return std::unexpected{slotCount.error()};
    }
    Extraction extraction;
    std::vector<std::string> empty;
    std::size_t used = 0;
    std::size_t instructions = 0;
    std::size_t models = 0;
    for (std::size_t slot = 0; slot < *slotCount; ++slot) {
        const auto slotBytes = fx::efeSlot(bytes, slot);
        if (!slotBytes) {
            return std::unexpected{slotBytes.error()};
        }
        if (fx::isEmptyEfeSlot(*slotBytes)) {
            empty.push_back(std::to_string(slot));
            continue;
        }
        SlotExtractor extractor{extraction, tables, source, slot, slot * fx::EfeSlotSize};
        extractor.extract(*slotBytes);
        instructions += extractor.instructions();
        models += extractor.models();
        ++used;
    }
    extraction.notes.push_back(std::format("{} effect slots of {:#x} bytes, one per technique; {} in use with {} "
                                           "script instructions and {} models",
                                           *slotCount, fx::EfeSlotSize, used, instructions, models));
    if (!empty.empty()) {
        extraction.notes.push_back(std::format("slots that are all fill, which no technique uses: {}", joined(empty)));
    }
    extraction.notes.emplace_back(
        "each slot's UV animation and sub-effect rooms are workspace: the loader clears some fields and the scripts "
        "write the rest before reading them, so what the file stores there is not data");
    extraction.notes.emplace_back(
        "the original ignores the TIM positions in the file: it uploads each loaded effect to one of 16 VRAM cells "
        "(BTL_setupLoadedEFEFile); models are textured as cell 0 holds them");
    return extraction;
}

core::Result<Extraction> extractEffectFile(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return extractEffects(*bytes, disc.tables(), file.path);
}

} // namespace dwe::dwx
