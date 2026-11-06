#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/lua_reader.hpp>
#include <dwe/formats/script/story_api_names.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

constexpr int HexBase = 16;
constexpr int DecimalBase = 10;
constexpr std::int64_t Unsigned16Mask = 0xFFFF;
constexpr std::int64_t Signed32Limit = 0x80000000;
constexpr std::int64_t Unsigned32 = 0x100000000;
constexpr std::int64_t ByteLimit = 0x100;
constexpr std::string_view RoutinesTable = "routines";
constexpr std::string_view StoryTable = "story";
constexpr std::string_view EndedName = "ENDED";
constexpr std::string_view RoutinePrefix = "at_";

// The comparisons of the story reads, in the order of the listing's compare codes
// (vocabulary::CompareNames: eq, ne, ge, le, gt, lt).
constexpr std::array<std::string_view, 6> CompareOperators = {"==", "~=", ">=", "<=", ">", "<"};
// The comparison that holds when one does not: eq/ne, ge/lt, le/gt.
constexpr std::array<std::size_t, 6> NegatedCompare = {1, 0, 5, 4, 3, 2};

// ---- tokens

enum class TokenKind : std::uint8_t { Name, Number, String, Symbol, End };

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text; // a name, a string's value or a symbol
  std::int64_t number = 0;
  std::size_t line = 0;
  // Where it is in the code: its first character and the one after its last.
  std::size_t begin = 0;
  std::size_t end = 0;
};

bool isNameStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

bool isNameChar(char c) {
  return isNameStart(c) || isDigit(c);
}

