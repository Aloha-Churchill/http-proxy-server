# HTTP Proxy Server
Implementation of HTTP proxy server with blocklist and caching with timeout.

## Run
1. Setup Proxy Mode on Browser

Open browser and set it to run in proxy mode. Set the proxy name to localhost and port number to 8888 or whichever port you choose. 


2. Start proxy server

```make all```
```make run``` or ```.\proxy [PORTNO] [TIMEOUT]```

3. Use proxy server

 In browser, type in name of website you want to go to. The proxy only accepts HTTP requests and not HTTPS requests. Try a site like http://nginx.org/en/ or www.example.com.


4. Other methods than browser

The proxy can also be used with wget, curl, nc, etc. 
 ## Cleanup
 ```make clean```

