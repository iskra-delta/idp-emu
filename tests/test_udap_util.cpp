#include <regex>
#include <string>
#include <string_view>

#include <sdcc/util.h>

int main()
{
    const std::string storage = "prefix:FOO:123:suffix";
    const std::string_view line(storage.data() + 7, 7);
    const auto groups = sdcc::util::match(
        line, std::regex(R"(([A-Z]+):([0-9]+))"));

    if (!groups || groups->size() != 2)
        return 1;
    if ((*groups)[0] != "FOO" || (*groups)[1] != "123")
        return 2;
    return 0;
}