bool isHexDigit(char c) {
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

core::Result<std::vector<Token>> tokenize(std::string_view code) {
  std::vector<Token> tokens;
  std::size_t line = 1;
  std::size_t at = 0;
  const auto failHere = [&line](std::string_view what) {
    return core::fail(std::format("line {}: {}", line, what));
  };
  // The level of a long bracket ([[ or [=[) that starts at `from`, if one does.
  const auto longBracket = [&code](std::size_t from) -> std::optional<std::size_t> {
    if (from >= code.size() || code[from] != '[') {
      return std::nullopt;
    }
    std::size_t level = 0;
    while (from + 1 + level < code.size() && code[from + 1 + level] == '=') {
      ++level;
    }
    if (from + 1 + level >= code.size() || code[from + 1 + level] != '[') {
      return std::nullopt;
    }
    return level;
  };
  // Each round reads one token, a space or a comment, so a round starts where the last token ended.
  std::size_t tokenStart = 0;
  std::size_t placed = 0;
  const auto place = [&] {
    if (tokens.size() > placed) {
      tokens.back().begin = tokenStart;
      tokens.back().end = at;
      placed = tokens.size();
    }
  };
  while (at < code.size()) {
    place();
    tokenStart = at;
    const char c = code[at];
    if (c == '\n') {
      ++line;
      ++at;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++at;
      continue;
    }
    if (code.substr(at).starts_with("--")) {
      at += 2;
      if (const auto level = longBracket(at)) {
        const std::string closing = "]" + std::string(*level, '=') + "]";
        const std::size_t end = code.find(closing, at);
        if (end == std::string_view::npos) {
          return failHere("a comment that does not end");
        }
        line += static_cast<std::size_t>(std::ranges::count(code.substr(at, end - at), '\n'));
        at = end + closing.size();
      } else {
        while (at < code.size() && code[at] != '\n') {
          ++at;
        }
      }
      continue;
    }
    if (isNameStart(c)) {
      const std::size_t start = at;
      while (at < code.size() && isNameChar(code[at])) {
        ++at;
      }
      tokens.push_back({.kind = TokenKind::Name,
                        .text = std::string{code.substr(start, at - start)},
                        .number = 0,
                        .line = line});
      continue;
    }
    if (isDigit(c)) {
      const bool hex = code.substr(at).starts_with("0x") || code.substr(at).starts_with("0X");
      const std::size_t start = hex ? at + 2 : at;
      std::size_t end = start;
      while (end < code.size() && (hex ? isHexDigit(code[end]) : isDigit(code[end]))) {
        ++end;
      }
      std::int64_t value = 0;
      const auto [last, error] = std::from_chars(code.data() + start, code.data() + end, value,
                                                 hex ? HexBase : DecimalBase);
      if (error != std::errc{} || last != code.data() + end || end == start ||
          (end < code.size() && (isNameChar(code[end]) || code[end] == '.'))) {
        return failHere("a number the reader does not take (only whole numbers, decimal or 0x)");
      }
      tokens.push_back({.kind = TokenKind::Number, .text = {}, .number = value, .line = line});
      at = end;
      continue;
    }
    if (c == '"' || c == '\'') {
      std::string value;
      ++at;
      while (true) {
        if (at >= code.size() || code[at] == '\n') {
          return failHere("a string that does not end on its line");
        }
        const char next = code[at++];
        if (next == c) {
          break;
        }
        if (next != '\\') {
          value += next;
          continue;
        }
        if (at >= code.size()) {
          return failHere("a string that does not end on its line");
        }
        const char escaped = code[at++];
        if (escaped == 'n') {
          value += '\n';
        } else if (escaped == 't') {
          value += '\t';
        } else if (escaped == '\\' || escaped == '"' || escaped == '\'') {
          value += escaped;
        } else if (isDigit(escaped)) {
          int byte = escaped - '0';
          for (int digit = 1; digit < 3 && at < code.size() && isDigit(code[at]); ++digit) {
            byte = (byte * DecimalBase) + (code[at++] - '0');
          }
          if (byte >= ByteLimit) {
            return failHere("a string escape past 255");
          }
          value += static_cast<char>(byte);
        } else {
          return failHere(std::format("a string escape \\{} the reader does not take", escaped));
        }
      }
      tokens.push_back(
          {.kind = TokenKind::String, .text = std::move(value), .number = 0, .line = line});
      continue;
    }
    const std::string_view two = code.substr(at, 2);
    if (two == "==" || two == "~=" || two == ">=" || two == "<=") {
      tokens.push_back(
          {.kind = TokenKind::Symbol, .text = std::string{two}, .number = 0, .line = line});
      at += 2;
      continue;
    }
    if (std::string_view{"(){}[]=<>,.;-"}.contains(c)) {
      tokens.push_back(
          {.kind = TokenKind::Symbol, .text = std::string(1, c), .number = 0, .line = line});
      ++at;
      continue;
    }
    return failHere(std::format("the character {:#04x} is not Lua the reader takes",
                                static_cast<unsigned char>(c)));
  }
  place();
  tokens.push_back({.kind = TokenKind::End, .text = {}, .number = 0, .line = line});
  tokens.back().begin = tokens.back().end = code.size();
  return tokens;
}

// ---- the syntax tree

// A key of a table constructor: a name (script = ...) or a number in brackets ([0x0051] = ...).
struct TableKey {
  std::string name;
  std::optional<std::int64_t> index;
};

struct LuaExpression {
  enum class Kind : std::uint8_t {
    Number,
    String,
    Name,
    Nil,
    True,
    False,
    Field, // object.name
    Call,  // [object.]name(operands)
    Not,
    Negate,
    And,
    Or,
    Compare, // operands[0] text operands[1]
    Parenthesized,
    Table,
  };
  Kind kind = Kind::Nil;
  std::size_t line = 0;
  std::int64_t number = 0;
  std::string text;   // a Name, a String, a Field's or a Call's name, a Compare's operator
  std::string object; // a Field's or a Call's table, if any
  std::vector<LuaExpression> operands;
  std::vector<TableKey> keys; // a Table's, one per operand
};

LuaExpression expressionOf(LuaExpression::Kind kind, std::size_t line) {
  LuaExpression result;
  result.kind = kind;
  result.line = line;
  return result;
}

struct LuaStatement;
using LuaBlock = std::vector<LuaStatement>;

struct LuaClause {
  std::size_t line = 0;
  LuaExpression condition;
  LuaBlock body;
};

struct LuaStatement {
  enum class Kind : std::uint8_t { Call, If, While, Do, Break, Return, Local, Assign, Function };
  Kind kind = Kind::Call;
  std::size_t line = 0;
  // Call: the call; While: the condition; Return: the value; Local, Assign: the value.
  std::optional<LuaExpression> value;
  std::string name; // Local and Assign: the variable; Function: its name, "routines.at_0040"
  std::vector<LuaClause> clauses; // If: the if and every elseif
  bool hasElse = false;
  LuaBlock otherwise; // If: the else
  LuaBlock body;      // While, Do, Function
};

// A statement's value; nil for one without (the parser gives a value to every kind that has one).
const LuaExpression& valueOf(const LuaStatement& statement) {
  static const LuaExpression nil = expressionOf(LuaExpression::Kind::Nil, 0);
  return statement.value ? *statement.value : nil;
}

const std::set<std::string_view, std::less<>>& keywords() {
  static const std::set<std::string_view, std::less<>> words = {
      "and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
      "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
      "repeat",   "return", "then", "true", "until",  "while"};
  return words;
}

class Parser {
public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  core::Result<LuaBlock> chunk() {
    LuaBlock result = block();
    if (!error_ && peek().kind != TokenKind::End) {
      failAt(peek(),
             std::format("'{}' where a statement or the end of the module goes", show(peek())));
    }
    if (error_) {
      return core::fail(*error_);
    }
    return result;
  }

private:
  [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
    return tokens_[std::min(at_ + ahead, tokens_.size() - 1)];
  }

  const Token& next() {
    const Token& token = peek();
    at_ = std::min(at_ + 1, tokens_.size() - 1);
    return token;
  }

  [[nodiscard]] bool is(std::string_view text, std::size_t ahead = 0) const {
    const Token& token = peek(ahead);
    return (token.kind == TokenKind::Symbol || token.kind == TokenKind::Name) && token.text == text;
  }

  bool accept(std::string_view text) {
    if (is(text)) {
      next();
      return true;
    }
    return false;
  }

  void expect(std::string_view text) {
    if (!accept(text)) {
      failAt(peek(), std::format("'{}' where '{}' goes", show(peek()), text));
    }
  }

  [[nodiscard]] static std::string show(const Token& token) {
    switch (token.kind) {
    case TokenKind::Number:
      return std::to_string(token.number);
    case TokenKind::String:
      return "a string";
    case TokenKind::End:
      return "the end of the module";
    default:
      return token.text;
    }
  }

  void failAt(const Token& token, std::string message) {
    if (!error_) {
      error_ = std::format("line {}: {}", token.line, message);
    }
  }

  [[nodiscard]] bool endsBlock() const {
    return peek().kind == TokenKind::End || is("end") || is("else") || is("elseif");
  }

  LuaBlock block() {
    LuaBlock statements;
    while (!error_ && !endsBlock()) {
      if (accept(";")) {
        continue;
      }
      statements.push_back(statement());
      if (statements.back().kind == LuaStatement::Kind::Return) {
        accept(";");
        if (!endsBlock()) {
          failAt(peek(), "a return that is not the last statement of its block");
        }
        break;
      }
    }
    return statements;
  }

  std::string name() {
    const Token& token = peek();
    if (token.kind != TokenKind::Name || keywords().contains(token.text)) {
      failAt(token, std::format("'{}' where a name goes", show(token)));
      return {};
    }
    return next().text;
  }

  LuaStatement statement() {
    LuaStatement result;
    result.line = peek().line;
    if (accept("if")) {
      result.kind = LuaStatement::Kind::If;
      do {
        LuaClause clause{.line = tokens_[at_ - 1].line, .condition = expression(), .body = {}};
        expect("then");
        clause.body = block();
        result.clauses.push_back(std::move(clause));
      } while (!error_ && accept("elseif"));
      if (accept("else")) {
        result.hasElse = true;
        result.otherwise = block();
      }
      expect("end");
    } else if (accept("while")) {
      result.kind = LuaStatement::Kind::While;
      result.value = expression();
      expect("do");
      result.body = block();
      expect("end");
    } else if (accept("do")) {
      result.kind = LuaStatement::Kind::Do;
      result.body = block();
      expect("end");
    } else if (accept("break")) {
      result.kind = LuaStatement::Kind::Break;
    } else if (accept("return")) {
      result.kind = LuaStatement::Kind::Return;
      if (!endsBlock() && !is(";")) {
        result.value = expression();
      }
    } else if (accept("local")) {
      result.kind = LuaStatement::Kind::Local;
      result.name = name();
      if (accept("<")) {
        name();
        expect(">");
      }
      expect("=");
      result.value = expression();
    } else if (accept("function")) {
      result.kind = LuaStatement::Kind::Function;
      result.name = name();
      if (accept(".")) {
        result.name += "." + name();
      }
      expect("(");
      expect(")");
      result.body = block();
      expect("end");
    } else if (peek().kind == TokenKind::Name && is("=", 1)) {
      result.kind = LuaStatement::Kind::Assign;
      result.name = name();
      expect("=");
      result.value = expression();
    } else {
      result.kind = LuaStatement::Kind::Call;
      result.value = primary();
      if (!error_ && result.value->kind != LuaExpression::Kind::Call) {
        failAt(tokens_[at_ - 1], "an expression where a statement goes");
      }
    }
    return result;
  }

  LuaExpression expression() { return orExpression(); }

  static LuaExpression binary(LuaExpression::Kind kind, LuaExpression lhs, LuaExpression rhs) {
    LuaExpression result = expressionOf(kind, lhs.line);
    result.operands.push_back(std::move(lhs));
    result.operands.push_back(std::move(rhs));
    return result;
  }

  LuaExpression orExpression() {
    LuaExpression result = andExpression();
    while (!error_ && accept("or")) {
      result = binary(LuaExpression::Kind::Or, std::move(result), andExpression());
    }
    return result;
  }

  LuaExpression andExpression() {
    LuaExpression result = comparison();
    while (!error_ && accept("and")) {
      result = binary(LuaExpression::Kind::And, std::move(result), comparison());
    }
    return result;
  }

  LuaExpression comparison() {
    LuaExpression lhs = unary();
    for (const std::string_view op : {"==", "~=", ">=", "<=", ">", "<"}) {
      if (!error_ && accept(op)) {
        LuaExpression result = binary(LuaExpression::Kind::Compare, std::move(lhs), unary());
        result.text = std::string{op};
        return result;
      }
    }
    return lhs;
  }

  LuaExpression unary() {
    const std::size_t line = peek().line;
    if (accept("not")) {
      LuaExpression result = expressionOf(LuaExpression::Kind::Not, line);
      result.operands.push_back(unary());
      return result;
    }
    if (accept("-")) {
      LuaExpression operand = unary();
      if (operand.kind == LuaExpression::Kind::Number) {
        operand.number = -operand.number;
        return operand;
      }
      LuaExpression result = expressionOf(LuaExpression::Kind::Negate, line);
      result.operands.push_back(std::move(operand));
      return result;
    }
    return primary();
  }

  LuaExpression primary() {
    const Token& token = peek();
    LuaExpression result = expressionOf(LuaExpression::Kind::Nil, token.line);
    if (token.kind == TokenKind::Number) {
      result.kind = LuaExpression::Kind::Number;
      result.number = next().number;
      return result;
    }
    if (token.kind == TokenKind::String) {
      result.kind = LuaExpression::Kind::String;
      result.text = next().text;
      return result;
    }
    if (accept("nil")) {
      result.kind = LuaExpression::Kind::Nil;
      return result;
    }
    if (accept("true")) {
      result.kind = LuaExpression::Kind::True;
      return result;
    }
    if (accept("false")) {
      result.kind = LuaExpression::Kind::False;
      return result;
    }
    if (accept("(")) {
      result.kind = LuaExpression::Kind::Parenthesized;
      result.operands.push_back(expression());
      expect(")");
      return result;
    }
    if (accept("{")) {
      return table(std::move(result));
    }
    result.kind = LuaExpression::Kind::Name;
    result.text = name();
    if (accept(".")) {
      result.kind = LuaExpression::Kind::Field;
      result.object = std::move(result.text);
      result.text = name();
    }
    if (accept("(")) {
      result.kind = LuaExpression::Kind::Call;
      while (!error_ && !is(")")) {
        result.operands.push_back(expression());
        if (!accept(",")) {
          break;
        }
      }
      expect(")");
    }
    return result;
  }

  LuaExpression table(LuaExpression result) {
    result.kind = LuaExpression::Kind::Table;
    while (!error_ && !is("}")) {
      TableKey key;
      if (accept("[")) {
        const LuaExpression index = expression();
        if (index.kind != LuaExpression::Kind::Number) {
          failAt(peek(), "a table key that is not a number");
        }
        key.index = index.number;
        expect("]");
      } else {
        key.name = name();
      }
      expect("=");
      result.keys.push_back(std::move(key));
      result.operands.push_back(expression());
      if (!accept(",") && !accept(";")) {
        break;
      }
    }
    expect("}");
    return result;
  }

  std::vector<Token> tokens_;
  std::size_t at_ = 0;
  std::optional<std::string> error_;
};

// ---- from the tree to the listing

// The op of each story function: storyFunctionName backwards.
const std::map<std::string, std::string, std::less<>>& opsByStoryName() {
  static const std::map<std::string, std::string, std::less<>> ops = [] {
    std::map<std::string, std::string, std::less<>> byName;
    for (unsigned opcode = 0; opcode <= std::numeric_limits<std::uint8_t>::max(); ++opcode) {
      const std::string_view op = opcodeName(static_cast<std::uint8_t>(opcode));
      if (!op.empty()) {
        byName.emplace(std::string{storyFunctionName(op)}, std::string{op});
      }
    }
    return byName;
  }();
  return ops;
}

const LuaExpression& unwrap(const LuaExpression& expression) {
  const LuaExpression* inner = &expression;
  while (inner->kind == LuaExpression::Kind::Parenthesized && !inner->operands.empty()) {
    inner = &inner->operands.front();
  }
  return *inner;
}

bool isCallOf(const LuaExpression& expression, std::string_view object) {
  return expression.kind == LuaExpression::Kind::Call && expression.object == object;
}

bool isName(const LuaExpression& expression, std::string_view name) {
  return expression.kind == LuaExpression::Kind::Name && expression.text == name;
}

// `<name> == <number>`, the arm test of a choice or a state machine.
std::optional<std::int64_t> armOf(const LuaExpression& condition, std::string_view variable) {
  const LuaExpression& test = unwrap(condition);
  if (test.kind != LuaExpression::Kind::Compare || test.text != "==" ||
      !isName(test.operands[0], variable) || test.operands[1].kind != LuaExpression::Kind::Number) {
    return std::nullopt;
  }
  return test.operands[1].number;
}

// The arguments of an op or an IF entry in the order of its operands, from their values by name; an
// operand without one is 0.
std::vector<Argument> argumentsOf(const std::vector<OperandField>& fields,
                                  const std::map<std::string_view, std::int32_t>& values) {
  std::vector<Argument> arguments;
  for (const OperandField& field : fields) {
    const auto value = values.find(field.name);
    arguments.push_back({.name = field.name,
                         .kind = field.kind,
                         .value = value != values.end() ? value->second : 0});
  }
  return arguments;
}

// What withFlow fills in from the operands: the section of another script a FileCall or a FileJump
// runs.
void addReference(ListingInstruction& instruction) {
  if (instruction.role != FlowRole::FileCall && instruction.role != FlowRole::FileJump) {
    return;
  }
  const auto script = instruction.argument("script");
  const auto section = instruction.argument("section");
  if (script && section) {
    instruction.reference = ScriptReference{.script = static_cast<std::uint16_t>(*script),
                                            .section = static_cast<std::uint16_t>(*section)};
  }
}

// An instruction with only its op and flow role.
ListingInstruction instructionOf(std::string op, FlowRole role) {
  ListingInstruction instruction;
  instruction.op = std::move(op);
  instruction.role = role;
  return instruction;
}

// A function of the module, routines.<name>.
struct Function {
  std::string name;
  const LuaStatement* statement = nullptr;
  std::size_t entry = 0; // the pending instruction it starts at
};

// An instruction of the listing being built, by the order it was met in.
struct Pending {
  ListingInstruction instruction;
  bool placeholder = true; // only the entry of a function, which no statement ran
};

class Lifter {
public:
  Lifter(const LuaBlock& chunk, const LuaLineMap* lines) : chunk_(&chunk), lines_(lines) {}

  core::Result<LuaListing> lift() {
    const LuaStatement* returned = nullptr;
    for (const LuaStatement& statement : *chunk_) {
      switch (statement.kind) {
      case LuaStatement::Kind::Local:
        break; // story, ENDED, PLAYER, PARTNER and the routines table
      case LuaStatement::Kind::Function: {
        if (!statement.name.starts_with(std::string{RoutinesTable} + ".")) {
          fail(statement.line, "a function that is not one of the routines table");
          break;
        }
        const std::string name = statement.name.substr(RoutinesTable.size() + 1);
        if (functionNamed(name) != nullptr) {
          fail(statement.line, std::format("routines.{} is defined twice", name));
        }
        functions_.push_back({.name = name, .statement = &statement, .entry = 0});
        break;
      }
      case LuaStatement::Kind::Return:
        returned = &statement;
        break;
      default:
        fail(statement.line, "only locals, the functions of routines and the returned table go at "
                             "the top of a module");
        break;
      }
    }
    if (returned == nullptr || valueOf(*returned).kind != LuaExpression::Kind::Table) {
      fail(returned != nullptr ? returned->line : 0,
           "the module does not return { script = <id>, sections = { ... } }");
    }
    if (error_) {
      return core::fail(*error_);
    }
    for (Function& function : functions_) {
      function.entry = placeEntry(function);
    }
    LuaListing result;
    for (const Function& function : functions_) {
      StructuredRoutine routine{.kind = RoutineKind::Subroutine,
                                .section = 0,
                                .instruction = function.entry,
                                .body = {},
                                .fallback = std::nullopt};
      liftBlock(function.statement->body, routine.body);
      result.structured.routines.push_back(std::move(routine));
    }
    readReturnedTable(valueOf(*returned), result);
    if (lines_ != nullptr && !inconsistency_) {
      for (const auto& [line, offset] : *lines_) {
        if (!usedLines_.contains(line)) {
          inconsistent(
              std::format("line {} has offset {:#06x} and runs no instruction", line, offset));
          break;
        }
      }
    }
    if (error_) {
      return core::fail(*error_);
    }
    finish(result);
    result.offsets = lines_ != nullptr;
    return result;
  }

  // Why the line map does not match the module, if it does not.
  [[nodiscard]] const std::optional<std::string>& inconsistency() const { return inconsistency_; }

private:
  void fail(std::size_t line, std::string message) {
    if (!error_) {
      error_ = std::format("line {}: {}", line, message);
    }
  }

  void inconsistent(std::string message) {
    if (!inconsistency_) {
      inconsistency_ = std::move(message);
    }
  }

  [[nodiscard]] const Function* functionNamed(std::string_view name) const {
    const auto found = std::ranges::find(functions_, name, &Function::name);
    return found != functions_.end() ? &*found : nullptr;
  }

  // The offset of a line that runs an instruction: the line map's, or the next number.
  std::uint32_t offsetOf(std::size_t line) {
    if (lines_ == nullptr) {
      return nextOffset_++;
    }
    const auto found = lines_->find(line);
    if (found == lines_->end()) {
      inconsistent(std::format("line {} runs an instruction and has no offset", line));
      return 0;
    }
    usedLines_.insert(line);
    return found->second;
  }

  [[nodiscard]] static bool sameInstruction(const ListingInstruction& lhs,
                                            const ListingInstruction& rhs) {
    return lhs.op == rhs.op && lhs.role == rhs.role && lhs.arguments == rhs.arguments &&
           lhs.targets == rhs.targets && lhs.reference == rhs.reference && lhs.text == rhs.text;
  }

  static bool startsWith(const std::vector<Condition>& longer,
                         const std::vector<Condition>& shorter) {
    return shorter.size() <= longer.size() &&
           std::equal(shorter.begin(), shorter.end(), longer.begin());
  }

  // Adds an instruction run at a line, or finds the one the line map already put at its offset.
  std::size_t place(std::size_t line, ListingInstruction instruction, bool placeholder = false) {
    instruction.offset = offsetOf(line);
    if (lines_ != nullptr) {
      const auto found = byOffset_.find(instruction.offset);
      if (found != byOffset_.end()) {
        Pending& existing = pending_[found->second];
        if (placeholder) {
          return found->second;
        }
        if (existing.placeholder) {
          existing = {.instruction = std::move(instruction), .placeholder = false};
        } else if (!sameInstruction(existing.instruction, instruction)) {
          inconsistent(std::format("line {} runs another instruction than the other lines at "
                                   "offset {:#06x}",
                                   line, instruction.offset));
        } else if (startsWith(instruction.conditions, existing.instruction.conditions)) {
          existing.instruction.conditions = std::move(instruction.conditions);
        } else if (!startsWith(existing.instruction.conditions, instruction.conditions)) {
          inconsistent(std::format("the IF on line {} tests other entries than the other lines at "
                                   "offset {:#06x}",
                                   line, instruction.offset));
        }
        return found->second;
      }
      byOffset_.emplace(instruction.offset, pending_.size());
    }
    pending_.push_back({.instruction = std::move(instruction), .placeholder = placeholder});
    return pending_.size() - 1;
  }

  std::size_t placeEntry(const Function& function) {
    const std::size_t line = function.statement->line;
    const std::size_t entry = place(line, instructionOf({}, FlowRole::Plain), /*placeholder=*/true);
    // The emitter names a function by the offset it starts at.
    if (function.name.starts_with(RoutinePrefix) && lines_ != nullptr) {
      const std::string_view digits = std::string_view{function.name}.substr(RoutinePrefix.size());
      std::uint32_t named = 0;
      const auto [last, error] =
          std::from_chars(digits.data(), digits.data() + digits.size(), named, HexBase);
      if (error == std::errc{} && last == digits.data() + digits.size() &&
          named != pending_[entry].instruction.offset) {
        inconsistent(std::format("routines.{} starts at offset {:#06x} by the line map",
                                 function.name, pending_[entry].instruction.offset));
      }
    }
    return entry;
  }

  // The entry of routines.<name>, called on a line.
  std::optional<std::size_t> routineEntry(const LuaExpression& call) {
    const Function* function = functionNamed(call.text);
    if (function == nullptr) {
      fail(call.line, std::format("routines.{} is not defined in the module", call.text));
      return std::nullopt;
    }
    if (!call.operands.empty()) {
      fail(call.line, std::format("routines.{} is called with operands", call.text));
    }
    return function->entry;
  }

  // ---- instructions

  // The instruction of a call of the story API.
  std::optional<ListingInstruction> storyInstruction(const LuaExpression& call) {
    const auto found = opsByStoryName().find(call.text);
    const auto opcode = found != opsByStoryName().end() ? opcodeNamed(found->second) : std::nullopt;
    if (!opcode) {
      fail(call.line, std::format("story.{} is not a function of the story API", call.text));
      return std::nullopt;
    }
    ListingInstruction instruction = instructionOf(found->second, opcodeFlowRole(*opcode));
    if (vocabulary::carriesText(*opcode)) {
      if (call.operands.empty() || call.operands.front().kind != LuaExpression::Kind::String) {
        fail(call.line, std::format("story.{} takes its text key first", call.text));
        return std::nullopt;
      }
      instruction.text = call.operands.front().text;
      return instruction;
    }
    std::vector<OperandField> fields = opcodeOperands(*opcode);
    std::erase_if(fields,
                  [](const OperandField& field) { return field.kind == ArgumentKind::Label; });
    if (fields.size() != call.operands.size()) {
      fail(call.line, std::format("story.{} takes {} operands, not {}", call.text, fields.size(),
                                  call.operands.size()));
      return std::nullopt;
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
      const LuaExpression& operand = call.operands[index];
      std::int64_t value = 0;
      if (operand.kind == LuaExpression::Kind::Number) {
        value = operand.number;
      } else if (isName(operand, "PLAYER")) {
        value = vocabulary::PlayerEntity;
      } else if (isName(operand, "PARTNER")) {
        value = vocabulary::PartnerEntity;
      } else {
        fail(call.line, std::format("operand {} of story.{} is not a number, PLAYER or PARTNER",
                                    index + 1, call.text));
        return std::nullopt;
      }
      instruction.arguments.push_back({.name = fields[index].name,
                                       .kind = fields[index].kind,
                                       .value = static_cast<std::int32_t>(value)});
    }
    addReference(instruction);
    return instruction;
  }

  void run(std::size_t line, ListingInstruction instruction, StatementList& out) {
    const std::size_t id = place(line, std::move(instruction));
    out.push_back({RunStatement{.first = id, .count = 1}});
  }

  // A call statement: an op of the story API, a routine of the module (a jump_and_link whose end of
  // section the caller does not pass on) or error(), which stops the section.
  void liftCall(const LuaStatement& statement, StatementList& out) {
    const LuaExpression& call = valueOf(statement);
    if (isCallOf(call, StoryTable)) {
      auto instruction = storyInstruction(call);
      if (!instruction) {
        return;
      }
      switch (instruction->role) {
      case FlowRole::Plain:
      case FlowRole::LoadScript:
      case FlowRole::FileCall:
      case FlowRole::FileJump:
      case FlowRole::MapChange:
      case FlowRole::EndSection:
      case FlowRole::EndScript:
        run(statement.line, std::move(*instruction), out);
        return;
      default:
        fail(
            statement.line,
            std::format("story.{} moves the flow: write it as Lua (if, while, return)", call.text));
        return;
      }
    }
    if (isCallOf(call, RoutinesTable)) {
      liftRoutineCall(statement.line, call, out);
      return;
    }
    if (call.object.empty() && call.text == "error") {
      run(statement.line, instructionOf("error", FlowRole::Data), out);
      return;
    }
    fail(statement.line, "a call of something that is not the story API nor a routine");
  }

  void liftRoutineCall(std::size_t line, const LuaExpression& call, StatementList& out) {
    const auto entry = routineEntry(call);
    if (!entry) {
      return;
    }
    ListingInstruction instruction = instructionOf("jump_and_link", FlowRole::Call);
    instruction.targets = {pending_[*entry].instruction.offset};
    run(line, std::move(instruction), out);
  }

  void liftReturn(const LuaStatement& statement, StatementList& out) {
    if (!statement.value) {
      run(statement.line, instructionOf("jump_return", FlowRole::Return), out);
      return;
    }
    const LuaExpression& value = valueOf(statement);
    if (isName(value, EndedName)) {
      run(statement.line, instructionOf("end_section", FlowRole::EndSection), out);
      return;
    }
    if (isCallOf(value, RoutinesTable)) {
      const auto entry = routineEntry(value);
      if (!entry) {
        return;
      }
      // The emitter puts the routine's offset on the line of a jump to it.
      if (lines_ != nullptr) {
        const auto found = lines_->find(statement.line);
        if (found != lines_->end()) {
          usedLines_.insert(statement.line);
          if (found->second != pending_[*entry].instruction.offset) {
            inconsistent(std::format("line {} jumps to routines.{} at another offset",
                                     statement.line, value.text));
          }
        }
      }
      out.push_back({TailCallStatement{.instruction = *entry}});
      return;
    }
    if (isCallOf(value, StoryTable)) {
      auto instruction = storyInstruction(value);
      if (!instruction) {
        return;
      }
      if (instruction->role != FlowRole::FileJump && instruction->role != FlowRole::MapChange) {
        fail(statement.line,
             std::format("story.{} does not end the section: call it, then return", value.text));
        return;
      }
      run(statement.line, std::move(*instruction), out);
      return;
    }
    fail(statement.line, "a return of something else than nothing, ENDED, a routine or "
                         "story.jump_to_file, story.warp_to or story.teleport");
  }

  // ---- conditions

  // The IF entry of one test; `negated` flips it.
  std::optional<Condition> test(const LuaExpression& expression, Combine combine) {
    bool negated = false;
    const LuaExpression* read = &unwrap(expression);
    while (read->kind == LuaExpression::Kind::Not) {
      negated = !negated;
      read = &unwrap(read->operands.front());
    }
    std::map<std::string_view, std::int32_t> values;
    ConditionTest kind = ConditionTest::Trigger;
    std::uint8_t variant = 0;
    if (isCallOf(*read, StoryTable)) {
      const auto number = [&]() -> std::int32_t {
        if (read->operands.size() != 1 || read->operands[0].kind != LuaExpression::Kind::Number) {
          fail(read->line, std::format("story.{} takes one number", read->text));
          return 0;
        }
        return static_cast<std::int32_t>(read->operands[0].number);
      };
      if (read->text == "trigger") {
        values["trigger"] = number();
        variant = negated ? vocabulary::ClearBit : 0;
      } else if (read->text == "knows_move") {
        kind = ConditionTest::Move;
        values["move"] = number();
        values["negate"] = negated ? 1 : 0;
      } else if (read->text == "has_condition") {
        kind = ConditionTest::Condition;
        values["mask"] = number();
        values["negate"] = negated ? 1 : 0;
      } else {
        fail(read->line, std::format("story.{} is no test of an if", read->text));
        return std::nullopt;
      }
    } else if (read->kind == LuaExpression::Kind::Compare &&
               isCallOf(read->operands[0], StoryTable) &&
               read->operands[1].kind == LuaExpression::Kind::Number) {
      const LuaExpression& reading = read->operands[0];
      const auto found = std::ranges::find(CompareOperators, read->text);
      auto compare = static_cast<std::size_t>(found - CompareOperators.begin());
      if (negated) {
        compare = NegatedCompare.at(compare);
      }
      const std::int64_t constant = read->operands[1].number;
      const auto operand = [&]() -> std::int32_t {
        if (reading.operands.size() != 1 ||
            reading.operands[0].kind != LuaExpression::Kind::Number) {
          fail(reading.line, std::format("story.{} takes one number", reading.text));
          return 0;
        }
        return static_cast<std::int32_t>(reading.operands[0].number);
      };
      values["compare"] = static_cast<std::int32_t>(compare);
      values["value"] = static_cast<std::int32_t>(constant & Unsigned16Mask);
      if (reading.text == "var") {
        kind = ConditionTest::Pstat;
        values["pstat"] = operand();
        values["value"] = static_cast<std::int32_t>(constant);
        variant = static_cast<std::uint8_t>(compare);
      } else if (reading.text == "stat") {
        kind = ConditionTest::Stat;
        values["stat"] = operand();
      } else if (reading.text == "cards") {
        kind = ConditionTest::Card;
        values["card"] = operand();
      } else if (reading.text == "items") {
        kind = ConditionTest::ItemCount;
        values["item"] = operand();
      } else if (reading.text == "money") {
        kind = ConditionTest::Money;
        if (!reading.operands.empty()) {
          fail(reading.line, "story.money takes no operands");
        }
        // The emitter writes a negative amount as its unsigned 32-bit value.
        values["value"] =
            static_cast<std::int32_t>(constant >= Signed32Limit ? constant - Unsigned32 : constant);
      } else {
        fail(reading.line, std::format("story.{} is no read an if compares", reading.text));
        return std::nullopt;
      }
    } else {
      fail(read->line, "an if tests something else than story.trigger, story.knows_move, "
                       "story.has_condition or a story read compared with a number");
      return std::nullopt;
    }
    return Condition{.code = vocabulary::entryCode(kind, variant, combine),
                     .test = kind,
                     .combine = combine,
                     .arguments = argumentsOf(conditionOperands(kind), values)};
  }

  static bool isTest(const LuaExpression& expression) {
    const LuaExpression& inner = unwrap(expression);
    return inner.kind != LuaExpression::Kind::And && inner.kind != LuaExpression::Kind::Or;
  }

  // The entries of a condition read left to right, as scriptIfInstruction folds them: `(a or b) and
  // c` is a, or b, and c. An and or an or whose right side is a combination is read with its sides
  // swapped (they are pure reads), so `a and (b or c)` is b, or c, and a.
  void flatten(const LuaExpression& expression, Combine combine, std::vector<Condition>& entries) {
    const LuaExpression& inner = unwrap(expression);
    if (inner.kind == LuaExpression::Kind::False && entries.empty() &&
        combine == Combine::Replace) {
      return; // the emitter's `if false then`: no entry
    }
    if (inner.kind != LuaExpression::Kind::And && inner.kind != LuaExpression::Kind::Or) {
      if (auto entry = test(inner, combine)) {
        entries.push_back(std::move(*entry));
      }
      return;
    }
    const Combine joined = inner.kind == LuaExpression::Kind::And ? Combine::And : Combine::Or;
    const LuaExpression& lhs = inner.operands[0];
    const LuaExpression& rhs = inner.operands[1];
    if (isTest(rhs)) {
      flatten(lhs, combine, entries);
      flatten(rhs, joined, entries);
    } else if (isTest(lhs)) {
      flatten(rhs, combine, entries);
      flatten(lhs, joined, entries);
    } else {
      fail(inner.line, "a condition that is not one chain of tests read left to right, such as "
                       "(a and b) or (c and d): split it into nested ifs");
    }
  }

  // ---- statements

  void liftBlock(const LuaBlock& block, StatementList& out) {
    for (const LuaStatement& statement : block) {
      if (error_) {
        return;
      }
      liftStatement(statement, out);
    }
  }

  void liftStatement(const LuaStatement& statement, StatementList& out) {
    switch (statement.kind) {
    case LuaStatement::Kind::Call:
      liftCall(statement, out);
      return;
    case LuaStatement::Kind::If:
      liftIf(statement, out);
      return;
    case LuaStatement::Kind::While: {
      if (valueOf(statement).kind != LuaExpression::Kind::True) {
        fail(statement.line, "a loop that is not while true do: leave it with break");
        return;
      }
      LoopStatement loop;
      liftBlock(statement.body, loop.body);
      out.push_back({std::move(loop)});
      return;
    }
    case LuaStatement::Kind::Do:
      liftDo(statement, out);
      return;
    case LuaStatement::Kind::Break:
      out.push_back({BreakStatement{}});
      return;
    case LuaStatement::Kind::Return:
      liftReturn(statement, out);
      return;
    case LuaStatement::Kind::Assign:
      if (std::ranges::contains(machines_, statement.name)) {
        if (valueOf(statement).kind == LuaExpression::Kind::Number) {
          out.push_back(
              {SetStateStatement{.state = static_cast<std::size_t>(valueOf(statement).number)}});
          return;
        }
        if (valueOf(statement).kind == LuaExpression::Kind::Nil) {
          return; // leaving the machine: the break that follows is the statement
        }
      }
      fail(statement.line, "an assignment that is not the state of a state machine");
      return;
    case LuaStatement::Kind::Local:
      fail(statement.line, "a local that does not start a choice or a state machine in a do block");
      return;
    case LuaStatement::Kind::Function:
      fail(statement.line, "a function inside a function");
      return;
    }
  }

  void liftIf(const LuaStatement& statement, StatementList& out) {
    // if routines.f() == ENDED then return ENDED end, and the same for jump_to_file_and_link.
    if (statement.clauses.size() == 1 && !statement.hasElse) {
      const LuaExpression& condition = unwrap(statement.clauses[0].condition);
      const LuaBlock& body = statement.clauses[0].body;
      if (condition.kind == LuaExpression::Kind::Compare && condition.text == "==" &&
          isName(condition.operands[1], EndedName) && body.size() == 1 &&
          body[0].kind == LuaStatement::Kind::Return && body[0].value &&
          isName(valueOf(body[0]), EndedName)) {
        const LuaExpression& call = condition.operands[0];
        if (isCallOf(call, RoutinesTable)) {
          liftRoutineCall(statement.line, call, out);
          return;
        }
        if (isCallOf(call, StoryTable)) {
          auto instruction = storyInstruction(call);
          if (instruction && instruction->role != FlowRole::FileCall) {
            fail(statement.line,
                 std::format("story.{} is no call of another script's section", call.text));
          } else if (instruction) {
            run(statement.line, std::move(*instruction), out);
          }
          return;
        }
      }
    }
    out.push_back({liftClause(statement, 0)});
  }

  // The IF of a clause; its false arm is the next elseif, or the else.
  IfStatement liftClause(const LuaStatement& statement, std::size_t index) {
    const LuaClause& clause = statement.clauses[index];
    // if not (<condition>) then: the emitter's IF with only a false arm.
    const LuaExpression* condition = &clause.condition;
    bool swapped = false;
    if (condition->kind == LuaExpression::Kind::Not &&
        condition->operands.front().kind == LuaExpression::Kind::Parenthesized) {
      condition = &condition->operands.front().operands.front();
      swapped = true;
    }
    std::vector<Condition> entries;
    flatten(*condition, Combine::Replace, entries);
    const std::size_t count = entries.size();
    ListingInstruction instruction = instructionOf("if", FlowRole::If);
    instruction.conditions = std::move(entries);
    const std::size_t id = place(clause.line, std::move(instruction));
    IfStatement result{
        .condition = {.instruction = id, .entry = count}, .whenTrue = {}, .whenFalse = {}};
    liftBlock(clause.body, result.whenTrue);
    if (index + 1 < statement.clauses.size()) {
      result.whenFalse.push_back({liftClause(statement, index + 1)});
    } else if (statement.hasElse) {
      liftBlock(statement.otherwise, result.whenFalse);
    }
    if (swapped) {
      std::swap(result.whenTrue, result.whenFalse);
    }
    return result;
  }

  // A do block: a choice, a state machine, or statements in a block of their own.
  void liftDo(const LuaStatement& statement, StatementList& out) {
    const LuaBlock& body = statement.body;
    if (body.empty() || body.front().kind != LuaStatement::Kind::Local) {
      liftBlock(body, out);
      return;
    }
    const LuaStatement& local = body.front();
    const LuaExpression& value = valueOf(local);
    if (isCallOf(value, StoryTable) && (value.text == "set_selection" || value.text == "var")) {
      liftChoice(statement, out);
      return;
    }
    if (value.kind == LuaExpression::Kind::Number && body.size() >= 2 &&
        body[1].kind == LuaStatement::Kind::While) {
      liftMachine(statement, out);
      return;
    }
    fail(local.line, "a local that does not start a choice or a state machine in a do block");
  }

  // do local choice <const> = story.set_selection(key, count) (or story.var(n) for a SWITCH)
  // followed by its arms: if choice == 0 then ... elseif choice == 1 then ... else ... end, or one
  // arm in a do block.
  void liftChoice(const LuaStatement& statement, StatementList& out) {
    const LuaStatement& local = statement.body.front();
    const LuaExpression& value = valueOf(local);
    if (statement.body.size() != 2) {
      fail(local.line, "a choice is its local and one if chain (or do block) with its arms");
      return;
    }
    const LuaStatement& arms = statement.body[1];
    std::vector<const LuaBlock*> blocks;
    const LuaBlock empty;
    if (arms.kind == LuaStatement::Kind::Do) {
      blocks.push_back(&arms.body);
    } else if (arms.kind == LuaStatement::Kind::If) {
      for (std::size_t index = 0; index < arms.clauses.size(); ++index) {
        if (armOf(arms.clauses[index].condition, local.name) != static_cast<std::int64_t>(index)) {
          fail(arms.clauses[index].line,
               std::format("arm {} of a choice does not test {} == {}", index, local.name, index));
          return;
        }
        blocks.push_back(&arms.clauses[index].body);
      }
      // Without an else, the values past the last arm run nothing.
      blocks.push_back(arms.hasElse ? &arms.otherwise : &empty);
    } else {
      fail(arms.line, "a choice is its local and one if chain (or do block) with its arms");
      return;
    }
    ListingInstruction instruction;
    if (value.text == "set_selection") {
      if (value.operands.size() != 2 || value.operands[0].kind != LuaExpression::Kind::String ||
          value.operands[1].kind != LuaExpression::Kind::Number) {
        fail(local.line, "story.set_selection takes a text key and how many options it has");
        return;
      }
      instruction = instructionOf("set_selection", FlowRole::Selection);
      instruction.targets.resize(
          static_cast<std::size_t>(std::max<std::int64_t>(value.operands[1].number, 0)));
      instruction.text = value.operands[0].text;
    } else {
      if (value.operands.size() != 1 || value.operands[0].kind != LuaExpression::Kind::Number) {
        fail(local.line, "story.var takes one number");
        return;
      }
      const auto opcode = opcodeNamed("switch");
      instruction = instructionOf("switch", FlowRole::Switch);
      instruction.targets.resize(blocks.size());
      instruction.arguments =
          argumentsOf(opcode ? opcodeOperands(*opcode) : std::vector<OperandField>{},
                      {{"pstat", static_cast<std::int32_t>(value.operands[0].number)}});
    }
    ChoiceStatement choice{.instruction = place(local.line, std::move(instruction)), .arms = {}};
    for (const LuaBlock* block : blocks) {
      StatementList arm;
      liftBlock(*block, arm);
      choice.arms.push_back(std::move(arm));
    }
    out.push_back({std::move(choice)});
  }

  // do local stateN = <initial> while true do if stateN == 0 then ... elseif stateN == 1 then ...
  // end end [if stateN == nil then <break> end] end, as the emitter writes a state machine.
  void liftMachine(const LuaStatement& statement, StatementList& out) {
    const LuaStatement& local = statement.body[0];
    const LuaStatement& loop = statement.body[1];
    const std::string& variable = local.name;
    const bool propagates =
        statement.body.size() == 3 && statement.body[2].kind == LuaStatement::Kind::If &&
        statement.body[2].clauses.size() == 1 &&
        unwrap(statement.body[2].clauses[0].condition).kind == LuaExpression::Kind::Compare &&
        isName(unwrap(statement.body[2].clauses[0].condition).operands[0], variable) &&
        unwrap(statement.body[2].clauses[0].condition).operands[1].kind == LuaExpression::Kind::Nil;
    if (valueOf(loop).kind != LuaExpression::Kind::True || loop.body.size() > 1 ||
        (statement.body.size() > 2 && !propagates)) {
      fail(local.line, "a state machine is its local, one while true do with an if per state and "
                       "the break that leaves it");
      return;
    }
    StateMachineStatement machine{.initial = static_cast<std::size_t>(valueOf(local).number),
                                  .states = {}};
    machines_.push_back(variable);
    if (!loop.body.empty()) {
      const LuaStatement& dispatch = loop.body.front();
      if (dispatch.kind != LuaStatement::Kind::If || dispatch.hasElse) {
        fail(dispatch.line, "the loop of a state machine is one if with an arm per state");
      } else {
        for (std::size_t index = 0; index < dispatch.clauses.size() && !error_; ++index) {
          if (armOf(dispatch.clauses[index].condition, variable) !=
              static_cast<std::int64_t>(index)) {
            fail(dispatch.clauses[index].line,
                 std::format("state {} of a state machine does not test {} == {}", index, variable,
                             index));
            break;
          }
          MachineState state{.state = index, .body = {}};
          liftBlock(dispatch.clauses[index].body, state.body);
          machine.states.push_back(std::move(state));
        }
      }
    }
    machines_.pop_back();
    out.push_back({std::move(machine)});
  }

  // ---- the module

  void readReturnedTable(const LuaExpression& table, LuaListing& result) {
    std::optional<std::int64_t> script;
    const LuaExpression* sections = nullptr;
    for (std::size_t index = 0; index < table.keys.size(); ++index) {
      if (table.keys[index].name == "script" &&
          table.operands[index].kind == LuaExpression::Kind::Number) {
        script = table.operands[index].number;
      } else if (table.keys[index].name == "sections" &&
                 table.operands[index].kind == LuaExpression::Kind::Table) {
        sections = &table.operands[index];
      }
    }
    if (!script || !std::in_range<std::uint16_t>(*script) || sections == nullptr) {
      fail(table.line, "the module does not return { script = <id>, sections = { ... } }");
      return;
    }
    result.script = static_cast<std::uint16_t>(*script);
    std::set<std::int64_t> ids;
    for (std::size_t index = 0; index < sections->keys.size(); ++index) {
      const auto id = sections->keys[index].index;
      const LuaExpression& value = sections->operands[index];
      if (!id || !std::in_range<std::uint16_t>(*id) || value.kind != LuaExpression::Kind::Field ||
          value.object != RoutinesTable) {
        fail(value.line, "a section that is not [<id>] = routines.<name>");
        return;
      }
      if (!ids.insert(*id).second) {
        fail(value.line, std::format("section {:#06x} is in the table twice", *id));
        return;
      }
      const Function* function = functionNamed(value.text);
      if (function == nullptr) {
        fail(value.line, std::format("routines.{} is not defined in the module", value.text));
        return;
      }
      const auto routine = static_cast<std::size_t>(function - functions_.data());
      StructuredRoutine& structured = result.structured.routines[routine];
      if (structured.kind != RoutineKind::Section) {
        structured.kind = RoutineKind::Section;
        structured.section = static_cast<std::uint16_t>(*id);
      }
      result.listing.sections.push_back({.id = static_cast<std::uint16_t>(*id),
                                         .entry = pending_[function->entry].instruction.offset});
    }
  }

  // Puts the instructions in offset order and the statements on their indices.
  void finish(LuaListing& result) {
    std::vector<std::size_t> order(pending_.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      order[index] = index;
    }
    std::ranges::stable_sort(order, {},
                             [this](std::size_t id) { return pending_[id].instruction.offset; });
    std::vector<std::size_t> indexOf(pending_.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      indexOf[order[index]] = index;
      result.listing.instructions.push_back(std::move(pending_[order[index]].instruction));
    }
    for (StructuredRoutine& routine : result.structured.routines) {
      routine.instruction = indexOf[routine.instruction];
      renumber(routine.body, indexOf);
    }
  }

  static void renumber(StatementList& list, const std::vector<std::size_t>& indexOf) {
    for (Statement& statement : list) {
      std::visit(
          [&](auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, RunStatement>) {
              node.first = indexOf[node.first];
            } else if constexpr (std::is_same_v<Node, IfStatement>) {
              node.condition.instruction = indexOf[node.condition.instruction];
              renumber(node.whenTrue, indexOf);
              renumber(node.whenFalse, indexOf);
            } else if constexpr (std::is_same_v<Node, LoopStatement>) {
              renumber(node.body, indexOf);
            } else if constexpr (std::is_same_v<Node, ChoiceStatement>) {
              node.instruction = indexOf[node.instruction];
              for (StatementList& arm : node.arms) {
                renumber(arm, indexOf);
              }
            } else if constexpr (std::is_same_v<Node, TailCallStatement>) {
              node.instruction = indexOf[node.instruction];
            } else if constexpr (std::is_same_v<Node, StateMachineStatement>) {
              for (MachineState& state : node.states) {
                renumber(state.body, indexOf);
              }
            }
          },
          statement.node);
    }
  }

  const LuaBlock* chunk_;
  const LuaLineMap* lines_;
  std::vector<Function> functions_;
  std::vector<Pending> pending_;
  std::map<std::uint32_t, std::size_t> byOffset_;
  std::set<std::size_t> usedLines_;
  std::uint32_t nextOffset_ = 0;
  std::vector<std::string> machines_;
  std::optional<std::string> error_;
  std::optional<std::string> inconsistency_;
};

} // namespace

