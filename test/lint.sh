command -v clang-format 2>/dev/null 2>&1 || sudo dnf install -y clang-tools-extra

base=https://download.redpesk.bzh/redpesk-ci/redpesk-format-style
test -f .clang-format || wget -O .clang-format $base/clang-format
git ls-files \
  | grep -E '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' \
  | xargs clang-format --dry-run --style=file:.clang-format --Werror || exit 1
