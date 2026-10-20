#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/effect/efe_analysis.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <set>
#include <utility>

namespace dwe::formats::effect {

namespace {

constexpr std::uint32_t VectorSize = 12;
constexpr std::uint32_t Int32Size = 4;
constexpr std::uint32_t ListEntrySize = 10;
constexpr std::uint32_t ListTerminatorSize = 2;
constexpr std::uint32_t KeyframeSize = 4;
constexpr std::uint32_t TileMapSize = 11 * 9;
constexpr std::uint32_t ClutColorSize = 8;
constexpr std::int32_t MaxClutColors = 16;
constexpr std::uint32_t ClutRowSize = 32;
constexpr std::uint32_t RibbonPointsSize = 10 * 8;
constexpr std::uint32_t CornerSize = 8;
// BTL_copyFromParentTransform and BTL_copyToParentTransform copy nothing for other sizes.
constexpr std::array<std::int32_t, 5> ParentFieldSizes = {12, 8, 4, 2, 1};

std::uint32_t bytesOf(EfeWidth width) {
    return static_cast<std::uint32_t>(width);
}

// The variable an instruction reads or writes itself, and its width, or nothing.
std::optional<std::pair<EfeVariable, EfeWidth>> accessedVariable(const EfeInstruction& in) {
    switch (in.opcode) {
    case EfeOpcode::LoadVariable:
    case EfeOpcode::PushVariable:
    case EfeOpcode::ApplyOperator:
    case EfeOpcode::BranchIf:
        return std::pair{in.variable, in.width};
    case EfeOpcode::LoadIndexed:
        return std::pair{in.variable, in.indexWidth};
    case EfeOpcode::PopToVariable:
        return std::pair{in.variable, EfeWidth::Int32};
    default:
        return std::nullopt;
    }
}

class Analyzer {
public:
    Analyzer(std::span<const std::byte> payload, const EfeHeader& header, const EfeScripts& scripts)
        : reader_(payload), scripts_(&scripts), dataEnd_(static_cast<std::uint32_t>(header.startScript)) {}

    EfeAnalysis run() {
        collectBranchTargets();
        // A code argument found on one pass is a label on the next, and a label cuts what a pass knows of the stack.
        std::size_t known = 0;
        do {
            known = labels_.size();
            bindArguments();
        } while (known != labels_.size());
        collectData();
        analysis_.labels.assign(labels_.begin(), labels_.end());
        return std::move(analysis_);
    }

private:
    void addLabel(std::int32_t target, std::uint32_t from) {
        const auto& in = scripts_->instructions;
        const bool atInstruction =
            std::ranges::binary_search(in, static_cast<std::uint32_t>(target), {}, &EfeInstruction::offset);
        if (target < 0 || !atInstruction) {
            analysis_.issues.push_back(
                std::format("{:#06x}: goes to {:#06x}, which is not the start of an instruction", from, target));
            return;
        }
        labels_.insert(static_cast<std::uint32_t>(target));
    }

    void collectBranchTargets() {
        for (const EfeInstruction& in : scripts_->instructions) {
            if (in.opcode == EfeOpcode::BranchIf || in.opcode == EfeOpcode::Jump || in.opcode == EfeOpcode::Call) {
                addLabel(in.target, in.offset);
            }
        }
    }

    void bindArguments() {
        const auto& instructions = scripts_->instructions;
        analysis_.arguments.assign(instructions.size(), {});
        std::vector<std::optional<std::size_t>> stack;
        const auto pop = [&]() -> std::optional<std::size_t> {
            if (stack.empty()) {
                return std::nullopt;
            }
            const auto top = stack.back();
            stack.pop_back();
            return top;
        };
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const EfeInstruction& in = instructions[i];
            if (labels_.contains(in.offset)) {
                stack.clear();
            }
            auto& args = analysis_.arguments[i];
            switch (in.opcode) {
            case EfeOpcode::PushImmediate:
            case EfeOpcode::PushVariable:
            case EfeOpcode::PushAddress:
                stack.emplace_back(i);
                break;
            case EfeOpcode::PopToVariable:
                args.push_back(pop());
                break;
            case EfeOpcode::SubOp: {
                const EfeSubOpSpec& spec = efeSubOp(in.subOp);
                args.resize(spec.args.size());
                for (std::size_t k = spec.args.size(); k-- > 0;) {
                    args[k] = pop();
                }
                for (std::size_t k = 0; k < spec.args.size(); ++k) {
                    const auto address = baseAddress(args[k]);
                    if (spec.args[k].kind == EfeArgKind::Code && address) {
                        addLabel(*address, in.offset);
                    }
                }
                break;
            }
            case EfeOpcode::Jump:
            case EfeOpcode::Call:
            case EfeOpcode::Return:
            case EfeOpcode::Stop:
            case EfeOpcode::Stop6:
            case EfeOpcode::Stop8:
                // What follows runs only from a label, or, after a call, with what the callee left on the stack.
                stack.clear();
                break;
            default:
                break;
            }
        }
    }