core::Result<LuaLineMap> readLuaLineMap(const core::YamlNode& lines) {
  if (!lines.isMapping()) {
    return core::fail("a line map is a mapping of line numbers to offsets");
  }
  LuaLineMap map;
  for (const core::YamlEntry& entry : lines.entries()) {
    std::size_t line = 0;
    const std::string& key = entry.key;
    const auto [keyEnd, keyError] = std::from_chars(key.data(), key.data() + key.size(), line);
    const std::string_view text =
        entry.value.isScalar() ? std::string_view{entry.value.text()} : std::string_view{};
    const bool hex = text.starts_with("0x") || text.starts_with("0X");
    const std::string_view digits = hex ? text.substr(2) : text;
    std::uint32_t offset = 0;
    const auto [valueEnd, valueError] = std::from_chars(
        digits.data(), digits.data() + digits.size(), offset, hex ? HexBase : DecimalBase);
    if (keyError != std::errc{} || keyEnd != key.data() + key.size() || line == 0 ||
        valueError != std::errc{} || valueEnd != digits.data() + digits.size() || digits.empty()) {
      return core::fail(
          std::format("line map entry {}: {} is not a line and an offset", key, text));
    }
    map.emplace(line, offset);
  }
  return map;
}

core::Result<LuaListing> readLuaModule(std::string_view code, const LuaLineMap* lines) {
  auto tokens = tokenize(code);
  if (!tokens) {
    return std::unexpected{tokens.error()};
  }
  auto chunk = Parser{std::move(*tokens)}.chunk();
  if (!chunk) {
    return std::unexpected{chunk.error()};
  }
  if (lines != nullptr) {
    Lifter mapped{*chunk, lines};
    auto result = mapped.lift();
    if (!result || !mapped.inconsistency()) {
      return result;
    }
    Lifter numbered{*chunk, nullptr};
    auto fallback = numbered.lift();
    if (fallback) {
      fallback->lineMapProblem = *mapped.inconsistency();
    }
    return fallback;
  }
  return Lifter{*chunk, nullptr}.lift();
}

