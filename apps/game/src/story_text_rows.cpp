#include "story_text_rows.hpp"

#include <array>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view Prefix = "story_textbox";
constexpr std::string_view RowList = "story_textbox.row";

// The rows and coloured runs the document has room for: the tallest sized box on the disc has 10
// rows, and a row changes colour a few times at most.
constexpr std::size_t MaxRows = 12;
constexpr std::size_t MaxRuns = 8;

// The font's colours: a {color:N} tag of the original's text becomes a marker the runs split at
// (formatText fills a placeholder from an argument of its name before it drops the tag).
constexpr char ColorMarker = '\x1F';
constexpr std::array<std::string_view, 16> ColorTags{
    "color:0", "color:1", "color:2",  "color:3",  "color:4",  "color:5",  "color:6",  "color:7",
    "color:8", "color:9", "color:10", "color:11", "color:12", "color:13", "color:14", "color:15"};

} // namespace

std::vector<localization::TextArgument> colorArguments() {
  std::vector<localization::TextArgument> colors;
  colors.reserve(ColorTags.size());
  for (std::size_t color = 0; color < ColorTags.size(); ++color) {
    colors.push_back({.name = ColorTags.at(color),
                      .value = std::format("{}{}{}", ColorMarker, color, ColorMarker)});
  }
  return colors;
}

void appendPageRows(std::vector<TextRow>& rows, std::string_view text) {
  std::int64_t color = PageColor;
  TextRow row;
  std::string run;
  auto endRun = [&] {
    if (!run.empty() && row.size() < MaxRuns) {
      row.push_back({.text = std::move(run), .color = color});
    } else if (!run.empty()) {
      row.back().text += run;
    }
    run.clear();
  };
  for (std::size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (c == '\n') {
      endRun();
      rows.push_back(std::move(row));
      row.clear();
    } else if (c == ColorMarker) {
      endRun();
      const std::size_t end = text.find(ColorMarker, at + 1);
      if (std::from_chars(text.data() + at + 1, text.data() + end, color).ec != std::errc{}) {
        color = PageColor;
      }
      at = end;
    } else {
      run += c;
    }
  }
  endRun();
  rows.push_back(std::move(row));
  if (rows.size() > MaxRows) {
    rows.resize(MaxRows);
  }
}

screens::ScreenSettings textboxSettings(const TextboxLook& look, const std::vector<TextRow>& rows) {
  screens::ScreenSettings settings{
      {.name = std::format("{}.open", Prefix), .value = look.open},
      {.name = std::format("{}.kind", Prefix),
       .value = std::string{look.speech ? "speech" : "sized"}},
      {.name = std::format("{}.columns", Prefix), .value = static_cast<double>(look.columns)},
      {.name = std::format("{}.rows", Prefix), .value = static_cast<double>(look.rows)},
      {.name = std::format("{}.opening", Prefix), .value = look.opening},
      {.name = std::format("{}.origin_x", Prefix),
       .value = look.origin ? static_cast<double>(look.origin->x) : -1.0},
      {.name = std::format("{}.origin_y", Prefix),
       .value = look.origin ? static_cast<double>(look.origin->y) : -1.0},
      {.name = std::format("{}.prompt", Prefix), .value = look.prompt},
      {.name = std::format("{}.cursor_row", Prefix),
       .value = look.cursorRow ? static_cast<double>(*look.cursorRow) : -1.0},
      {.name = std::format("{}.row_count", Prefix), .value = static_cast<double>(rows.size())},
  };
  for (std::size_t row = 0; row < rows.size(); ++row) {
    settings.push_back({.name = screens::listEntryName(RowList, row, "runs"),
                        .value = static_cast<double>(rows[row].size())});
    const std::string runs = std::format("{}.{}.run", RowList, row);
    for (std::size_t run = 0; run < rows[row].size(); ++run) {
      settings.push_back(
          {.name = screens::listEntryName(runs, run, "text"), .value = rows[row][run].text});
      settings.push_back({.name = screens::listEntryName(runs, run, "color"),
                          .value = static_cast<double>(rows[row][run].color)});
    }
  }
  return settings;
}

} // namespace dwe::app
