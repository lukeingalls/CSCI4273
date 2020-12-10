This is my implementation of the distributed file server.

To run
1. run `make` in both the `DFC` and `DFS` directories to compile the files.
1. In each of the `DFS#` directories you should start an instance of the DFS server. (ex: `../DFS1 23023 ../dsf.conf`)
1. The client can run wherever. It will need to be passed the config file to use.

This implementation is simple in that it uses the hashes as logical indeces into an array of the open sockets. Everything was built as a nonredundant dfs and then the hashing and the other stuff was added after.

The creds on the server are stored as a linked list. This can mean an unlimited amount of creds.

On the client the list command creates a linked list fo files and the sockets which they can be retrieved. This process is used for get and list and can help signify whether files are availible.