namespace {

// The functions a composed module gives patched sections: routines.patch_<section>.
constexpr std::string_view PatchRoutinePrefix = "patch_";
// The names of story's that the modules give locals of (lua_emitter.hpp), so a patch's
// `story.ENDED` reads as the module's `ENDED`.
constexpr std::array<std::string_view, 3> StoryConstants = {"ENDED", "PLAYER", "PARTNER"};
// What a module a script only patches give starts with, as the emitter's modules do.
constexpr std::string_view ModulePrelude = "local story <const> = story\n"
                                           "local ENDED <const> = story.ENDED\n"
                                           "local PLAYER <const> = story.PLAYER\n"
                                           "local PARTNER <const> = story.PARTNER\n"
                                           "\n"
                                           "local routines = {}\n"
                                           "\n";

// The line of an error of the reader ("line N: ..."), and the rest.
std::optional<std::pair<std::size_t, std::string_view>> errorLine(std::string_view message) {
  constexpr std::string_view Prefix = "line ";
  if (!message.starts_with(Prefix)) {
    return std::nullopt;
  }
  const std::string_view rest = message.substr(Prefix.size());
  std::size_t line = 0;
  const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), line);
  const std::string_view after = rest.substr(static_cast<std::size_t>(end - rest.data()));
  if (error != std::errc{} || !after.starts_with(": ")) {
    return std::nullopt;
  }
  return std::pair{line, after.substr(2)};
}

