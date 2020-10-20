# Programming Assignment 2

This directory contains my code implementing the http web server. To run the server first compile by typing `make` and from there you can run the server by typing `./server <port number>`. On your local machine this would mean the server can be accessed by using a web browser and navigating to `localhost:<port number>`.

As far as what is supported by my implementation:
1. Multithreading: Each request is passed to a unique thread for handling (with a distinct port).
1. Basic error checking: The server can detect codes `200`, `404`, and `400`. Any other issues encountered will return `500`. The `404` error will display a messages in browser to inform the user that the page was unreachable.
1. If the server receives a sigint that will prompt it to cease further connections and any requests in progress will finish and then no longer be active.