    // The base offset a push_address with the base flag pushed, or nothing.
    [[nodiscard]] std::optional<std::int32_t> baseAddress(std::optional<std::size_t> push) const {
        if (!push) {
            return std::nullopt;
        }
        const EfeInstruction& in = scripts_->instructions[*push];
        if (in.opcode != EfeOpcode::PushAddress || in.variable.space != EfeSpace::Base) {
            return std::nullopt;
        }
        return in.variable.offset;
    }

    // The immediate a push_immediate pushed, or nothing.
    [[nodiscard]] std::optional<std::int32_t> immediate(std::optional<std::size_t> push) const {
        if (!push || scripts_->instructions[*push].opcode != EfeOpcode::PushImmediate) {
            return std::nullopt;
        }
        return scripts_->instructions[*push].value;
    }

    // How many values an index pushed from a variable can take, when the block that pushes it last wrote it with a
    // modulo by an immediate (`load_immediate n` then `apply_operator variable modulo`): n, the indices 0 to n - 1.
    // The dividend is taken as not negative; on the disc it is the instance's frame counter.
    [[nodiscard]] std::optional<std::int32_t> moduloRange(std::optional<std::size_t> push) const {
        const auto& instructions = scripts_->instructions;
        if (!push || instructions[*push].opcode != EfeOpcode::PushVariable) {
            return std::nullopt;
        }
        const EfeInstruction& pushed = instructions[*push];
        for (std::size_t i = *push; i > 0 && !labels_.contains(instructions[i].offset); --i) {
            const EfeInstruction& in = instructions[i - 1];
            const bool writes = (in.opcode == EfeOpcode::ApplyOperator || in.opcode == EfeOpcode::PopToVariable ||
                                 in.opcode == EfeOpcode::PushAddress) &&
                                in.variable == pushed.variable;
            if (in.opcode == EfeOpcode::Call || (writes && in.opcode != EfeOpcode::ApplyOperator)) {
                return std::nullopt;
            }
            if (!writes) {
                continue;
            }
            const bool modulo = in.op == EfeOperator::Modulo && in.width == pushed.width && i >= 2 &&
                                !labels_.contains(in.offset) &&
                                instructions[i - 2].opcode == EfeOpcode::LoadImmediate && instructions[i - 2].value > 0;
            return modulo ? std::optional<std::int32_t>{instructions[i - 2].value} : std::nullopt;
        }
        return std::nullopt;
    }

    void addItem(std::uint32_t from, std::int32_t offset, EfeDataItem item) {
        if (offset < 0 || static_cast<std::uint32_t>(offset) + item.size > dataEnd_ ||
            std::cmp_greater_equal(offset, dataEnd_)) {
            analysis_.issues.push_back(
                std::format("{:#06x}: addresses {:#06x}+{}, outside the data area", from, offset, item.size));
            return;
        }
        item.offset = static_cast<std::uint32_t>(offset);
        if (std::ranges::find(items_, item) == items_.end()) {
            items_.push_back(item);
        }
    }

    // The bytes of a list of 10-byte entries that ends with an int16 below 0, or nothing if it runs out of the data.
    [[nodiscard]] std::optional<std::uint32_t> listSize(std::int32_t offset) const {
        for (auto at = static_cast<std::uint32_t>(offset); at + ListTerminatorSize <= dataEnd_; at += ListEntrySize) {
            if (static_cast<std::int16_t>(reader_.u16(at).value_or(0)) < 0) {
                return at - static_cast<std::uint32_t>(offset) + ListTerminatorSize;
            }
        }
        return std::nullopt;
    }

