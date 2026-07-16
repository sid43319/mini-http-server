# HTTP Server

A minimal C HTTP server that listens on port `8080`, accepts one connection at a time, parses a basic HTTP request line, and serves static pages from the `www/` directory.

## Overview

This project is a learning-focused implementation of an HTTP server in C. It uses POSIX sockets and a simple accept loop to handle client requests one by one.

## Features

- Single TCP listening socket on port `8080`
- Accepts incoming connections in a loop
- Parses the HTTP request line from the request buffer
- Supports only `GET` requests
- Serves:
  - `/` → `www/index.html`
  - `/about.html` → `www/about.html`
- Sends basic error responses for invalid requests
- Closes each client socket after responding

## Repository Structure

- `src/main.c` — server implementation
- `www/index.html` — default home page
- `www/about.html` — about page
- `static/` — additional static content directory

## Build

Compile the server with `gcc`:

```sh
gcc -Wall -Wextra -pedantic src/main.c -o http-server
```

If you already built the project, there may also be an existing binary such as `http-server-test`.

## Run

Start the server from the repository root:

```sh
./http-server
```

Then visit the server or make requests from a terminal:

```sh
curl http://localhost:8080/
curl http://localhost:8080/about.html
```

## Supported Requests

- `GET /` → serves `www/index.html`
- `GET /about.html` → serves `www/about.html`

## Error Handling

The server returns basic HTTP error responses for these conditions:

- `400 Bad Request` — malformed request path or invalid request format
- `404 Not Found` — unsupported path or missing file
- `405 Method Not Allowed` — any method other than `GET`
- `505 HTTP Version Not Supported` — any HTTP version other than `HTTP/1.1`

## Notes

- The current server is intentionally simple and not production-ready.
- It does not support persistent connections, headers beyond the request line, or dynamic content.
- Client-side errors during request handling close only the client socket, while the server continues accepting new connections.

## Possible Improvements

- serve arbitrary static files based on the requested path
- implement content-type detection
- support HTTP/1.0 and keep-alive
- add concurrency using threads or non-blocking I/O
- improve request parsing and header support
