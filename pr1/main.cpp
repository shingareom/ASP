#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

int main() {
    std::stringstream line2;
    std::string line;
    std::string word;
    int count = 0;
    int wordCount = 0;
    std::ifstream inputFile("input.txt"); 

    if (inputFile.is_open()) {
        while (std::getline(inputFile, line)) {
            count++;
            line2.clear();
            line2.str(line);
            while (line2 >> word) {
                wordCount++;
            }
        }        
    } else {
        std::cerr << "Error: Unable to open file" << std::endl;
    }
    std::cout << "count = " << count <<std::endl;
    std::cout << "word co = " << wordCount <<std::endl;
    return 0;
}