    void addArgument(const EfeInstruction& in, const EfeArg& arg, std::span<const std::optional<std::size_t>> args,
                     std::int32_t offset) {
        using enum EfeArgKind;
        const auto related = [&] { return immediate(args[arg.related]); };
        EfeDataItem item{.size = 0, .kind = EfeDataKind::Argument, .argKind = arg.kind};
        switch (arg.kind) {
        case Code:
            return;
        case Value:
        case Unused:
            break;
        case Int32:
            item.size = Int32Size;
            break;
        case Vector:
        case Color:
            item.size = VectorSize;
            break;
        case BoneOffsets:
        case ParallaxSprites: {
            const auto size = offset >= 0 ? listSize(offset) : std::nullopt;
            if (!size) {
                analysis_.issues.push_back(std::format("{:#06x}: the {} list at {:#06x} has no end in the data area",
                                                       in.offset, arg.name, offset));
                return;
            }
            item.size = *size;
            break;
        }
        case UvKeyframes: {
            const auto count = offset >= 0 ? reader_.s32(static_cast<std::uint32_t>(offset)) : std::nullopt;
            if (!count || *count < 0 || std::cmp_greater(*count, dataEnd_ / KeyframeSize)) {
                analysis_.issues.push_back(
                    std::format("{:#06x}: the keyframes at {:#06x} have no valid count", in.offset, offset));
                return;
            }
            item.size = Int32Size + (static_cast<std::uint32_t>(*count) * KeyframeSize);
            break;
        }
        case ParentField: {
            const auto size = related();
            item.bounded = size.has_value();
            if (size && std::ranges::contains(ParentFieldSizes, *size)) {
                item.size = static_cast<std::uint32_t>(*size);
            }
            break;
        }
        case Int32Array: {
            const auto count = related();
            item.bounded = count.has_value();
            if (count && *count > 0) {
                item.size = static_cast<std::uint32_t>(*count) * Int32Size;
            }
            break;
        }
        case ClutColors: {
            const auto count = related();
            item.bounded = count.has_value();
            if (count && *count >= 1 && *count <= MaxClutColors) {
                item.size = static_cast<std::uint32_t>(*count) * ClutColorSize;
            }
            break;
        }
        case ClutRow: {
            // BTL_addClutLoadPrim copies 32 bytes from the colours plus twice the index.
            if (const auto index = related()) {
                offset += *index * 2;
                item.size = ClutRowSize;
            } else if (const auto indices = moduloRange(args[arg.related])) {
                item.size = ClutRowSize + (static_cast<std::uint32_t>(*indices - 1) * 2);
                item.count = static_cast<std::int16_t>(*indices);
            } else {
                item.bounded = false;
            }
            break;
        }
        case TileMap:
            item.size = TileMapSize;
            break;
        case TileOffsets: {
            const auto map = baseAddress(args[arg.related]);
            const auto tiles =
                map && *map >= 0 ? reader_.bytes(static_cast<std::uint32_t>(*map), TileMapSize) : std::nullopt;
            item.bounded = tiles.has_value();
            if (tiles) {
                const auto highest = std::ranges::max(
                    *tiles | std::views::transform([](std::byte b) { return std::to_integer<std::uint32_t>(b); }));
                item.size = (highest + 1) * 2;
            }
            break;
        }
        case RibbonPoints:
            item.size = RibbonPointsSize;
            break;
        case QuadCorner: {
            const auto semiTransparency = related();
            item.bounded = semiTransparency.has_value();
            if (semiTransparency && *semiTransparency < 0) {
                item.size = CornerSize;
            }
            break;
        }
        }
        addItem(in.offset, offset, item);
    }

