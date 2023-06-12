# fcgi_proc
FCGI Process Monitor Interface

## Overview

The fcgi_proc service is a FastCGI server which interfaces with the
procmon utility to perform process management activities via GET and POST
HTTP requests.

The fcgi_proc service currently supports the following features:

- start a stopped process
- stop a running process
- restart a running process
- list processes

Note the fcgi_proc service can only perform actions on processes which are
managed by the procmon service.

## Build

The build script installs build tools and the lighttpd web server via apt-get.
It then compiles and installs the FCGI library before finally compiling
and installing the fcgi_vars service.

The fcgi_vars service is automatically invoked by the lighttpd web server when
the lighttpd web server starts.

```
./build.sh
```

## Prerequisites

The fcgi_vars service requires the following components:

- procmon : process monitor utility ( https://github.com/tjmonk/procmon )
- FCGI : Fast CGI ( https://github.com/FastCGI-Archives/fcgi2 )

The example is run using the lighttpd web server ( https://www.lighttpd.net/).

The build script installs the lighttpd web server, and builds the FCGI library.

## Set up the Process Monitor

```
procmon -f test/procmon.json &
```

## Start the lighttpd server

```
lighttpd -f test/lighttpd.conf &
```

## Sample Lighttpd Configuration

```
server.modules = (
    "mod_auth",
    "mod_cgi",
    "mod_fastcgi",
    "mod_setenv"
)

server.document-root        = "/www/pages"
server.upload-dirs          = ( "/var/cache/lighttpd/uploads" )
server.errorlog             = "/var/log/lighttpd/error.log"
server.pid-file             = "/var/run/lighttpd.pid"
#server.username             = "www-data"
#server.groupname            = "www-data"
server.port                 = 80

setenv.add-response-header = ("Access-Control-Allow-Origin" => "*" )

index-file.names            = ( "index.php", "index.html", "index.lighttpd.html" )
url.access-deny             = ( "~", ".inc" )
static-file.exclude-extensions = ( ".php", ".pl", ".fcgi" )

compress.cache-dir          = "/var/cache/lighttpd/compress/"
compress.filetype           = ( "application/javascript", "text/css", "text/html", "text/plain" )

fastcgi.debug = 1
fastcgi.server = (
  "/procs" => ((
    "bin-environment" => (
        "LD_LIBRARY_PATH" => "/usr/local/lib"
    ),
    "bin-path" => "/usr/local/bin/fcgi_proc",
    "socket" => "/tmp/fcgi_proc.sock",
    "check-local" => "disable",
    "max-procs" => 1,
  ))
)
```

## List Running Processes

```
curl localhost/procs?list
```
```
[{"name": "procmon1","pid": 21418,"runcount": 2,"since": "12m01s","state": "running","exec": "procmon -F test/procmon.json"},{"name": "procmon2","pid": 21415,"runcount": 1,"since": "12m02s","state": "running","exec": "procmon -f test/procmon.json"},{"name": "sleep2","pid": 31933,"runcount": 12,"since": "1m00s","state": "running","exec": "sleep 60"},{"name": "sleep1","pid": 32583,"runcount": 40,"since": "13s","state": "running","exec": "sleep 18"}]
```

## Stop a Process

```
curl localhost/procs?stop=sleep1
```

```
curl localhost/procs?list
```

```
[{"name": "procmon1","pid": 21418,"runcount": 2,"since": "13m21s","state": "running","exec": "procmon -F test/procmon.json"},{"name": "procmon2","pid": 21415,"runcount": 1,"since": "13m22s","state": "running","exec": "procmon -f test/procmon.json"},{"name": "sleep2","pid": 33391,"runcount": 14,"since": "20s","state": "running","exec": "sleep 60"},{"name": "sleep1","pid": 33174,"runcount": 43,"since": "23s","state": "stopped","exec": "sleep 18"}]

```

## Start a Process

```
curl localhost/procs?start=sleep1
```

```
curl localhost/procs?list
```

```
[{"name": "procmon1","pid": 21418,"runcount": 2,"since": "15m13s","state": "running","exec": "procmon -F test/procmon.json"},{"name": "procmon2","pid": 21415,"runcount": 1,"since": "15m14s","state": "running","exec": "procmon -f test/procmon.json"},{"name": "sleep2","pid": 34486,"runcount": 16,"since": "12s","state": "running","exec": "sleep 60"},{"name": "sleep1","pid": 34598,"runcount": 44,"since": "4s","state": "running","exec": "sleep 18"}]
```

## Restart a Process

```
curl localhost/procs?restart=sleep1
```

```
curl localhost/procs?list
```

```
[{"name": "procmon1","pid": 21418,"runcount": 2,"since": "16m41s","state": "running","exec": "procmon -F test/procmon.json"},{"name": "procmon2","pid": 21415,"runcount": 1,"since": "16m42s","state": "running","exec": "procmon -f test/procmon.json"},{"name": "sleep2","pid": 35158,"runcount": 17,"since": "40s","state": "running","exec": "sleep 60"},{"name": "sleep1","pid": 35513,"runcount": 49,"since": "3s","state": "running","exec": "sleep 18"}]
```
