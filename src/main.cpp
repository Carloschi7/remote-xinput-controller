#include "host.hpp"
#include "client.hpp"

int main() 
{
	std::string ans;
	std::cout << "Start server? (Y/N)\n";
	std::cin >> ans;
	if (ans == "Y")
		host_implementation();
	else {
		std::string ip;
		std::cout << "select an ip to connect to:\n";
		std::cin >> ip;
		client_implementation(ip.c_str(), 20000);
	}
}