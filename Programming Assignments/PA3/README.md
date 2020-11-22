### PA3

The general architecture of my approach is as follows. Upon receiving a request from the client it is parsed be the `parse_request` function. This will isolate all pertinent information about the destination of the packet (`hostent` info, port, etc). Within the scope of this function are the functionality for the DNS cache and the blacklist. When we obtain the destination domain name we first check whether this information is cached. If it isn't cached we will resolve it and then cache the resolved version. From here we iterate through the blacklist to see if either the ip or the domain name have been blacklisted and if they have been than we mark the request object as blacklisted.

After the `parse_request` some error handling occurs. At this point we know whether we have a host, the type of request, and whether this connection is allowable. The functions `transfer_400`, `transfer_403`, and `transfer_404` are invoked if a condition for doing so is met.

Once we've validated everything and made a connection (line 273). I start to handle the transfers. First I assess whether the request is cached by checking the hashes. My approach with the hashes was to take the SHA1 hash of the first line of the request only. I did so because this would be unique to each request as it has the pertinent URI and avoids the parts of the request that can change (like the date/time). Originally I did the other but obviously a changing data/time results in a changing hash which certainly makes matching tricky.

Assuming a cache miss I go to the function `transfer_get` and I start by defining the cache page. I have implemented my cache as a linked list because it is scalable. With the cache page I can then store the file as I read it from the external and have it exist in cache. This function will complete the transfer and also populate the cache frame associated with it.

If I had had a cache hit I know I can retrieve the item from the cache and so I simply call `serve_from_cache` and the file is sent from memory to the proxied connection.

Outside of this I have to signal handlers. One for `sigint` which will close the connection and deallocate cache. The other is for `sigpipe` which is for broken connections. This will close the active thread.

----------

Anything beyond this is just helper functions. There are numerous that conern themselves with the linked list operations of the cache. The other helpers are to populate the blacklist data structure and check the cache status of hosts.