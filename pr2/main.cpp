#include <iostream>
#include <vector>
#include <array>
#include <set>
#include <limits>

void eraseDuplicates(std::vector <std::vector <int>>& table) {
	std::set <int> seenElements;
	for (int i { 0 }; i < table.size(); i++) {
		for (int j { 0 }; j < table[0].size(); j++) {
			if (seenElements.count (table[i][j])) {
				table[i][j] = -1;
			} else {
				seenElements.insert(table[i][j]);
			}
		}
	}
}

std::array <int, 2> searchElement(std::vector <std::vector <int>>& table, int value) {
	for (int i { 0 }; i < table.size(); i++) {
		for (int j { 0 }; j < table[0].size(); j++) {
			if (table[i][j] == value) {
				return {i, j};
			}
		}
	}
	return {-1, -1};
}

void deleteElement (std::vector <std::vector <int>>& table, int row, int col) {
	table[row][col] = -1;
}

void insertElement (std::vector <std::vector <int>>& table, int row, int col, int val) {
	table[row][col] = val;
}

void ignoreLine () {
	if (std::cin.fail()) {
		std::cin.clear();
	}
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int main () {
	std::array<int, 2> dims;
	std::cout << "Enter dimensions separated by a space\n";
	std::cin >> dims[0] >> dims[1];
	if (std::cin.fail()) {
		ignoreLine();
		std::cout << "Dimension reading failed! Try again\n";
		std::cin >> dims[0] >> dims[1];
	}
	ignoreLine();

	std::vector<std::vector<int>> table(dims[0], std::vector<int>(dims[1]));
	std::cout << "Enter elements in row major order:\n";
	for (int i { 0 }; i < dims[0]; i++) {
		for (int j { 0 }; j < dims[1]; j++) {
			std::cin >> table[i][j];
			if (std::cin.fail()) {
				// so that i++ on the next loop will return to the current row
				// index
				i--;
				std::cout << "Row " << i + 1 << " failed, try again\n";
				break;
			}
		}
		if (std::cin.fail()) {
			ignoreLine();
		}
	}

	while (true) {
		std::cout << "Enter d to delete an element (set its value to -1), "
			"p to find duplicate entries and set them to -1, "
			"i to insert an entry at any position in the table, "
			"s to search for an element in the table, "
			"h to show the table, "
			"q to quit\n";
		char op { };
		std::cin >> op;
		ignoreLine();
		if (op == 's') {
			int value;
			std::cout << "Enter value to be searched\n";
			std::cin >> value;
			while (!std::cin) {
				std::cout << "Entering value failed, try again\n";
				ignoreLine();
				std::cin >> value;
			}
			ignoreLine();
			std::array <int, 2> indices { searchElement (table, value) };
			std::cout << "Element found at ["
				<< indices[0] << ' '
				<< indices[1] << "]\n";
		}
		else if (op == 'd') {
			int row, col;
			std::cout << "Enter row:\n";
			std::cin >> row;
			while (!std::cin) {
				std::cout << "Entering row failed, try again\n";
				ignoreLine();
				std::cin >> row;
			}
			ignoreLine();
			std::cout << "Enter col:\n";
			std::cin >> col;
			while (!std::cin) {
				std::cout << "Entering col failed, try again\n";
				ignoreLine();
				std::cin >> col;
			}
			ignoreLine();
			deleteElement(table, row, col);
		} else if (op == 'i') {
			int row, col;
			std::cout << "Enter row:\n";
			std::cin >> row;
			while (!std::cin) {
				std::cout << "Entering row failed, try again\n";
				ignoreLine();
				std::cin >> row;
			}
			ignoreLine();
			std::cout << "Enter col:\n";
			std::cin >> col;
			while (!std::cin) {
				std::cout << "Entering row failed, try again\n";
				ignoreLine();
				std::cin >> col;
			}
			ignoreLine();
			std::cout << "Enter value:\n";
			int value;
			std::cin >> value;
			while (!std::cin) {
				std::cout << "Entering value failed, try again\n";
				ignoreLine();
				std::cin >> value;
			}
			ignoreLine();
			insertElement(table, row, col, value);
		} else if (op == 'p') {
			std::cout << "Erasing duplicates\n";
			eraseDuplicates (table);
		} else if (op == 'q') {
			break;
		} else if (op == 'h') {
			for (int i { 0 }; i < dims[0]; i++) {
				for (int j { 0 }; j < dims[1]; j++) {
					std::cout << table[i][j] << " ";
				}
				std::cout << '\n';
			}
		} else {
			std::cout << "Invalid operation!\n";
			continue;
		}
	}

	return 0;
}
