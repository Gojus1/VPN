# This more a routing simulator (like RIP), VPN works on http servers only, next project (VPN 2.0) will be a fully working VPN program

You need your own server (just change the IP) compile/run with "g++ vpn_client.cpp -o vpn_client -lws2_32 -std=c++11" on 1 server and  "g++ proxy_server.cpp -o proxy_server -pthread" on other.
