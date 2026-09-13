#include <string>

int main() {
    std::string text = "abcdefghijklmnopqrstuvwx";
    text += "yz";
    text.replace(2, 2, "--");
    if (text.size() != 26 || text.substr(0, 5) != "ab--e" || text.find("xyz") != 23) return 1;
    auto copy = text;
    text.clear();
    if (!text.empty() || copy.back() != 'z') return 2;
    return 0;
}
