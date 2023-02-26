#include <utility>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <sstream>

std::string slurp(std::ifstream& in) {
	std::ostringstream sstr;
	sstr << in.rdbuf();
	//return std::move(sstr.str());
	return sstr.str();
}

int main() {
	std::ifstream fis("mainVert.cso", std::ifstream::in | std::ifstream::binary);
	std::string str = slurp(fis);
	std::cout << str << std::endl;
	return 0;
}