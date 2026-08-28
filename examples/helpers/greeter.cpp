// greeter -- the "hello" example's task program.
// Prints a greeting and writes greeting.txt in the working directory.
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string who = argc > 1 ? argv[1] : "world";
    std::cout << "Hello, " << who << "! This task ran as an external process.\n";
    std::ofstream out("greeting.txt");
    out << "Hello, " << who << "!\n";
    return 0;
}
