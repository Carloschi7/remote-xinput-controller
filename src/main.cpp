//#define SERVER_IMPL

#ifdef SERVER_IMPL

#include "server.hpp"

int main() 
{
	ServerImplementation();
}

#else

#include "host.hpp"
#include "client.hpp"

int main()
{
	std::string ans;
	std::cout << "Start server? (Y/N)\n";
	std::cin >> ans;
	std::string ip;
	std::cout << "insert the server IP:\n";
	std::cin >> ip;
	if (ans == "Y")
		HostImplementation(ip.c_str(), 20000);
	else 
		ClientImplementation(ip.c_str(), 20000);
	
}
#endif

