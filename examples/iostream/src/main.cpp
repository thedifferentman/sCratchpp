#include <iostream>
#include <iomanip>
#include <string>

int main() {
    std::cout << "C++ iostream / Console\n";
    std::cout << "Name: ";
    std::string name;
    if (!std::getline(std::cin, name)) return 1;
    std::cout << "Hello, " << name << "!\n";
    std::cout << "Number: ";
    double number;
    if (!(std::cin >> number)) {
        std::cerr << "Invalid number.\n";
        return 2;
    }
    std::cout << std::fixed << std::setprecision(2) << number << " * 2 = " << number * 2 << '\n';
    // Normal process exit flushes pending output; no Console-specific call needed.
}
