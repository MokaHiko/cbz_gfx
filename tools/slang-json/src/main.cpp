#include <iostream>
#include <string>

#include <slang.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cerr << "Usage: slang-json <file.slang> -o <out.json>\n";
    return 1; // Return non-zero to indicate incorrect usage
  }

  std::string inputFile = argv[1];
  std::string outputFlag = argv[2];
  std::string outputFile = argv[3];

  if (outputFlag != "-o") {
    std::cerr << "Expected '-o' flag as second argument.\n";
    return 1;
  }

  std::cout << "Input File: " << inputFile << "\n";
  std::cout << "Output File: " << outputFile << "\n";

  // TODO: Add actual Slang compilation/reflection logic here

  return 0;
}