// An error of the reader on a patch, "line N: ..." as "<patch>:N: ...".
std::string inPatch(std::string_view patch, std::string_view message) {
  if (const auto located = errorLine(message)) {
    return std::format("{}:{}: {}", patch, located->first, located->second);
  }
  return std::format("{}: {}", patch, message);
}

std::size_t newlines(std::string_view text) {
  return static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

// Reads a patch's tokens: the shape readLuaPatch describes, each section's body cut out of the
// code.
class PatchReader {
public:
  PatchReader(std::string_view code, std::vector<Token> tokens, LuaPatch& patch)
      : code_(code), tokens_(std::move(tokens)), patch_(&patch) {}

  core::Result<void> read() {
    while (!error_ && accept("local")) {
      topLocal();
    }
    const Token& returned = peek();
    expect("return");
    expect("{");
    bool script = false;
    bool sections = false;
    while (!error_ && !is("}")) {
      const Token& key = peek();
      if (accept("script")) {
        expect("=");
        script = true;
        const Token& value = next();
        if (value.kind != TokenKind::Number || !std::in_range<std::uint16_t>(value.number)) {
          fail(value, "a script that is not a number from 0 to 65535");
        }
        patch_->script = static_cast<std::uint16_t>(value.number);
      } else if (accept("sections")) {
        expect("=");
        sections = true;
        readSections();
      } else {
        fail(key, std::format("'{}' in the patch's table, which has only script and sections",
                              key.text));
      }
      if (!accept(",") && !accept(";")) {
        break;
      }
    }
    expect("}");
    accept(";");
    if (!error_ && peek().kind != TokenKind::End) {
      fail(peek(), "code after the patch's table");
    }
    if (!error_ && (!script || !sections)) {
      fail(returned, "the patch does not return { script = <id>, sections = { ... } }");
    }
    if (error_) {
      return core::fail(*error_);
    }
    return {};
  }

private:
  [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
    return tokens_[std::min(at_ + ahead, tokens_.size() - 1)];
  }

  const Token& next() {
    const Token& token = peek();
    at_ = std::min(at_ + 1, tokens_.size() - 1);
    return token;
  }

  [[nodiscard]] bool is(std::string_view text, std::size_t ahead = 0) const {
    const Token& token = peek(ahead);
    return (token.kind == TokenKind::Symbol || token.kind == TokenKind::Name) && token.text == text;
  }

  bool accept(std::string_view text) {
    if (is(text)) {
      next();
      return true;
    }
    return false;
  }

  void expect(std::string_view text) {
    if (!accept(text)) {
      fail(peek(), std::format("'{}' where '{}' goes", peek().text, text));
    }
  }

  void fail(const Token& token, std::string_view message) {
    if (!error_) {
      error_ = std::format("{}:{}: {}", patch_->name, token.line, message);
    }
  }

  // local story <const> = story, local ENDED <const> = story.ENDED: the module has the same.
  void topLocal() {
    const Token& local = next();
    if (accept("<")) {
      next();
      expect(">");
    }
    expect("=");
    const bool story = accept(StoryTable);
    std::string field;
    if (story && accept(".")) {
      field = next().text;
    }
    const bool same = field.empty() ? local.text == StoryTable : field == local.text;
    if (!error_ && (!story || !same)) {
      fail(local, "a local at the top of a patch that is not one of story's own names "
                  "(local story <const> = story, local ENDED <const> = story.ENDED)");
    }
  }

  void readSections() {
    expect("{");
    while (!error_ && !is("}")) {
      readSection();
      if (!accept(",") && !accept(";")) {
        break;
      }
    }
    expect("}");
  }

  void readSection() {
    const Token& open = peek();
    expect("[");
    const Token& id = next();
    if (id.kind != TokenKind::Number || !std::in_range<std::uint16_t>(id.number)) {
      fail(id, "a section that is not a number from 0 to 65535");
    }
    expect("]");
    expect("=");
    if (!accept("function")) {
      fail(peek(), "a section that is not [<id>] = function(original) ... end");
      return;
    }
    expect("(");
    std::string original;
    if (peek().kind == TokenKind::Name && !is(")")) {
      original = next().text;
    }
    const Token& close = peek();
    expect(")");
    if (error_) {
      return;
    }
    LuaPatchSection section{
        .id = static_cast<std::uint16_t>(id.number), .pieces = {}, .line = close.line};
    if (std::ranges::contains(patch_->sections, section.id, &LuaPatchSection::id)) {
      fail(open, std::format("section {:#06x} is in the patch twice", section.id));
      return;
    }
    std::string piece;
    std::size_t copied = close.end;
    // The blocks open in the body: the function's own `end` closes the last.
    std::size_t depth = 1;
    while (!error_) {
      const Token& token = peek();
      if (token.kind == TokenKind::End) {
        fail(open, "a section's function that does not end");
        return;
      }
      if (token.kind == TokenKind::Name) {
        if (token.text == "function" || token.text == "do" || token.text == "if" ||
            token.text == "repeat") {
          ++depth;
        } else if ((token.text == "end" || token.text == "until") && --depth == 0) {
          piece += code_.substr(copied, token.begin - copied);
          section.pieces.push_back(std::move(piece));
          next();
          break;
        } else if (!original.empty() && token.text == original) {
          if (!is("(", 1) || !is(")", 2)) {
            fail(token, std::format("{} used other than as {}(): the walk cannot follow it",
                                    original, original));
            return;
          }
          piece += code_.substr(copied, token.begin - copied);
          section.pieces.push_back(std::exchange(piece, {}));
          copied = peek(2).end;
          at_ += 3;
          continue;
        } else if (token.text == StoryTable && is(".", 1) &&
                   std::ranges::contains(StoryConstants, peek(2).text)) {
          piece += code_.substr(copied, token.begin - copied);
          piece += peek(2).text;
          copied = peek(2).end;
          at_ += 3;
          continue;
        }
      }
      next();
    }
    patch_->sections.push_back(std::move(section));
  }

  std::string_view code_;
  std::vector<Token> tokens_;
  LuaPatch* patch_;
  std::size_t at_ = 0;
  std::optional<std::string> error_;
};

} // namespace