    void collectData() {
        const auto& instructions = scripts_->instructions;
        std::vector<std::int32_t> tables;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const EfeInstruction& in = instructions[i];
            if (const auto variable = accessedVariable(in); variable && variable->first.space == EfeSpace::Base) {
                addItem(in.offset, variable->first.offset,
                        {.size = bytesOf(variable->second), .kind = EfeDataKind::Variable, .width = variable->second});
            }
            if (in.opcode == EfeOpcode::LoadIndexed) {
                tables.push_back(in.target);
                addItem(in.offset, in.target, {.size = 0, .kind = EfeDataKind::Table, .width = in.width});
            }
            if (in.opcode == EfeOpcode::Spawn) {
                if (in.count < 0 || in.stride < 0) {
                    analysis_.issues.push_back(std::format("{:#06x}: a negative instance table", in.offset));
                    continue;
                }
                addItem(in.offset, in.target,
                        {.size = static_cast<std::uint32_t>(in.count) * static_cast<std::uint32_t>(in.stride),
                         .kind = EfeDataKind::Instances,
                         .count = in.count,
                         .stride = in.stride});
            }
            if (in.opcode == EfeOpcode::SubOp) {
                const EfeSubOpSpec& spec = efeSubOp(in.subOp);
                for (std::size_t k = 0; k < spec.args.size(); ++k) {
                    if (const auto address = baseAddress(analysis_.arguments[i][k])) {
                        addArgument(in, spec.args[k], analysis_.arguments[i], *address);
                    }
                }
            }
        }
        // An address pushed for anything but a sub-op argument, such as a call's: the width it was written with.
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const EfeInstruction& in = instructions[i];
            if (in.opcode != EfeOpcode::PushAddress || in.variable.space != EfeSpace::Base || isBound(i)) {
                continue;
            }
            const unsigned declared = in.variant();
            if (declared == 1 || declared == 2 || declared == 4) {
                addItem(in.offset, in.variable.offset,
                        {.size = declared, .kind = EfeDataKind::Variable, .width = static_cast<EfeWidth>(declared)});
            }
        }
        std::ranges::sort(items_, {}, &EfeDataItem::offset);
        extendTables();
        addGaps();
    }

    [[nodiscard]] bool isBound(std::size_t push) const {
        for (std::size_t i = 0; i < analysis_.arguments.size(); ++i) {
            if (scripts_->instructions[i].opcode == EfeOpcode::SubOp &&
                std::ranges::contains(analysis_.arguments[i], std::optional{push})) {
                return true;
            }
        }
        return false;
    }

    // A table runs from its base up to the next byte another item covers, past a variable that is its first element.
    void extendTables() {
        std::vector<bool> covered(dataEnd_, false);
        for (const EfeDataItem& item : items_) {
            if (item.kind != EfeDataKind::Table) {
                std::fill_n(covered.begin() + item.offset, std::min(item.size, dataEnd_ - item.offset), true);
            }
        }
        for (EfeDataItem& table : items_) {
            if (table.kind != EfeDataKind::Table) {
                continue;
            }
            std::uint32_t end = table.offset + bytesOf(table.width);
            while (end < dataEnd_ && !covered[end]) {
                ++end;
            }
            table.size = std::min(end, dataEnd_) - table.offset;
        }
    }

    // Bytes no item covers are unused: nothing else in the interpreter forms a data address. A read with no known end
    // may run into the gaps after it, which stay unreferenced.
    void addGaps() {
        std::vector<bool> covered(dataEnd_, false);
        std::optional<std::uint32_t> open;
        for (const EfeDataItem& item : items_) {
            std::fill_n(covered.begin() + item.offset, std::min(item.size, dataEnd_ - item.offset), true);
            if (!item.bounded && (!open || item.offset < *open)) {
                open = item.offset;
            }
        }
        analysis_.data = items_;
        for (std::uint32_t at = 0; at < dataEnd_;) {
            if (covered[at]) {
                ++at;
                continue;
            }
            std::uint32_t end = at;
            bool fill = true;
            while (end < dataEnd_ && !covered[end]) {
                fill = fill && reader_.u8(end) == std::to_integer<std::uint8_t>(EfeFill);
                ++end;
            }
            const bool reachable = !fill && open && *open <= at;
            EfeDataKind kind = EfeDataKind::Unused;
            if (fill) {
                kind = EfeDataKind::Fill;
            } else if (reachable) {
                kind = EfeDataKind::Unreferenced;
            }
            analysis_.data.push_back({.offset = at, .size = end - at, .kind = kind});
            if (reachable) {
                analysis_.issues.push_back(std::format(
                    "{:#06x}-{:#06x}: data that no instruction addresses, after a read at {:#06x} with no known end",
                    at, end, *open));
            }
            at = end;
        }
        std::ranges::stable_sort(analysis_.data, {}, &EfeDataItem::offset);
    }

    ByteReader reader_;
    const EfeScripts* scripts_;
    std::uint32_t dataEnd_;
    std::set<std::uint32_t> labels_;
    std::vector<EfeDataItem> items_;
    EfeAnalysis analysis_;
};

} // namespace

core::Result<EfeAnalysis> analyzeEfeScripts(std::span<const std::byte> payload, const EfeHeader& header,
                                            const EfeScripts& scripts) {
    if (std::cmp_greater(header.startScript, payload.size())) {
        return core::fail("the data area runs past the payload");
    }
    return Analyzer{payload, header, scripts}.run();
}

} // namespace dwe::formats::effect