core::Result<LuaPatch> readLuaPatch(std::string_view code, std::string name) {
  LuaPatch patch{.name = std::move(name), .script = 0, .sections = {}};
  auto tokens = tokenize(code);
  if (!tokens) {
    return core::fail(inPatch(patch.name, tokens.error().message));
  }
  if (auto read = PatchReader{code, std::move(*tokens), patch}.read(); !read) {
    return std::unexpected{read.error()};
  }
  return patch;
}

std::string ComposedLuaModule::located(std::string_view message) const {
  const auto at = errorLine(message);
  if (!at) {
    return std::string{message};
  }
  for (const PatchLines& lines : patchLines) {
    if (at->first >= lines.first && at->first <= lines.last) {
      return std::format("{}:{}: {}", lines.patch, lines.patchLine + (at->first - lines.first),
                         at->second);
    }
  }
  return std::string{message};
}

core::Result<ComposedLuaModule> composeLuaPatches(std::optional<std::string_view> module,
                                                  std::uint16_t script,
                                                  std::span<const LuaPatch> patches) {
  // Each section's patch: the last one that gives it.
  std::map<std::uint16_t, std::pair<const LuaPatch*, const LuaPatchSection*>> winners;
  for (const LuaPatch& patch : patches) {
    if (patch.script != script) {
      continue;
    }
    for (const LuaPatchSection& section : patch.sections) {
      winners.insert_or_assign(section.id, std::pair{&patch, &section});
    }
  }
  ComposedLuaModule result;
  // The returned table's sections, in the module's order, by their routine.
  std::vector<std::pair<std::uint16_t, std::string>> table;
  if (module) {
    auto tokens = tokenize(*module);
    if (!tokens) {
      return std::unexpected{tokens.error()};
    }
    // The module's returned table names no function, so the last `return` is the module's own.
    const auto returned =
        std::ranges::find_if(std::views::reverse(*tokens), [](const Token& token) {
          return token.kind == TokenKind::Name && token.text == "return";
        });
    if (returned == std::ranges::rend(*tokens)) {
      return core::fail("line 1: the module does not return { script = <id>, sections = { ... } }");
    }
    const std::size_t returnBegin = returned->begin;
    auto chunk = Parser{std::move(*tokens)}.chunk();
    if (!chunk) {
      return std::unexpected{chunk.error()};
    }
    const LuaExpression* sections = nullptr;
    if (!chunk->empty() && chunk->back().kind == LuaStatement::Kind::Return) {
      const LuaExpression& value = valueOf(chunk->back());
      for (std::size_t index = 0; index < value.keys.size(); ++index) {
        if (value.keys[index].name == "sections" &&
            value.operands[index].kind == LuaExpression::Kind::Table) {
          sections = &value.operands[index];
        }
      }
    }
    if (sections == nullptr) {
      return core::fail(std::format(
          "line {}: the module does not return {{ script = <id>, sections = {{ ... }} }}",
          chunk->empty() ? 1 : chunk->back().line));
    }
    for (std::size_t index = 0; index < sections->keys.size(); ++index) {
      const auto id = sections->keys[index].index;
      const LuaExpression& value = sections->operands[index];
      if (!id || !std::in_range<std::uint16_t>(*id) || value.kind != LuaExpression::Kind::Field ||
          value.object != RoutinesTable) {
        return core::fail(
            std::format("line {}: a section that is not [<id>] = routines.<name>", value.line));
      }
      table.emplace_back(static_cast<std::uint16_t>(*id), value.text);
    }
    result.code = std::string{module->substr(0, returnBegin)};
  } else {
    result.code = std::string{ModulePrelude};
  }
  if (!result.code.empty() && result.code.back() != '\n') {
    result.code += '\n';
  }
  for (const auto& [id, winner] : winners) {
    const auto& [patch, section] = winner;
    const auto entry = std::ranges::find(table, id, &std::pair<std::uint16_t, std::string>::first);
    if (section->pieces.size() > 1 && entry == table.end()) {
      return core::fail(std::format("{}:{}: section {:#06x} calls the original, which script {} "
                                    "does not have",
                                    patch->name, section->line, id, script));
    }
    std::string body = section->pieces.front();
    for (std::size_t piece = 1; piece < section->pieces.size(); ++piece) {
      body += std::format("{}.{}(){}", RoutinesTable, entry->second, section->pieces[piece]);
    }
    const std::string routine = std::format("{}{:04x}", PatchRoutinePrefix, id);
    const std::size_t first = newlines(result.code) + 1;
    result.patchLines.push_back({.first = first,
                                 .last = first + newlines(body),
                                 .patch = patch->name,
                                 .patchLine = section->line});
    result.code += std::format("function {}.{}(){} end\n", RoutinesTable, routine, body);
    if (entry != table.end()) {
      entry->second = routine;
    } else {
      table.emplace_back(id, routine);
    }
  }
  result.code += std::format("\nreturn {{\n  script = {},\n  sections = {{\n", script);
  for (const auto& [id, routine] : table) {
    result.code += std::format("    [{:#06x}] = {}.{},\n", id, RoutinesTable, routine);
  }
  result.code += "  },\n}\n";
  return result;
}

} // namespace dwe::formats::script
